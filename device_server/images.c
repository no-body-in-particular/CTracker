#include "images.h"
#include "config.h"
#include "logfiles.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Images are appended to <imei>.images.db in a container deliberately kept trivial to take
 * apart by hand. Each record is one plain ASCII header line followed by the image bytes
 * exactly as the device sent them, then a newline:
 *
 *     CTIMG1 <unix_ts> <device_time> <bytes> <lat> <lon>\n
 *     <bytes of JPEG>
 *     \n
 *
 * So `strings file.images.db | grep CTIMG1` prints the index with offsets implied by the
 * sizes, and pulling one out is a seek and a read of <bytes> - no library and no schema.
 * The payload is stored verbatim rather than base64'd or compressed, so the JPEG start and
 * end markers survive; if the headers were ever damaged the images are still recoverable
 * with any carver that looks for FFD8..FFD9.
 */

#define IMAGE_MAGIC       "CTIMG1"
#define MAX_IMAGE_SIZE    (4 * 1024 * 1024)
#define MAX_IMAGE_PACKETS 8192


void image_discard(connection * conn) {
    if (conn->image_buffer) {
        free(conn->image_buffer);
        conn->image_buffer = 0;
    }

    conn->image_len = 0;
    conn->image_expected_packets = 0;
    conn->image_next_packet = 0;
    conn->image_time[0] = 0;
}


bool image_begin(connection * conn, const char * device_time, size_t total_packets) {
    image_discard(conn);

    if (total_packets == 0 || total_packets > MAX_IMAGE_PACKETS) {
        log_line(conn, "  image: implausible packet count %u, ignoring\n", (unsigned)total_packets);
        return false;
    }

    //the device says how many packets are coming and every packet but the last is a fixed
    //size, so the upper bound is known before the first byte arrives
    //a packet is 1024 bytes of picture, but some firmware sends it hex encoded and declares
    //twice that; size for the larger of the two so neither layout runs out of room
    size_t cap = total_packets * 2048 + 2048;

    if (cap > MAX_IMAGE_SIZE) {
        log_line(conn, "  image: %u packets is larger than the %u byte limit, ignoring\n",
                 (unsigned)total_packets, (unsigned)MAX_IMAGE_SIZE);
        return false;
    }

    conn->image_buffer = malloc(cap);

    if (!conn->image_buffer) {
        log_line(conn, "  image: could not allocate %u bytes\n", (unsigned)cap);
        return false;
    }

    conn->image_len = 0;
    conn->image_capacity = cap;
    conn->image_expected_packets = total_packets;
    conn->image_next_packet = 1;
    snprintf(conn->image_time, sizeof(conn->image_time), "%s", device_time ? device_time : "");
    return true;
}


bool image_append(connection * conn, size_t packet_no, const unsigned char * data, size_t len) {
    if (!conn->image_buffer) {
        return false;
    }

    //Packets are defined to arrive in order, each one acknowledged before the next is sent.
    //A repeat of the packet just stored is normal - it means our acknowledgement was lost -
    //so accept it silently rather than tearing the whole image down.
    if (packet_no + 1 == conn->image_next_packet) {
        log_line(conn, "  image: duplicate packet %u, already have it\n", (unsigned)packet_no);
        return true;
    }

    if (packet_no != conn->image_next_packet) {
        log_line(conn, "  image: packet %u arrived while expecting %u, discarding image\n",
                 (unsigned)packet_no, (unsigned)conn->image_next_packet);
        image_discard(conn);
        return false;
    }

    if (conn->image_len + len > conn->image_capacity) {
        log_line(conn, "  image: packet %u would overrun the buffer, discarding image\n", (unsigned)packet_no);
        image_discard(conn);
        return false;
    }

    memcpy(conn->image_buffer + conn->image_len, data, len);
    conn->image_len += len;
    conn->image_next_packet++;
    return true;
}


bool image_complete(connection * conn) {
    return conn->image_buffer
           && conn->image_expected_packets > 0
           && conn->image_next_packet > conn->image_expected_packets;
}


bool image_store(connection * conn) {
    if (!conn->image_buffer || conn->image_len == 0) {
        image_discard(conn);
        return false;
    }

    FILE * fp = fopen(conn->images_file, "ab");

    if (!fp) {
        log_line(conn, "  image: could not open %s for append\n", conn->images_file);
        image_discard(conn);
        return false;
    }

    time_t now = time(0);
    fprintf(fp, "%s %ld %s %u %f %f\n", IMAGE_MAGIC, (long)now,
            conn->image_time[0] ? conn->image_time : "-",
            (unsigned)conn->image_len, conn->current_lat, conn->current_lon);
    size_t written = fwrite(conn->image_buffer, 1, conn->image_len, fp);
    fputc('\n', fp);
    fclose(fp);

    if (written != conn->image_len) {
        log_line(conn, "  image: short write, %u of %u bytes\n", (unsigned)written, (unsigned)conn->image_len);
        image_discard(conn);
        return false;
    }

    log_line(conn, "  image: stored %u bytes in %s\n", (unsigned)conn->image_len, conn->images_file);

    //The event carries the same unix timestamp that heads the record, which is what the web
    //side matches on to offer the picture - it needs no offset into the file and so cannot
    //be invalidated by the file being truncated or rotated underneath it.
    char message[128] = {0};
    snprintf(message, sizeof(message), "photo:%ld", (long)now);
    log_event(conn, message);
    image_discard(conn);
    return true;
}

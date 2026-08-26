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
 * apart by hand. Each record is one plain ASCII header line, then one line of the image as
 * hex, so the whole file is printable:
 *
 *     CTIMG2 <unix_ts> <device_time> <bytes> <lat> <lon>\n
 *     <2 * bytes hex characters>\n
 *
 * <bytes> is the size of the picture itself, so the hex line is twice that long. Being all
 * text means the file can be paged, grepped and diffed like anything else, and one picture
 * comes out with nothing but shell:
 *
 *     grep -A1 '^CTIMG2 1787738793' f.images.db | tail -1 | xxd -r -p > out.jpg
 *
 * CTIMG1 was the same layout with the payload written as raw bytes. Readers still accept it;
 * only the writer has moved on.
 */

#define IMAGE_MAGIC       "CTIMG2"
#define AUDIO_MAGIC       "CTAUD1"
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


/*
 * Both a picture and a monitor recording arrive down the same voice-packet path, and on this
 * firmware both can announce themselves with the same broken "IWnull" id, so the id cannot
 * be used to tell them apart. The bytes can: a JPEG opens FF D8 FF and an AMR file opens with
 * the literal "#!AMR". Anything else is stored as a picture, which is what it was before.
 */
static bool payload_is_audio(const unsigned char * data, size_t len) {
    return len >= 5 && memcmp(data, "#!AMR", 5) == 0;
}


bool image_store(connection * conn) {
    if (!conn->image_buffer || conn->image_len == 0) {
        image_discard(conn);
        return false;
    }

    bool audio = payload_is_audio(conn->image_buffer, conn->image_len);
    const char * path = audio ? (const char *)conn->audio_file : (const char *)conn->images_file;
    const char * magic = audio ? AUDIO_MAGIC : IMAGE_MAGIC;
    const char * what = audio ? "audio" : "image";
    FILE * fp = fopen(path, "ab");

    if (!fp) {
        log_line(conn, "  %s: could not open %s for append\n", what, path);
        image_discard(conn);
        return false;
    }

    time_t now = time(0);
    fprintf(fp, "%s %ld %s %u %f %f\n", magic, (long)now,
            conn->image_time[0] ? conn->image_time : "-",
            (unsigned)conn->image_len, conn->current_lat, conn->current_lon);

    static const char hexdigits[] = "0123456789abcdef";
    size_t written = 0;

    //written a chunk at a time rather than two fputc calls per byte: a 40KB picture is
    //80000 characters and going through stdio for each one is needless
    for (size_t i = 0; i < conn->image_len;) {
        char line[1024];
        size_t n = 0;

        while (i < conn->image_len && n + 2 <= sizeof(line)) {
            unsigned char b = conn->image_buffer[i++];
            line[n++] = hexdigits[b >> 4];
            line[n++] = hexdigits[b & 0x0f];
        }

        if (fwrite(line, 1, n, fp) != n) {
            break;
        }

        written += n / 2;
    }

    fputc('\n', fp);
    fclose(fp);

    if (written != conn->image_len) {
        log_line(conn, "  image: short write, %u of %u bytes\n", (unsigned)written, (unsigned)conn->image_len);
        image_discard(conn);
        return false;
    }

    log_line(conn, "  %s: stored %u bytes in %s\n", what, (unsigned)conn->image_len, path);

    //The event carries the same unix timestamp that heads the record, which is what the web
    //side matches on to offer the picture - it needs no offset into the file and so cannot
    //be invalidated by the file being truncated or rotated underneath it.
    char message[128] = {0};
    snprintf(message, sizeof(message), "%s:%ld", audio ? "audio" : "photo", (long)now);
    log_event(conn, message);
    //A picture is nearly always the answer to a command the user sent, so it belongs in the
    //command results next to the command that asked for it - otherwise the only sign it
    //worked is a marker somewhere on the map. Same "photo:<ts>" form, so the web side needs
    //one rule for both lists.
    snprintf(message, sizeof(message), "%s received %s:%ld (%u bytes)",
             audio ? "recording" : "picture", audio ? "audio" : "photo",
             (long)now, (unsigned)conn->image_len);
    log_command_response(conn, message);
    image_discard(conn);
    return true;
}

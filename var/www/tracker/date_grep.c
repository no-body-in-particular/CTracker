#include <stdio.h>
#include <stddef.h>
#include <time.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define BUF_SIZE 8192

/**
 * parses a javascript ISO datestamp using a simple and efficient regex
 */
long long parse_date(const char * dt) {
    struct tm tmVar = {0};
    unsigned long long timeVar = 0;

    if (sscanf(dt, "%d-%d-%dT%d:%d:%dZ", &tmVar.tm_year, &tmVar.tm_mon, & tmVar.tm_mday, &tmVar.tm_hour, &tmVar.tm_min, &tmVar.tm_sec) == 6) {
        tmVar.tm_year -= 1900;
        tmVar.tm_mon -= 1;
        timeVar=timegm(&tmVar);
       //timeVar = mktime(&tmVar);
        return timeVar;

    } else {
        return -1;
    }
}

/**
 * reads a date from offset forwards. this function will skip the first line at offset, unless offset is 0
 */
long long read_date_forwards(FILE * fp,  size_t offset) {
    char buf[BUF_SIZE + 1];
    memset(buf, 0, BUF_SIZE + 1);
    fseek(fp, offset, SEEK_SET);//skip to the position where we have to read our date from
    size_t read = fread(buf, 1, BUF_SIZE, fp);

    for (size_t i = 0; i < read; i++) {
        if (buf[i] == '\n') {//skip the current line, unless our offset is 0
            size_t n = i + 1;
            buf[i] = 0;

            for (; n < read && buf[n] != '\n'; n++);

            buf[n] = 0;
            return parse_date(buf + (offset == 0 ? 0 : i + 1));//parse the resulting date
        }
    }

    return -1;
}

/**
 * Tries to binary search for where the unix timestamp given by dt occurs in the file as an ISO date
 */
unsigned long long binary_seek(FILE * fp, size_t beg, size_t end, unsigned long long dt) {
    unsigned long long med = beg + ((end - beg) / 2);

    if (med == beg) {
        return med;
    }

    //basically compute the middle point between begin and end
    long long first_date = read_date_forwards(fp, med);

    //if our date is greater, we need to search between begin and the middle
    if (first_date > dt || first_date < 0) {
        return binary_seek(fp, beg, med, dt);
    }

    //otherwise between middle and the end
    if (first_date < dt) {
        return binary_seek(fp, med, end, dt);
    }

    return med;
}

/**
 * Method that figures out where to start reading our file, given that the date we want to start at is date
 */
size_t find_start(FILE * fp, unsigned long long date) {
    size_t total_size = 0;
    char buf[BUF_SIZE + 1]; //initialize an empty buffer
    memset(buf, 0, BUF_SIZE + 1);
    fseek(fp, 0, SEEK_END); // seek to end of file
    total_size = ftell(fp); // get current file pointer
    fseek(fp, 0, SEEK_SET); // seek back to beginning of file
    //find the correct start offset for this date
    size_t offs = binary_seek(fp, 0, total_size, date);
    fseek(fp, offs, SEEK_SET);

    //read until we're sure we're on the clean next line
    if (offs != 0) {
        size_t read = fread(buf, 1, BUF_SIZE, fp);
        size_t i = 0;

        for (; i < read && buf[i] != '\n'; i++);

        offs += i + 1;
    }

    fseek(fp, offs, SEEK_SET);
    return offs;
}

int main(int argc, char * argv[]) {
    FILE * fp;
    char buf[BUF_SIZE + 1];
    size_t buf_idx = 0;
    unsigned long long startdate = strtoull(argv[2], 0, 0);
    unsigned long long  enddate = argc > 3 ? strtoull(argv[3], 0, 0) : 0;

    if (argc < 3) {
        fprintf(stdout, "Syntax: dategrep [file] [start unix ts] [end unix ts]. minimum 2 arguments. \n");
        return -1;
    }

    fp = fopen(argv[1], "r");

    if (fp <= 0) {
        return -1;    //if we cannot open our file, there's nothing to do.
    }

    find_start(fp, startdate);
    memset(buf, 0, BUF_SIZE + 1);

    for (;;) {
        size_t rd = fread(buf + buf_idx, 1, BUF_SIZE - buf_idx, fp); //read into our buffer
        buf_idx += rd;

        for (;;) {
            int line_begin_idx = 0;

            for (; !isdigit(buf[line_begin_idx]) && line_begin_idx < buf_idx; line_begin_idx++); //find the first character that is not a newline

            //if we're at the end of the buffer, read a new set of data
            if (line_begin_idx >= buf_idx) {
                break;
            }

            unsigned long long dt = parse_date(buf + line_begin_idx); //then parse the date at the beginning of this line
            int line_end_idx = line_begin_idx;

            for (; line_end_idx < buf_idx && buf[line_end_idx] != '\n' && buf[line_end_idx] != '\r'; line_end_idx++); //and walk over the rest of the line

            if (line_end_idx >= buf_idx) {
                break;//if we cannot find a line ending - our buffer does not contain the entire line
            }

            buf[line_end_idx] = 0;//make sure we end our line with a null character for printing

            if ( dt > enddate && enddate != 0) { //if this sample has a date greater than our end date, we're done
                fclose(fp);
                return 0;
            }

            if ( startdate < dt ) {//if not: print our sample
                fprintf(stdout, "%s\n", buf + line_begin_idx);
            }

            //move the part of our buffer that we have to read back
            memcpy(buf, buf + line_end_idx + 1, BUF_SIZE - line_end_idx - 1);
            buf_idx -= (line_end_idx + 1 );

            if (rd == 0 && buf_idx == 0) {//if we've exhausted our buffer, fetch a new one
                break;
            }
        }

        if (feof(fp)) {//if we're at the end of our file we're done
            fclose(fp);
            return 0;
        }
    }

    return 0;
}

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include <sys/select.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "../util.c"

size_t build_xex_packet(uint8_t * buffer, size_t len) {
    //0xfa, 0xaf, bericht 0xfa, 0xaf,
    if (len < 4) {
        len = 4;
    }

    buffer[0] = 0xfa;
    buffer[1] = 0xaf;

    for (size_t i = 2; i < (len - 2); i++) {
        buffer[i] = rand();
    }

    buffer[len - 2] = 0xfa;
    buffer[len - 1] = 0xaf;
    return len;
}

size_t build_jimi_packet(uint8_t * buffer, size_t len) {
    //0x78, 0x78, bericht 0x0D, 0x0A
    if (len < 4) {
        len = 4;
    }

    buffer[0] = 0x78;
    buffer[1] = 0x78;

    for (size_t i = 2; i < (len - 2); i++) {
        buffer[i] = rand();
    }

    buffer[len - 2] = 0x0D;
    buffer[len - 1] = 0x0A;
    return len;
    return 0;
}


static char * rand_string(char * str, size_t size) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    if (size) {
        --size;

        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }

        str[size] = '\0';
    }

    return str;
}

size_t build_megastek_packet(uint8_t * buffer, size_t max_len) {
    size_t cnt = rand() % 10 + 35;
    uint8_t buf[128] = {0};
    buffer[0] = 0;
    strcat(buffer, "0158$MGV002");

    for (size_t n = 0; n < cnt ; n++) {
        strcat(buffer, ",");
        rand_string(buf, rand() % 127);
        strcat(buffer, buf);
    }

    return strlen(buffer);
}
//sets the non-blocking flag for an IO descriptor
void set_nonblock(int fd) {
    //retrieve all the flags for this file descriptor
    int fl = fcntl(fd, F_GETFL, 0);

    if (fl < 0) {
        fprintf(stderr, "Failed to get flags for file descriptor %d: %s\n", fd, strerror(errno));
        return;
    }

    //add the non-blocking flag to this file descriptor's flags
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) {
        fprintf(stderr, "Failed to set flags for file descriptor %d: %s\n", fd, strerror(errno));
        return;
    }

    int optval = 1;
    socklen_t optlen = sizeof(optval);
    optlen = sizeof(optval);

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
        fprintf(stderr, "Failed to set keepalive\n");
    }

    fprintf(stdout, "SO_KEEPALIVE set on socket\n");
}
int main(int argc, char * argv[]) {
    unsigned char message[1000] ;
    unsigned char server_reply[2000];
    srand(time(NULL));

    for (;;) {
        int sock = 0;
        struct sockaddr_in server = {0};
        //Create socket
        sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock == -1) {
            printf("Could not create socket");
        }

        puts("Socket created");
        server.sin_addr.s_addr = inet_addr("127.0.0.1");
        server.sin_family = AF_INET;
        server.sin_port = htons( 9000 );

        //Connect to remote server
        if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
            perror("connect failed. Error");

        } else {
            set_nonblock(sock);
        }

        puts("Connected\n");

        for (; sock > 0;) {
            //Send some data
            uint8_t sendbuf[4096];
            fprintf(stdout, "sending data: ");
            size_t len = build_megastek_packet(sendbuf, 4092);

            for (int n = 0; n < len; n++) {
                fprintf(stdout, "%x ", sendbuf[n]);
            }

            fprintf(stdout, "\n");

            if ( send(sock, sendbuf, len, 0) < 0 && errno != EWOULDBLOCK) {
                fprintf(stdout, "send failed for message\n");
                sock = 0;
            }

            msleep(50);
        }

        close(sock);
        msleep(5000);
    }

    return 0;
}


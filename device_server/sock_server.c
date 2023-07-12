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
#include <signal.h>

#include "sock_server.h"
#include "connection.h"
#include "config.h"
#include "device/jimi_protocol.h"
#include "device/megastek_protocol.h"
#include "device/XEXUN_protocol.h"
#include "device/thinkrace_protocol.h"
#include "device/myrope_r18_protocol.h"
#include "device/myrope_protocol.h"
#include "device/basic_protocol.h"

#include "geofence.h"
#include "commands.h"
#include "logfiles.h"
#include "events.h"
#include "util.h"
#include <pthread.h>

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

int create_server_sock(char * addr, int port) {
    int on = 1;
    static struct sockaddr_in client_addr;
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        fprintf(stderr, "Failed to create server socket.\n");
        return -1;
    }

    memset(&client_addr, '\0', sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = inet_addr(addr);
    client_addr.sin_port = htons(port);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, 4) < 0) {
        fprintf(stderr, "Failed to set socket address %s:%d\n", addr, port);
        return -1;
    }

    if (bind(sock, (struct sockaddr *) &client_addr, sizeof(client_addr)) < 0) {
        fprintf(stderr, "Failed to bind server socket on %s:%d\n", addr, port);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        fprintf(stderr, "Failed to listen on %s:%d\n", addr, port);
        return -1;
    }

    fprintf(stdout, "Listening on %s port %d\n", addr, port);
    return sock;
}

int wait_for_client(int s) {
    struct sockaddr_in peer;
    socklen_t len = sizeof(struct sockaddr);
    fprintf(stdout, "Accepting connections with file descriptor: %d\n", s);
    int newsock = accept(s, (struct sockaddr *) &peer, &len);

    if (newsock < 0 && errno != EINTR) {
        fprintf(stdout, "Failed to accept connection with file descriptor %d: %s\n", s, strerror(errno));
        return -1;
    }

    struct hostent * hostinfo = gethostbyaddr((char *) &peer.sin_addr.s_addr, len, AF_INET);

    fprintf(stdout, "Incoming connection accepted from %s[%s]\n", !hostinfo ? "" : hostinfo->h_name, inet_ntoa(peer.sin_addr));

    set_nonblock(newsock);

    return (newsock);
}


void determine_device(connection * conn) {
    if (conn->read_count < 12) {
        return;
    }

    JIMI_identify(conn);
    megastek_identify(conn);
    XEXUN_identify(conn);
    thinkrace_identify(conn);
    myrope_r18_identify(conn);
    myrope_identify(conn);
    basic_identify(conn);
}

void * process_thread(void * int_ptr) {
    connection conn = new_connection(int_ptr);
    struct timeval to = { 0, 100 };
    time_t since_packet = time(0);

    for (;;) {
        //if we've read data from our cmmand we need to send it
        if (conn.send_count) {
            int sent = write(conn.socket, conn.send_buffer, conn.send_count);

            //if an error happens during sending - the client probably gave up
            if (sent < 0 && errno != EWOULDBLOCK) {
                fprintf(stdout, "client disconnected: %s\n", strerror(errno));
                pthread_exit(0);
            }

            //reduce our buffer by the amount of data sent
            if (sent != conn.send_count) {
                memmove(conn.send_buffer, conn.send_buffer + sent, conn.send_count - sent);
            }

            conn.send_count -= sent;
        }

        msleep(GRACE_TIME);
        //use select to wait until anything happens to our source file.
        int rdCount = read(conn.socket, conn.recv_buffer + conn.read_count, BUF_SIZE - conn.read_count);	//read potential input from the client - and discard it.

        //if our program closed - end the session
        if (rdCount < 0 && errno != EWOULDBLOCK) {
            fprintf(stdout, "socket closed : %s\n", strerror(errno));
            pthread_exit(0);
        }

        //add the amount of data to the buffer that we have to send
        if (rdCount > 0) {
            conn.read_count += rdCount;
            since_packet = time(0);
        }

        conn.iteration++;

        //for xexun devices: never send/recieve in the same iteration.
        if (conn.PROCESS_FUNCTION > 0 && conn.send_count == 0) {
            //wait until first 12 bytes recieved
            //then determine device from those 12 bytes.
            //depending on this set the commands to use for a warning + the function pointers
            if (  conn. iteration % 5 == 0 ) {
                process_command_file(&conn);
            }

            conn.PROCESS_FUNCTION(&conn);

            if (conn.iteration % 100 == 0) {
                read_geofence(&conn);
                read_disabled_alarms(&conn);
            }

            if (conn.iteration % 1000 == 0) {
                conn.command_response_filehandle =  log_truncate(conn.command_response_filehandle, conn.command_response_outfile, MAX_LOG_SIZE);
                conn.gps_filehandle =  log_truncate(conn.gps_filehandle, conn.gps_outfile, MAX_DATA_SIZE);
                conn.log_filehandle =  log_truncate(conn.log_filehandle, conn.log_outfile, MAX_LOG_SIZE);
                conn.stats_filehandle =    log_truncate(conn.stats_filehandle, conn.stats_file, MAX_DATA_SIZE);
                read_geofence(&conn);
                read_disabled_alarms(&conn);
            }

            if ( time(0) > conn.timeout_time ) {
                if (conn.log_disconnect) {
                    log_event(&conn, "device disconnected");
                }

                fprintf(stdout, "client disconnected\n");
                close(conn.socket);
                close_connection(&conn);
                pthread_exit(0);
            }

        } else {
            determine_device(&conn);

            //test for disconnected client every 5 minutes then end the process
            if (time(0) - since_packet > 20) {
                fprintf(stdout, "client timed out\n");
                close(conn.socket);
                close_connection(&conn);
                pthread_exit(0);
            }
        }
    }
}

void run_server() {
    int client = -1;
    int server_socket = -1;

    while (server_socket <= 0) {
        server_socket = create_server_sock(LISTEN_ON, LISTEN_PORT);

        if (server_socket <= 0) {
            msleep(10000);
        }
    }

    //will not wait for child processes
    signal(SIGCHLD, SIG_IGN);
    pthread_t thread_id;

    for (;;) {
        if ((client = wait_for_client(server_socket)) < 0) {
            continue;
        }

        if ( client > 0 && ( pthread_create( &thread_id, NULL,  process_thread, (void *) client) < 0)) {
            fprintf(stderr, "Failed to create thread.\n");
        }
    }
}

#include <stdio.h>
#include <stdint.h>
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
#include "tracking.h"
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

    //nothing acts on this and it is printed for every connection accepted, the vast
    //majority of which are internet background noise on an open port
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

    //run_server retries this in a loop, and each failed attempt used to abandon its
    //descriptor - so a port that stayed busy leaked one fd every ten seconds until the
    //process ran out entirely.
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        fprintf(stderr, "Failed to set socket address %s:%d\n", addr, port);
        close(sock);
        return -1;
    }

    if (bind(sock, (struct sockaddr *) &client_addr, sizeof(client_addr)) < 0) {
        fprintf(stderr, "Failed to bind server socket on %s:%d\n", addr, port);
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        fprintf(stderr, "Failed to listen on %s:%d\n", addr, port);
        close(sock);
        return -1;
    }

    fprintf(stdout, "Listening on %s port %d\n", addr, port);
    return sock;
}

int wait_for_client(int s) {
    struct sockaddr_in peer;
    socklen_t len = sizeof(struct sockaddr);
    //likewise: the listening descriptor does not change, so printing it per accept says
    //nothing and buries the lines that do
    int newsock = accept(s, (struct sockaddr *) &peer, &len);

    if (newsock < 0) {
        //EINTR used to fall through to set_nonblock() and the logging below with an fd of -1
        if (errno != EINTR) {
            fprintf(stdout, "Failed to accept connection with file descriptor %d: %s\n", s, strerror(errno));
        }

        return -1;
    }

    //len is sizeof(struct sockaddr) - 16 bytes - but the address handed over is the four
    //byte s_addr, so this read twelve bytes past it. It is also a blocking reverse lookup
    //run inside the accept loop, which stalls every new connection behind a DNS round trip,
    //and gethostbyaddr is not thread safe. The peer address alone is what the log needs.
    //Only interesting once the peer turns out to be a device. Roughly nine in ten
    //connections on this port never send a recognisable byte - they are scanners - and
    //logging each one drowns the real traffic. The address is printed by determine_device
    //instead, once something has identified itself.
    (void)peer;
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
    //nothing ever joins these threads. Left joinable, every finished connection kept its
    //whole stack - and struct connection is a few hundred KB of buffers and geofence slots
    //- reserved until the process exited. Detaching lets it be reclaimed at pthread_exit.
    pthread_detach(pthread_self());
    //round tripping the fd through void * as a plain int is undefined on LP64; intptr_t is
    //the type that is actually guaranteed to survive the trip
    connection conn = new_connection((int)(intptr_t)int_ptr);
    time_t since_packet = time(0);
    bool buffer_stalled = false;

    for (;;) {
        //if we've read data from our cmmand we need to send it
        if (conn.send_count) {
            int sent = write(conn.socket, conn.send_buffer, conn.send_count);

            //if an error happens during sending - the client probably gave up
            if (sent < 0 && errno != EWOULDBLOCK) {
                fprintf(stdout, "client disconnected: %s\n", strerror(errno));
                //this path used to leave the socket and every log FILE * open
                close(conn.socket);
                close_connection(&conn);
                pthread_exit(0);
            }

            //a socket that is not ready to accept more returns -1 with EWOULDBLOCK, which
            //the checks above deliberately do not treat as fatal. it must not be treated as
            //a byte count either: memmove would walk one byte back off the front of the
            //buffer and send_count would grow rather than shrink, so the buffer could never
            //drain. the loop then sat permanently in the branch below that re-runs
            //determine_device, re-identifying the device and sending it another SYNCTIME
            //every pass - thousands of them - while never processing anything it sent.
            if (sent > 0) {
                //reduce our buffer by the amount of data sent
                if (sent != conn.send_count) {
                    memmove(conn.send_buffer, conn.send_buffer + sent, conn.send_count - sent);
                }

                conn.send_count -= sent;
            }
        }

        msleep(GRACE_TIME);
        //use select to wait until anything happens to our source file.
        int rdCount = read(conn.socket, conn.recv_buffer + conn.read_count, BUF_SIZE - conn.read_count);	//read potential input from the client - and discard it.

        //if our program closed - end the session
        if (rdCount < 0 && errno != EWOULDBLOCK) {
            fprintf(stdout, "socket closed : %s\n", strerror(errno));
            close(conn.socket);
            close_connection(&conn);
            pthread_exit(0);
        }

        //read() returning 0 on a non-empty request is the peer closing cleanly. Neither
        //branch used to match it, so a device that hung up politely left its thread and
        //socket sitting here until the inactivity timeout eventually noticed.
        if (rdCount == 0 && conn.read_count < BUF_SIZE) {
            if (conn.PROCESS_FUNCTION != 0) {
                fprintf(stdout, "client closed the connection\n");
            }

            if (conn.log_disconnect && is_command_owner(&conn)) {
                log_event(&conn, "device disconnected");
            }

            close(conn.socket);
            close_connection(&conn);
            pthread_exit(0);
        }

        //A full buffer leaves read() with a size of zero, so it returns 0 for ever and the
        //loop spins at full tilt. The parser below drains the buffer in the normal case, so
        //only give up once it has had a pass at it and the buffer is still full - dropping
        //on the first sight of a full buffer would kill devices whose packet merely happens
        //to fill it.
        if (conn.read_count >= BUF_SIZE) {
            if (buffer_stalled) {
                fprintf(stdout, "receive buffer full and not draining, dropping client\n");
                close(conn.socket);
                close_connection(&conn);
                pthread_exit(0);
            }

            buffer_stalled = true;

        } else {
            buffer_stalled = false;
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
                //only the newest connection for this device sends anything. a device
                //routinely leaves an older connection half open, and writes to it are
                //silently lost - so a queued command could be consumed from the file and
                //thrown into a dead socket, never reaching the device at all.
                if (is_command_owner(&conn)) {
                    process_command_file(&conn);
                    poll_health(&conn);
                    update_tracking_interval(&conn);
                }
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
                //A device commonly holds several sockets open at once and lets the older
                //ones lapse; those closing say nothing about the device being reachable, and
                //logging each one is what filled the event file. Only the connection that
                //currently owns the device is allowed to report it gone.
                if (conn.log_disconnect && is_command_owner(&conn)) {
                    log_event(&conn, "device disconnected");
                }

                fprintf(stdout, "device %s disconnected\n", conn.imei[0] ? (char *)conn.imei : "(unidentified)");
                close(conn.socket);
                close_connection(&conn);
                pthread_exit(0);
            }

        } else {
            //only worth doing while the device is still unknown. once a protocol has claimed
            //it, re-running this just re-sends the identify handshake.
            if (conn.PROCESS_FUNCTION == 0) {
                determine_device(&conn);
            }

            //test for disconnected client every 5 minutes then end the process
            if (time(0) - since_packet > 20) {
                //a connection that never identified is a scanner nine times out of ten;
                //saying so once per scan is what made the log unreadable
                if (conn.PROCESS_FUNCTION != 0) {
                    fprintf(stdout, "client timed out\n");
                }
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

        if ( client > 0 && ( pthread_create(&thread_id, NULL,  process_thread, (void *)(intptr_t) client) < 0)) {
            fprintf(stderr, "Failed to create thread.\n");
            //the descriptor is ours again if no thread took it
            close(client);
        }
    }
}

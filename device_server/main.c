#include "sock_server.h"
#include <stdio.h>
#include <curl/curl.h>
#include "lbs_lookup.h"
#include "wifi_lookup.h"
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

void handler(int sig) {
    void * array[10];
    size_t size;
    // get void*'s for all entries on the stack
    size = backtrace(array, 10);
    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

/*
 * Wait a little before replacing a worker that died immediately.
 *
 * Without this the loop replaces a worker as fast as the machine can fork one, which is fine
 * when the worker ran for a while and hit something unlucky, and very much not fine when it
 * dies on startup every time: a cache that would not fit in memory produced 2544 segfaults in
 * the six seconds it took to measure them, each one a fork, a gigabyte of allocation attempts
 * and ten lines of backtrace. The condition lasts as long as it lasts either way; the
 * difference is whether the machine spends that time pinned and the log unreadable.
 *
 * Only a worker that failed to stay up is held back, and the wait is capped, so a server that
 * has been running for a week and crashes once still comes straight back.
 */
#define RESPAWN_MIN_LIFETIME 10         //seconds a worker must last to count as having started
#define RESPAWN_BACKOFF_MAX 30          //longest we will wait before trying again

void server_loop() {
    unsigned int backoff = 0;

    while (true) {
        pid_t childPid;  // the child process that the execution will soon run inside of.
        time_t started = time(0);
        childPid = fork();

        if (childPid == 0) { // fork succeeded
            init_lbs();
            init_wifi();
            run_server();
            //run_server does not return; if it ever does, do not fall into the parent's path
            _exit(0);

        } else if (childPid < 0) {
            fprintf(stderr, "Failed to fork main thread.\n");
            sleep(1);

        } else { // Main (parent) process after fork succeeds
            int returnStatus;
            waitpid(childPid, &returnStatus, 0);  // Parent process waits here for child to terminate.
            time_t lived = time(0) - started;

            if (lived >= RESPAWN_MIN_LIFETIME) {
                backoff = 0;

            } else {
                backoff = backoff ? backoff * 2 : 1;

                if (backoff > RESPAWN_BACKOFF_MAX) {
                    backoff = RESPAWN_BACKOFF_MAX;
                }

                fprintf(stderr, "worker lasted %ld seconds, waiting %u before starting another\n",
                        (long)lived, backoff);
                sleep(backoff);
            }
        }
    }
}

int main(int argc, char * argv[]) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, handler);
    signal(SIGFPE, handler);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    setbuf(stdout, NULL);
    tzset();
    server_loop();
    curl_global_cleanup();
    return 0;
}

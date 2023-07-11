#include "sock_server.h"
#include <stdio.h>
#include <curl/curl.h>
#include "lbs_lookup.h"
#include "wifi_lookup.h"
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
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

void server_loop() {
    while (true) {
        pid_t childPid;  // the child process that the execution will soon run inside of.
        childPid = fork();

        if (childPid == 0) { // fork succeeded
            init_lbs();
            init_wifi();
            run_server();

        } else if (childPid < 0) {
            fprintf(stderr, "Failed to fork main thread.\n");

        } else { // Main (parent) process after fork succeeds
            int returnStatus;
            waitpid(childPid, &returnStatus, 0);  // Parent process waits here for child to terminate.
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

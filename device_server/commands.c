
#include "commands.h"
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include "string.h"

void add_command(connection * conn, char * command) {
    if (!conn->can_log) {
        return;
    }

    FILE * fp = fopen(conn->command_infile, "a");

    if (fp <= 0) {
        fprintf(stderr, "Failed to add command %s for device with IMEI: %s\n", command, conn->imei);
        return;
    }

    /* Write content to file */
    fprintf(fp, "%s\n", command);
    fclose(fp);
}


void process_command_file(connection * conn) {
    char buffer[BUF_SIZE];
    char remaining_commands[BUF_SIZE];

    if (!conn->can_log) {
        return;
    }

    FILE * fp = fopen(conn->command_infile, "r");

    //if there's no commands file, well there is nothing to do
    if (fp <= 0) {
        fp = fopen(conn->command_infile, "w");

        if (fp == 0) {
            fprintf(stdout, "failed to create:%s\n", conn->command_infile);
            return;
        }

        fclose(fp);
        fp = fopen(conn->command_infile, "r");
    }

    if (fp <= 0) {
        fprintf(stdout, "failed to open:%s\n", conn->command_infile);
        return;
    }

    fseek(fp, 0L, SEEK_END);

    if (ftell(fp) < 2) {
        fclose(fp);
        return;
    }

    fseek(fp, 0L, SEEK_SET);
    memset(buffer, 0, BUF_SIZE);
    memset(remaining_commands, 0, BUF_SIZE);
    bool sent_command = false;

    //read the file line by line and send our commands
    while (fgets(buffer, BUF_SIZE - 1, fp)) {
        if (strlen(buffer) > 2) {
            if (!sent_command) {
                if (strncmp(buffer, "WARNONCE#", 9) == 0) {
                    if (conn->WARNING_FUNCTION) {
                        conn->WARNING_FUNCTION(conn, "single warning command");
                    }

                } else if (strncmp(buffer, "WAIT#", 5) == 0) {
                    sleep(8);

                } else if (strncmp(buffer, "WARNAUDIO#", 10) == 0) {
                    if ( conn->AUDIO_WARNING_FUNCTION) {
                        conn->AUDIO_WARNING_FUNCTION(conn, "audio warning command");
                    }

                } else if (strncmp(buffer, "WARNMOTOR#", 10) == 0) {
                    if (conn->MOTOR_WARNING_FUNCTION) {
                        conn->MOTOR_WARNING_FUNCTION(conn, "vibrate alarm command");
                    }

                } else {
                    strip_whitespace(buffer);

                    if (conn->COMMAND_FUNCTION > 0 && conn->COMMAND_FUNCTION(conn, buffer)) {
                        fprintf(stdout, "sent command:%s\n", buffer);

                    } else {
                        strcat(buffer, "\n");
                        strcat(remaining_commands, buffer);
                    }
                }

                sent_command = true;

            } else {
                if (strlen(remaining_commands) + strlen(buffer) < BUF_SIZE) {
                    strcat(remaining_commands, buffer);
                }
            }
        }
    }

    //clear our commands file
    freopen(NULL, "w", fp);
    //add the remaining commands
    fwrite(remaining_commands, 1, strlen(remaining_commands), fp);
    fclose(fp);
}

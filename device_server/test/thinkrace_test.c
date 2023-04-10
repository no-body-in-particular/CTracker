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
#include "../device/thinkrace_protocol.h"
#include "../connection.h"

int main(int argc, char * argv[]) {
    const uint8_t bytes[] = "IWAP00357653050859797,8931262004022036837f,204080589033692#IWAPTQ,1,204,08,6070|19561258|44,0#";
    const uint8_t bytes1[] = "IWAPVR,357653050859797,C42F-l005l005-EU-P1-V0.3.45.20211209.115039#";
    const uint8_t bytes2[] = "IWAP01211225A1234.0116N00321.3506E000.5212100000.0004700009900008,204,08,6070,19561258,AP1|b0:b9:ba:51:8e:a0|-24&AP2|96:6a:ba:0a:0a:72|-55&AP3|b0:00:00:61:4f:bb|-65&AP4|00:00:00:44:ab:ae|-66&AP5|00:00:00:4a:70:09|-74#IWAP10211226A1234.0159N00123.3525E000.7142905000.0004600008500008,204,08,6070,19561258,16,en,00,#IWAP01211226A5232.0099N00541.3518E001.8151246000.0004300009500008,204,08,6070,19561258,AP1|00:00:00:00:8e:ac|-22&AP2|00:00:00:00:ab:ae|-53&AP3|00:00:00:00:2a:72|-59&AP4|00:00:00:61:4f:bb|-65&AP5|00:00:00:00:2a:e4|-79#IWAPHT,70,117,73#I";
    connection connection = new_connection(0);
    memcpy(connection.recv_buffer, bytes, strlen(bytes));
    memcpy(connection.recv_buffer + strlen(bytes), bytes1, strlen(bytes1));
    memcpy(connection.recv_buffer + strlen(bytes) + strlen(bytes1), bytes2, strlen(bytes2));
    connection.read_count = strlen(bytes) + strlen(bytes1) + strlen(bytes2);
    thinkrace_identify(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    thinkrace_process(&connection);
    return 0;
}


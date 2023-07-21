

#ifndef CONNECTION_H__
#define CONNECTION_H__

#include <stdio.h>
#include <stdbool.h>
#include "config.h"
#include <time.h>
#include <stdint.h>

typedef struct {
    short start_hour;
    short start_minute;
    short end_hour;
    short end_minute;
    uint8_t day_of_week;
    time_t fence_start_today;//to be filled by time conversion routines
    time_t fence_end_today;
    uint8_t type;//in,out,in+out,outside
    float lat;
    float lon;
    float radius;
    bool warn_enable;
    char name[32];
    bool valid;
} geofence;


typedef struct {
    unsigned char send_buffer[BUF_SIZE];
    unsigned char recv_buffer[BUF_SIZE];
    unsigned char gps_outfile[FILENAME_MAX];
    unsigned char log_outfile[FILENAME_MAX];
    unsigned char event_outfile[FILENAME_MAX];
    unsigned char geofence_file[FILENAME_MAX];
    unsigned char command_response_outfile[FILENAME_MAX];
    unsigned char current_status_file[FILENAME_MAX];
    unsigned char stats_file[FILENAME_MAX];
    unsigned char command_infile[FILENAME_MAX];
    unsigned char disabled_alarms_infile[FILENAME_MAX];
    unsigned char current_packet[BUF_SIZE];
    unsigned char disabled_alarms[BUF_SIZE];
    unsigned char previous_command_packet[BUF_SIZE];
    unsigned char previous_packet[BUF_SIZE];
    FILE * log_filehandle;
    FILE * gps_filehandle;
    FILE * event_filehandle;
    FILE * command_response_filehandle;
    FILE * stats_filehandle;
    bool current_packet_valid;
    size_t send_count ;
    size_t read_count;
    size_t since_last_status;
    size_t timeout_time;
    size_t since_battalm;
    size_t iteration;
    bool just_connected;
    bool can_log;
    bool log_disconnect;
    bool log_connect;
    char imei[17];
    int socket;
    geofence fence_list[MAX_FENCE];
    size_t fence_count;
    size_t packet_index;
    float current_lat;
    float current_lon;
    float current_speed;
    float last_gps_lat;
    float last_gps_lon;
    size_t current_position_type;
    size_t current_sat_count;
    time_t device_time;
    time_t since_last_locate;
    const char * SINGLE_WARNING;
    unsigned int device_extra;
    void (*PROCESS_FUNCTION)(void *) ;
    void (*AUDIO_WARNING_FUNCTION)(void *, const char * );
    void (*MOTOR_WARNING_FUNCTION)(void *, const char * );
    void (*WARNING_FUNCTION)(void *, const char * );
    bool (*COMMAND_FUNCTION)(void *, const char *);
} connection;


connection new_connection(int socket);

void close_connection(connection * conn);
void init_imei(connection * conn);
void send_string(connection * conn, char * str);

#endif // CONNECTION_H_INCLUDED

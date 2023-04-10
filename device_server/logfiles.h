#ifndef LOGFILES_H_INCLUDED
#define LOGFILES_H_INCLUDED
#include "connection.h"
void logvfprintf( connection * conn, const char * format, va_list arglist) ;
void gpsvfprintf( connection * conn, const char * format, va_list arglist ) ;
void eventvfprintf( connection * conn, const char * format, va_list arglist ) ;
void logprintf( connection * conn, const char * format, ... );
void gpsprintf( connection * conn, const char * format, ... );
void eventprintf( connection * conn, const char * format, ... ) ;
void statusvfprintf(connection * conn, const char * format, va_list args) ;
void statusprintf(connection * conn, const char * format, ...) ;
void log_command_response(connection * conn, const unsigned char * response) ;
void log_time(connection * conn);
void log_line( connection * conn, const char * format, ... );
void log_array(connection * conn, uint8_t * array, size_t len);
void log_buffer(connection * conn);
void log_event(connection * conn, float lat, float lon, float speed, const unsigned char * response);
void write_stat(connection * conn, time_t timestamp, char * value_name, float value);
void statsvfprintf( connection * conn, const char * format, va_list arglist);
void statsprintf( connection * conn, const char * format, ... ) ;
FILE * log_truncate( FILE * fp, char * name, size_t max_size);
#endif // LOGFILES_H_INCLUDED

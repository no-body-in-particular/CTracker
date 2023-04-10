#ifndef COMMANDS_H_INCLUDED
#define COMMANDS_H_INCLUDED

#include "connection.h"

void process_command_file(connection * conn) ;
void add_command(connection * conn, char * command) ;

#endif // COMMANDS_H_INCLUDED

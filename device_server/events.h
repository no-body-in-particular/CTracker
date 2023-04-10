#ifndef _EVENTS_H_INCLUDED_
#define _EVENTS_H_INCLUDED_

#include "connection.h"
void read_disabled_alarms(connection * conn);
bool is_alarm_disabled(connection * conn, const char * evt);

#endif // EVENTS_H_INCLUDED

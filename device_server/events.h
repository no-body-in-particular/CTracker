#ifndef _EVENTS_H_INCLUDED_
#define _EVENTS_H_INCLUDED_

#include "connection.h"
void read_disabled_alarms(connection * conn);
bool is_alarm_disabled(connection * conn, const char * evt);
void read_disabled_fences(connection * conn);
bool is_fence_folder_disabled(connection * conn, const char * folder);

#endif // EVENTS_H_INCLUDED

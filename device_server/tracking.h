#ifndef _TRACKING_H_
#define _TRACKING_H_

#include "connection.h"

void claim_command_ownership(connection * conn);
bool is_command_owner(connection * conn);
void note_health(connection * conn);
void note_heartrate(connection * conn, int bpm);
void note_movement(connection * conn, double speed_kmh, bool speed_known);
void note_device_interval(connection * conn, unsigned int seconds);
void poll_health(connection * conn);
void update_tracking_interval(connection * conn);

#endif

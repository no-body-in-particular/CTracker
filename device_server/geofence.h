#ifndef GEOFENCE_H_INCLUDED
#define GEOFENCE_H_INCLUDED

#define FENCE_IN 0
#define FENCE_OUT 1
#define FENCE_IN_OUT 2
#define FENCE_STAY 3
#define FENCE_EXCLUDE 4

#include "connection.h"
void read_geofence(connection * conn) ;
void move_to(connection * conn, time_t device_time, int position_type, double lat, double lon);


#endif // GEOFENCE_H_INCLUDED

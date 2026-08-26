#ifndef _IMAGES_H_INCLUDED_
#define _IMAGES_H_INCLUDED_

#include "connection.h"
#include <stdbool.h>
#include <stddef.h>

//Start a new image. Any half assembled image already in progress is discarded - a device
//that abandons an upload part way through simply starts the next one, and there is no
//packet that says "cancel".
bool image_begin(connection * conn, const char * device_time, size_t total_packets);

//Append one received packet. Returns false if the packet does not belong to the image
//currently being assembled or it would grow past MAX_IMAGE_SIZE.
bool image_append(connection * conn, size_t packet_no, const unsigned char * data, size_t len);

//True once every packet the header promised has arrived.
bool image_complete(connection * conn);

//Write the assembled image into <imei>.images.db and log an event for it. Frees the
//assembly buffer either way.
bool image_store(connection * conn);

//Drop a partial image and free its buffer.
void image_discard(connection * conn);

#endif // _IMAGES_H_INCLUDED_

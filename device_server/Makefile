TARGETS=jimitrack
CFLAGS+=-L/usr/local/opt/curl/lib
CFLAGS+=-I/usr/local/opt/curl/include
#CFLAGS += -O2 -lm
CFLAGS+= -g -lm -std=gnu99 -lcurl -lpthread -rdynamic

all: $(TARGETS)

jimitrack: main.o util.o multilaterate.o wifi_lookup.c search.c sort.c lbs_lookup.c web_geolocate.o crc16.o sock_server.o connection.o device/jimi_protocol.o device/jimi_packet.o string.o device/jimi_util.o commands.o events.o geofence.o logfiles.o device/megastek_protocol.o device/XEXUN_protocol.o device/thinkrace_protocol.o crc64.o device/myrope_protocol.o device/myrope_r18_protocol.c device/basic_protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

install: clean jimitrack
	cp jimitrack /bin/

clean:  
	find . -name "*.o" -delete
	rm -f jimitrack
	echo "clean"

style:
	astyle --style=java --break-blocks=all --pad-oper --pad-oper --pad-comma --pad-header --indent-switches --delete-empty-lines -k2 -xf --add-braces -Q  -n --recursive "*.c" "*.h"

TARGETS=jimitrack
CFLAGS+=-L/usr/local/opt/curl/lib
CFLAGS+=-I/usr/local/opt/curl/include
#CFLAGS += -O2 -lm
CFLAGS+= -g -lm -std=gnu99 -lcurl -lpthread -rdynamic

# Track header dependencies. Without this a change to config.h or connection.h left every
# object that already existed untouched, so a build could mix objects compiled against two
# different layouts of struct connection - each translation unit reading its fields at a
# different offset. That is not a subtle slowdown: sock_server saw PROCESS_FUNCTION as null
# no matter how many times a protocol set it, so it re-identified the device on every pass
# and buried it in handshakes, and the timestamp one file wrote was not the one the next
# file read. Both looked like protocol bugs and neither was reproducible from a clean tree.
CFLAGS+= -MMD -MP

all: $(TARGETS)

jimitrack: tracking.o main.o util.o multilaterate.o wifi_lookup.c search.c sort.c lbs_lookup.c web_geolocate.o crc16.o sock_server.o connection.o device/jimi_protocol.o device/jimi_packet.o string.o device/jimi_util.o commands.o events.o geofence.o logfiles.o device/megastek_protocol.o device/XEXUN_protocol.o device/thinkrace_protocol.o crc64.o device/myrope_protocol.o device/myrope_r18_protocol.c device/basic_protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

install: clean jimitrack
	cp jimitrack /bin/

clean:  
	find . -name "*.o" -delete
	find . -name "*.d" -delete
	rm -f jimitrack
	echo "clean"

style:
	astyle --style=java --break-blocks=all --pad-oper --pad-oper --pad-comma --pad-header --indent-switches --delete-empty-lines -k2 -xf --add-braces -Q  -n --recursive "*.c" "*.h"

-include $(shell find . -name '*.d' 2>/dev/null)

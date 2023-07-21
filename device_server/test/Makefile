TARGETS=jimitrack
CFLAGS+=-L/usr/local/opt/curl/lib
CFLAGS+=-I/usr/local/opt/curl/include
#CFLAGS += -O2 -lm
CFLAGS+= -g -lm -std=gnu99 -lcurl -lpthread -rdynamic -lm

all: $(TARGETS)

jimitrack:
	( cd .. && make jimitrack && cp jimitrack test/jimitrack)

install: clean jimitrack
	cp jimitrack /bin/

clean:
	rm jimitrack || true
	( cd .. && make clean )

test: clean jimitrack
	rm test >/dev/null 2>&1 || true
	$(CC) $(LDFLAGS) -o test test.c
	killall jimitrack >/dev/null 2>&1 || true
	./jimitrack >jimi.log 2>&1 &
	sleep 20
	./test
	sleep 3
	killall jimitrack

fuzzer: jimitrack
	rm fuzzer >/dev/null 2>&1 || true
	$(CC) $(CFLAGS) -o fuzzer fuzzer.c



thinkrace: thinkrace_test.o ../lbs_lookup.o ../wifi_lookup.o ../web_geolocate.o ../multilaterate.o ../sort.o ../search.o ../crc16.o ../util.o ../sock_server.o ../connection.o ../device/jimi_protocol.o ../device/jimi_packet.o ../string.o ../device/jimi_util.o ../commands.o ../events.o ../geofence.o ../logfiles.o ../device/megastek_protocol.o ../device/XEXUN_protocol.o ../device/thinkrace_protocol.o ../crc64.o  ../device/myrope_r18_protocol.o ../device/basic_protocol.o ../device/myrope_protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

xexun: xexun_test.o ../lbs_lookup.o ../wifi_lookup.o ../web_geolocate.o ../multilaterate.o ../sort.o ../search.o ../crc16.o ../util.o ../sock_server.o ../connection.o ../device/jimi_protocol.o ../device/jimi_packet.o ../string.o ../device/jimi_util.o ../commands.o ../events.o ../geofence.o ../logfiles.o ../device/megastek_protocol.o ../device/XEXUN_protocol.o ../crc64.o ../device/thinkrace_protocol.o  ../device/myrope_r18_protocol.o ../device/basic_protocol.o ../device/myrope_protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

wifi: test_wifi_lookup.o  ../lbs_lookup.o ../multilaterate.o ../sort.o ../search.o ../web_geolocate.o ../crc16.o ../util.o ../sock_server.o ../connection.o ../device/jimi_protocol.o ../device/jimi_packet.o ../string.o ../device/jimi_util.o ../commands.o ../events.o ../geofence.o ../logfiles.o ../device/megastek_protocol.o ../device/XEXUN_protocol.o ../device/thinkrace_protocol.o ../crc64.o  ../device/myrope_r18_protocol.o ../device/basic_protocol.o ../device/myrope_protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

lbs: test_lbs_lookup.o ../sort.o ../search.o ../web_geolocate.o ../wifi_lookup.o ../crc16.o ../util.o ../sock_server.o ../connection.o ../device/jimi_protocol.o ../device/jimi_packet.o ../string.o ../device/jimi_util.o ../commands.o ../events.o ../geofence.o ../logfiles.o ../device/megastek_protocol.o ../device/XEXUN_protocol.o ../crc64.o ../multilaterate.o ../device/thinkrace_protocol.o ../device/myrope_r18_protocol.o ../device/basic_protocol.o ../device/myrope_protocol.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

style:
	astyle --style=java --break-blocks=all --pad-oper --pad-oper --pad-comma --pad-header --indent-switches --delete-empty-lines -k2 -xf --add-braces -Q  -n --recursive "*.c" "*.h"

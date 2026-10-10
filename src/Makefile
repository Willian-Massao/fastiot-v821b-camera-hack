CC = riscv32-linux-musl-gcc
CFLAGS = -O2 -Wall
LDFLAGS = -lpthread

all: ptz_bridge.so cam_server

ptz_bridge.so: ptz_bridge.c
	$(CC) -fPIC -shared $(CFLAGS) $< -o $@ $(LDFLAGS)

cam_server: cam_server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f ptz_bridge.so cam_server

CC ?= riscv32-linux-musl-gcc
CFLAGS ?= -O2 -Wall -Wextra -Iinclude -D_GNU_SOURCE
LDFLAGS ?= -lpthread

SRCS = main.c motor_ctrl.c gpio_ctrl.c rtsp_server.c http_server.c ha_mqtt.c
OBJS = $(SRCS:.c=.o)
TARGET = open-ipcam

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

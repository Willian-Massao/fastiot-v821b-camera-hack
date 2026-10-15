#!/bin/bash
set -e

TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/home/willi/riscv32-linux-musl-cross/bin}"
export PATH="$TOOLCHAIN_BIN:$PATH"

CC=riscv32-linux-musl-gcc

echo "=== Compiling Open-IPCam for RISC-V 32-bit (Allwinner V821B) ==="

CFLAGS="-O2 -Wall -Wextra -Iinclude"
SRCS="main.c motor_ctrl.c gpio_ctrl.c rtsp_server.c http_server.c ha_mqtt.c wifi_mgr.c"

echo "1. Building dynamic binary (open-ipcam)..."
$CC $CFLAGS $SRCS -o open-ipcam -lpthread
riscv32-linux-musl-strip open-ipcam

echo "2. Building static binary (open-ipcam-static)..."
$CC $CFLAGS -static $SRCS -o open-ipcam-static -lpthread
riscv32-linux-musl-strip open-ipcam-static

echo "3. Copying binaries to sdcard release folder..."
mkdir -p ../../sdcard/FastIoT_factoryTest/
cp -f open-ipcam ../../sdcard/FastIoT_factoryTest/
cp -f open-ipcam-static ../../sdcard/FastIoT_factoryTest/

echo "Build complete!"
ls -lh open-ipcam open-ipcam-static

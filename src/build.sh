#!/bin/bash
set -e

# Toolchain RISC-V 32-bit musl
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/home/willi/riscv32-linux-musl-cross/bin}"
export PATH="$TOOLCHAIN_BIN:$PATH"

echo "Compilando ptz_bridge.so..."
riscv32-linux-musl-gcc -fPIC -shared -O2 ptz_bridge.c -o ptz_bridge.so -lpthread

echo "Compilando cam_server..."
riscv32-linux-musl-gcc -O2 cam_server.c -o cam_server -lpthread

echo "Copiando binarios para pasta sdcard..."
cp -f ptz_bridge.so ../sdcard/FastIoT_factoryTest/
cp -f cam_server ../sdcard/FastIoT_factoryTest/

echo "Build concluido com sucesso!"

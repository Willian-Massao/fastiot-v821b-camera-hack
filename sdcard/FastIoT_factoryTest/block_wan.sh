#!/bin/sh
while true; do
    /media/FastIoT_factoryTest/busybox ip route del default 2>/dev/null
    echo "127.0.0.1 localhost" > /tmp/resolv.conf 2>/dev/null
    sleep 5
done

#!/bin/sh
umount -l /system/bin/ipcam 2>/dev/null
export LD_PRELOAD=/media/FastIoT_factoryTest/ptz_bridge.so
exec /system/bin/ipcam "$@"

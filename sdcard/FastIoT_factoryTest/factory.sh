#!/bin/sh
DIR="/media/FastIoT_factoryTest"

chmod +x "$DIR/busybox" "$DIR/cam_server" "$DIR/ipcam_launcher.sh" "$DIR/block_wan.sh"

# Intercepta a inicializacao do ipcam para injetar ptz_bridge.so
mount --bind "$DIR/ipcam_launcher.sh" /system/bin/ipcam

# Telnet root na porta 23
"$DIR/busybox" telnetd -l /bin/ash -p 23 &

# Servidor Web na porta 8080
"$DIR/cam_server" &

# Bloqueio de acesso externo (Isolamento LAN - sem Internet)
"$DIR/block_wan.sh" &

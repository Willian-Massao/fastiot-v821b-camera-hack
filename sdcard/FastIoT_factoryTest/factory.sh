#!/bin/sh
DIR="/media/FastIoT_factoryTest"

chmod +x "$DIR/busybox" "$DIR/open-ipcam" "$DIR/open-ipcam-static" "$DIR/block_wan.sh" "$DIR/ipcam_launcher.sh"

# 1. Telnet root na porta 23 (sem senha)
"$DIR/busybox" telnetd -l /bin/ash -p 23 &

# 2. Bloqueio de acesso externo (Isolamento LAN - sem Internet nem nuvem)
"$DIR/block_wan.sh" &

# 3. Inicializa o Open-IPCam Daemon (Painel Web 8080, REST API, Home Assistant MQTT, Motor PTZ, GPIO IRCUT/LED)
if [ -x "$DIR/open-ipcam-static" ]; then
    "$DIR/open-ipcam-static" &
elif [ -x "$DIR/open-ipcam" ]; then
    "$DIR/open-ipcam" &
fi

# 4. Intercepta o app proprietario ipcam para injetar ptz_bridge caso esteja em modo hibrido
if [ -f "$DIR/ptz_bridge.so" ]; then
    mount --bind "$DIR/ipcam_launcher.sh" /system/bin/ipcam
fi

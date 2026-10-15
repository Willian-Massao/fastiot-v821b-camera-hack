#!/bin/sh
# wifi_setup.sh - Script de configuracao Wi-Fi rapida via Telnet/CLI
# Uso: ./wifi_setup.sh [ap | sta <SSID> <SENHA> | scan]

DIR="/media/FastIoT_factoryTest"

case "$1" in
    ap)
        echo "Ativando modo Access Point (OpenIPCam-Setup)..."
        killall -9 udhcpc wpa_supplicant 2>/dev/null
        wifi -f sta 2>/dev/null
        wifi -o ap OpenIPCam-Setup
        ifconfig wlan0 192.168.4.1 netmask 255.255.255.0 up
        printf "start 192.168.4.100\nend 192.168.4.200\ninterface wlan0\nopt subnet 255.255.255.0\nopt router 192.168.4.1\nopt dns 192.168.4.1\n" > /tmp/udhcpd_ap.conf
        udhcpd /tmp/udhcpd_ap.conf &
        echo "AP Ativo! Conecte-se em 'OpenIPCam-Setup' e acesse http://192.168.4.1:8080"
        ;;
    sta)
        if [ -z "$2" ]; then
            echo "Uso: $0 sta <SSID> [SENHA]"
            exit 1
        fi
        SSID="$2"
        PASS="$3"
        echo "Desativando modo AP e conectando a '$SSID'..."
        killall -9 udhcpd 2>/dev/null
        wifi -f ap 2>/dev/null
        ifconfig wlan0 0.0.0.0
        wifi -o sta
        if [ -n "$PASS" ]; then
            wifi -c "$SSID" "$PASS"
        else
            wifi -c "$SSID"
        fi
        udhcpc -i wlan0 -b -s /usr/share/udhcpc/default.script &
        echo "Conectando... Aguardando IP:"
        for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
            sleep 1
            IP=$(ifconfig wlan0 | grep 'inet addr:' | cut -d: -f2 | awk '{print $1}')
            if [ -n "$IP" ] && [ "$IP" != "0.0.0.0" ] && [ "$IP" != "192.168.4.1" ]; then
                echo "Conectado com sucesso! IP obtido: $IP"
                echo "Painel Web: http://$IP:8080"
                printf "ssid=%s\npassword=%s\n" "$SSID" "$PASS" > "$DIR/wifi.conf"
                exit 0
            fi
        done
        echo "Aviso: Nao foi possivel obter IP automaticamente. Verifique as credenciais."
        ;;
    scan)
        echo "Buscando redes Wi-Fi proximas..."
        wpa_cli -i wlan0 scan 2>/dev/null
        sleep 1
        wpa_cli -i wlan0 scan_results
        ;;
    *)
        echo "Uso:"
        echo "  $0 ap                  - Inicia rede de configuracao (OpenIPCam-Setup)"
        echo "  $0 sta <SSID> [SENHA]  - Conecta a uma rede Wi-Fi e desliga AP"
        echo "  $0 scan                - Lista redes Wi-Fi visiveis"
        ;;
esac

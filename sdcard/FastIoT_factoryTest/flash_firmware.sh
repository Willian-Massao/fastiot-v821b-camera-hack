#!/bin/sh
# flash_firmware.sh - Open-IPCam Web OTA Firmware Flashing Tool
# Supports flashing both:
#   1) Full 8MB SPI NOR flash image (8,388,608 bytes)
#   2) SquashFS /system partition image (<= 2,703,360 bytes)

IMAGE="$1"

if [ -z "$IMAGE" ] || [ ! -f "$IMAGE" ]; then
    echo "[OTA] Erro: Arquivo de imagem nao especificado ou inexistente: '$IMAGE'"
    exit 1
fi

SIZE=$(stat -c%s "$IMAGE" 2>/dev/null || wc -c < "$IMAGE")
echo "[OTA] ==============================================="
echo "[OTA] Iniciando gravacao de firmware OTA via Web"
echo "[OTA] Arquivo: $IMAGE ($SIZE bytes)"
echo "[OTA] ==============================================="

# Descobre o dispositivo MTD da particao /system (app)
APP_LNK=$(readlink /dev/by-name/app 2>/dev/null)
if [ -n "$APP_LNK" ]; then
    MTD_APP=$(echo "$APP_LNK" | sed 's/block//')
else
    MTD_APP="/dev/mtd6"
fi

echo "[OTA] Particao /system (app) mapeada em: $MTD_APP"

IMG_TO_FLASH=""
TARGET_MTD=""

# Caso 1: Imagem completa de 8MB (8.388.608 bytes)
if [ "$SIZE" -eq 8388608 ]; then
    echo "[OTA] Imagem completa de 8MB detectada!"
    
    # Verifica se /dev/mtd0 eh o chip inteiro de 8MB (00800000 em /proc/mtd)
    MTD0_SIZE=$(grep -E '^mtd0:' /proc/mtd 2>/dev/null | awk '{print $2}')
    if [ "$MTD0_SIZE" = "00800000" ]; then
        echo "[OTA] /dev/mtd0 cobre os 8MB completos da memoria flash NOR."
        TARGET_MTD="/dev/mtd0"
        IMG_TO_FLASH="$IMAGE"
    else
        echo "[OTA] /dev/mtd0 nao eh o chip completo (tamanho $MTD0_SIZE)."
        echo "[OTA] Extraindo particao /system (SquashFS no offset 0x51A000)..."
        # 0x51A000 = 5.349.376 bytes = 1306 blocos de 4096 bytes
        # Tamanho maximo = 2.703.360 bytes = 660 blocos de 4096 bytes
        dd if="$IMAGE" of=/tmp/system_extracted.squashfs bs=4096 skip=1306 count=660 2>/dev/null
        TARGET_MTD="$MTD_APP"
        IMG_TO_FLASH="/tmp/system_extracted.squashfs"
    fi
else
    # Caso 2: Imagem SquashFS da particao /system
    echo "[OTA] Imagem de particao /system detectada."
    TARGET_MTD="$MTD_APP"
    IMG_TO_FLASH="$IMAGE"
fi

echo "[OTA] Alvo de gravacao: $IMG_TO_FLASH -> $TARGET_MTD"

# Encerra processos pesados para evitar travamentos ou colisoes de barramento SPI
echo "[OTA] Encerrando ipcam para liberar recursos..."
killall -9 ipcam 2>/dev/null
sleep 1

# Localiza ferramenta flashcp
FLASHCP=""
if [ -x /usr/sbin/flashcp ]; then
    FLASHCP="/usr/sbin/flashcp"
elif [ -x /system/bin/busybox ]; then
    FLASHCP="/system/bin/busybox flashcp"
elif [ -x /bin/busybox ]; then
    FLASHCP="/bin/busybox flashcp"
else
    echo "[OTA] ERRO CRITICO: flashcp nao encontrado no sistema!"
    exit 2
fi

echo "[OTA] Executando: $FLASHCP -v $IMG_TO_FLASH $TARGET_MTD"
$FLASHCP -v "$IMG_TO_FLASH" "$TARGET_MTD"
STATUS=$?

if [ $STATUS -eq 0 ]; then
    echo "[OTA] ==============================================="
    echo "[OTA] GRAVACAO CONCLUIDA COM SUCESSO E VERIFICADA!"
    echo "[OTA] Sincronizando dados na flash e reiniciando..."
    echo "[OTA] ==============================================="
    sync
    sleep 2
    reboot -f
else
    echo "[OTA] ==============================================="
    echo "[OTA] ERRO NA GRAVACAO! Codigo de saida: $STATUS"
    echo "[OTA] A camera NAO sera reiniciada para prevencao de brick."
    echo "[OTA] ==============================================="
    exit 3
fi

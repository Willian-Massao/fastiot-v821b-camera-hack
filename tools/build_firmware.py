#!/usr/bin/env python3
"""
build_firmware.py - Gerador de Firmware Completo Open-IPCam para Allwinner V821B (8MB SPI NOR Flash)

Este script:
 1. Carrega o dump de firmware original (original.bin, 8.388.608 bytes).
 2. Extrai a particao /system (SquashFS no offset 0x51A000).
 3. Injeta o novo daemon open-ipcam (nativo RISC-V 32-bit), o busybox e atualiza o start.sh.
 4. Habilita Telnet permanente (porta 23) e remove dependencias da nuvem proprietaria (SightCloud).
 5. Reempacota a particao em SquashFS (xz, bloco 128KB).
 6. Grava no offset 0x51A000 com padding 0xFF ate o offset 0x7AE000.
 7. Gera a imagem final pronta para gravacao (8MB exatos).
"""

import os
import sys
import shutil
import subprocess
import hashlib

FLASH_TOTAL_SIZE = 8388608  # 8 MB
SYSTEM_OFFSET = 0x51A000     # 5.349.376
SYSTEM_MAX_SIZE = 0x7AE000 - SYSTEM_OFFSET  # 2.703.360 bytes (2.58 MB)

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

candidate_firmware_dirs = [
    "/mnt/c/Users/willi/OneDrive/Documentos/firmware/camera",
    r"C:\Users\willi\OneDrive\Documentos\firmware\camera",
    os.path.join(os.environ.get("USERPROFILE", ""), "OneDrive", "Documentos", "firmware", "camera"),
    os.path.join(os.path.expanduser("~"), "OneDrive", "Documentos", "firmware", "camera"),
]

FIRMWARE_DIR = next((d for d in candidate_firmware_dirs if os.path.isdir(d)), candidate_firmware_dirs[0])
ORIGINAL_BIN = os.path.join(FIRMWARE_DIR, "original.bin")
SDCARD_DIR = os.path.join(BASE_DIR, "sdcard", "FastIoT_factoryTest")
OUTPUT_BIN = os.path.join(BASE_DIR, "firmware", "open-ipcam-v821b-8mb.bin")

START_SH_CONTENT = """#!/bin/sh
# start.sh - Open-IPCam Native System Initialization (FastIoT V821B RISC-V 32-bit)

echo "=== Booting Open-IPCam Firmware (100% Local / Zero Cloud) ==="

# 1. Habilita acesso Root via Telnet permanente na porta 23
if [ -x /system/bin/busybox ]; then
    /system/bin/busybox telnetd -l /bin/ash -p 23 &
fi

# 2. Carrega modulos do kernel
sh /system/ko/loadsystemko.sh
insmod /system/ko/xradio_bt_smd.ko
insmod /system/ko/motor.ko
insmod /system/ko/logger.ko

# 3. Configuracoes de hardware e registradores sunxi
echo 97 > /sys/class/gpio/export 2>/dev/null
echo out > /sys/class/gpio/gpio97/direction 2>/dev/null
echo 1 > /sys/class/gpio/gpio97/value 2>/dev/null
echo 0x42000358 0x00000003 > /sys/class/sunxi_dump/write 2>/dev/null
echo 0x420000ac 0x33111111 > /sys/class/sunxi_dump/write 2>/dev/null

# 4. Diretorios de configuracao persistentes
mkdir -p /userdata/Config
if [ ! -f /userdata/local_timezone ]; then
    ln -s /system/zoneinfo/Shanghai /userdata/local_timezone 2>/dev/null
fi

export LD_LIBRARY_PATH="/system/lib:/lib:/usr/lib:/usr/local/lib"
export PATH="/system/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

# 5. Montagem do MicroSD (se inserido) e hook de seguranca/resgate
MDEV=""
if [ -e /dev/mmcblk0p1 ]; then
    MDEV=/dev/mmcblk0p1
elif [ -e /dev/mmcblk0 ]; then
    MDEV=/dev/mmcblk0
fi
if [ -n "$MDEV" ]; then
    mount -t vfat -o usefree $MDEV /media/ 2>/dev/null || mount -t exfat $MDEV /media/ 2>/dev/null
fi

# Se houver factory.sh no cartao, ele ainda tem prioridade de execucao (modo desenvolvimento/resgate)
if [ -f /media/FastIoT_factoryTest/factory.sh ]; then
    chmod +x /media/FastIoT_factoryTest/factory.sh
    /media/FastIoT_factoryTest/factory.sh &
    exit 0
fi

# 6. Inicia o daemon Open-IPCam (Web Dashboard 8080, RTSP 554, PTZ, GPIO, MQTT HA, Wi-Fi AP)
if [ -x /system/bin/open-ipcam-static ]; then
    /system/bin/open-ipcam-static &
elif [ -x /system/bin/open-ipcam ]; then
    /system/bin/open-ipcam &
elif [ -x /system/bin/ipcam ]; then
    /system/bin/ipcam &
fi

exit 0
"""

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main():
    print("=" * 65)
    print("  Open-IPCam Firmware Builder for FastIoT V821B (8MB SPI NOR)")
    print("=" * 65)

    if not os.path.exists(ORIGINAL_BIN):
        print(f"[!] Erro: original.bin nao encontrado em: {ORIGINAL_BIN}")
        sys.exit(1)

    orig_size = os.path.getsize(ORIGINAL_BIN)
    if orig_size != FLASH_TOTAL_SIZE:
        print(f"[!] Erro: original.bin tem tamanho invalido: {orig_size} bytes (esperado: {FLASH_TOTAL_SIZE})")
        sys.exit(1)

    build_workdir = os.path.join(BASE_DIR, "build_firmware_tmp")
    staging_dir = os.path.join(build_workdir, "squashfs-system")
    new_squashfs = os.path.join(build_workdir, "new_51A000.squashfs")

    if os.path.exists(build_workdir):
        shutil.rmtree(build_workdir)
    os.makedirs(staging_dir, exist_ok=True)
    os.makedirs(os.path.dirname(OUTPUT_BIN), exist_ok=True)

    # 1. Localizar fonte da particao /system
    reextract_sqfs = os.path.join(FIRMWARE_DIR, "reextract_51A000.squashfs")
    reextract_dir = os.path.join(FIRMWARE_DIR, "reextract-system")

    print("[1/6] Extraindo particao /system original...")
    if os.path.exists(reextract_dir):
        print(f"      Usando diretorio base (preservando symlinks): {reextract_dir}")
        shutil.copytree(reextract_dir, staging_dir, symlinks=True, dirs_exist_ok=True)
    elif os.path.exists(reextract_sqfs):
        print(f"      Descompactando via unsquashfs: {reextract_sqfs}")
        subprocess.run(["unsquashfs", "-d", staging_dir, "-f", reextract_sqfs], check=True)
    else:
        # Extrai via dd do original.bin
        print(f"      Extraindo bloco 0x51A000 diretamente do original.bin...")
        raw_sqfs = os.path.join(build_workdir, "extracted.sqfs")
        with open(ORIGINAL_BIN, "rb") as f:
            f.seek(SYSTEM_OFFSET)
            sqfs_bytes = f.read(SYSTEM_MAX_SIZE)
        with open(raw_sqfs, "wb") as f:
            f.write(sqfs_bytes)
        subprocess.run(["unsquashfs", "-d", staging_dir, "-f", raw_sqfs], check=True)

    # 2. Injetar binarios do Open-IPCam
    print("[2/6] Injetando binarios Open-IPCam e BusyBox...")
    bin_dir = os.path.join(staging_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)

    src_open_ipcam = os.path.join(SDCARD_DIR, "open-ipcam")
    src_open_ipcam_static = os.path.join(SDCARD_DIR, "open-ipcam-static")
    src_busybox = os.path.join(SDCARD_DIR, "busybox")
    src_conf = os.path.join(SDCARD_DIR, "open-ipcam.conf")

    # Remove o ipcam proprietario gigante (5.1MB)
    orig_ipcam_bin = os.path.join(bin_dir, "ipcam")
    if os.path.lexists(orig_ipcam_bin):
        if os.path.islink(orig_ipcam_bin) or os.path.isfile(orig_ipcam_bin):
            os.remove(orig_ipcam_bin)

    for f_src in [src_open_ipcam, src_open_ipcam_static, src_busybox]:
        if not os.path.exists(f_src):
            print(f"[!] Erro: arquivo necessario nao encontrado: {f_src}")
            sys.exit(1)
        dest = os.path.join(bin_dir, os.path.basename(f_src))
        if os.path.lexists(dest):
            os.remove(dest)
        shutil.copy2(f_src, dest)
        os.chmod(dest, 0o755)

    # Cria symlink ipcam -> open-ipcam-static
    try:
        os.symlink("open-ipcam-static", orig_ipcam_bin)
    except OSError:
        shutil.copy2(src_open_ipcam_static, orig_ipcam_bin)

    # Copia arquivo de configuracao para /system/config/
    cfg_dir = os.path.join(staging_dir, "config")
    os.makedirs(cfg_dir, exist_ok=True)
    shutil.copy2(src_conf, os.path.join(cfg_dir, "open-ipcam.conf"))

    # 3. Atualizar start.sh
    print("[3/6] Atualizando start.sh para inicializacao nativa...")
    start_sh_path = os.path.join(staging_dir, "start.sh")
    with open(start_sh_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(START_SH_CONTENT)
    os.chmod(start_sh_path, 0o755)

    # 4. Reempacotar com mksquashfs
    print("[4/6] Reempacotando particao /system com mksquashfs (xz, 128KB)...")
    if os.path.exists(new_squashfs):
        os.remove(new_squashfs)

    mksq_cmd = [
        "mksquashfs",
        staging_dir,
        new_squashfs,
        "-comp", "xz",
        "-b", "131072",
        "-noappend",
        "-all-root"
    ]
    subprocess.run(mksq_cmd, check=True)

    new_sqfs_size = os.path.getsize(new_squashfs)
    print(f"      Novo SquashFS gerado: {new_sqfs_size:,} bytes")
    print(f"      Espaco maximo disponivel: {SYSTEM_MAX_SIZE:,} bytes")

    if new_sqfs_size > SYSTEM_MAX_SIZE:
        print(f"[!] ERRO CRITICO: Novo SquashFS ({new_sqfs_size} bytes) excede o espaco maximo ({SYSTEM_MAX_SIZE} bytes)!")
        sys.exit(1)

    headroom = SYSTEM_MAX_SIZE - new_sqfs_size
    print(f"      Espaco livre restante na particao: {headroom:,} bytes ({headroom / 1024:.1f} KB)")

    # 5. Patching da imagem de 8MB
    print("[5/6] Aplicando patch no offset 0x51A000 de original.bin...")
    with open(ORIGINAL_BIN, "rb") as f:
        flash_data = bytearray(f.read())

    with open(new_squashfs, "rb") as f:
        sqfs_data = f.read()

    # Sobrescreve o squashfs na posicao
    flash_data[SYSTEM_OFFSET : SYSTEM_OFFSET + len(sqfs_data)] = sqfs_data

    # Preenche o restante ate 0x7AE000 com 0xFF (flash apagada)
    pad_start = SYSTEM_OFFSET + len(sqfs_data)
    pad_end = SYSTEM_OFFSET + SYSTEM_MAX_SIZE
    flash_data[pad_start:pad_end] = b"\xFF" * (pad_end - pad_start)

    with open(OUTPUT_BIN, "wb") as f:
        f.write(flash_data)

    # Copia tambem para a pasta de firmware para conveniencia
    firmware_copy = os.path.join(FIRMWARE_DIR, "open-ipcam-v821b-8mb.bin")
    shutil.copy2(OUTPUT_BIN, firmware_copy)

    # 6. Verificacao Final
    final_size = os.path.getsize(OUTPUT_BIN)
    final_sha = sha256_file(OUTPUT_BIN)

    print("[6/6] Verificacao e Hash:")
    print(f"      Arquivo final: {OUTPUT_BIN}")
    print(f"      Tamanho:       {final_size:,} bytes (deve ser exatamente {FLASH_TOTAL_SIZE:,})")
    print(f"      SHA256:        {final_sha}")

    if final_size == FLASH_TOTAL_SIZE:
        print("\n[✓] SUCESSO! Firmware completo Open-IPCam pronto para gravacao.")
    else:
        print("\n[!] ALERTA: Tamanho final incorreto!")

    # Limpeza
    shutil.rmtree(build_workdir, ignore_errors=True)

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
flash_telnet.py - Gravador de Firmware OTA via Telnet para Open-IPCam / FastIoT V821B

Este script:
 1. Conecta na camera via Telnet (porta 23) sem precisar de gravador CH341A nem abrir o aparelho.
 2. Transfere a nova particao /system (open-ipcam-system.squashfs, ~2.4MB) direto para /tmp/ via socket TCP.
 3. Executa a gravacao na memoria flash NOR usando flashcp com verificacao de integridade.
 4. Reinicia a camera automaticamente com o novo firmware ativo.

Uso:
    python tools/flash_telnet.py <IP_DA_CAMERA>
Exemplo:
    python tools/flash_telnet.py 192.168.1.150
"""

import sys
import os
import time
import socket
import re

PORT_TELNET = 23
PORT_TRANSFER = 9999

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE_CANDIDATES = [
    os.path.join(BASE_DIR, "firmware", "open-ipcam-system.squashfs"),
    os.path.join(r"C:\Users\willi\OneDrive\Documentos\firmware\camera", "open-ipcam-system.squashfs"),
]

def find_firmware():
    for f in FIRMWARE_CANDIDATES:
        if os.path.isfile(f):
            return f
    return None

def recv_until(sock, patterns, timeout=10):
    sock.settimeout(timeout)
    buf = ""
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = sock.recv(1024).decode("latin1", errors="ignore")
            if not chunk:
                break
            buf += chunk
            for p in patterns:
                if re.search(p, buf):
                    return buf
        except socket.timeout:
            break
    return buf

def send_cmd(sock, cmd, wait_patterns=[r"[#\$]\s*$", r"/ #\s*"], timeout=5):
    sock.sendall((cmd + "\n").encode("latin1"))
    time.sleep(0.3)
    return recv_until(sock, wait_patterns, timeout=timeout)

def main():
    if len(sys.argv) < 2:
        print("Uso: python tools/flash_telnet.py <IP_DA_CAMERA> [ARQUIVO_FIRMWARE]")
        print("Exemplo: python tools/flash_telnet.py 192.168.1.150")
        sys.exit(1)

    ip = sys.argv[1].strip()
    fw_path = sys.argv[2] if len(sys.argv) > 2 else find_firmware()

    if not fw_path or not os.path.isfile(fw_path):
        print(f"[!] Erro: Arquivo de firmware nao encontrado: {fw_path}")
        sys.exit(1)

    fw_size = os.path.getsize(fw_path)
    print("=" * 65)
    print("  Gravador de Firmware OTA via Telnet - Open-IPCam")
    print("=" * 65)
    print(f"[*] Camera IP:   {ip}:{PORT_TELNET}")
    print(f"[*] Firmware:    {fw_path}")
    print(f"[*] Tamanho:     {fw_size:,} bytes ({fw_size / 1024 / 1024:.2f} MB)")
    print("=" * 65)

    # 1. Conectar via Telnet
    print("[1/5] Conectando a camera via Telnet (porta 23)...")
    try:
        t_sock = socket.create_connection((ip, PORT_TELNET), timeout=8)
    except Exception as e:
        print(f"[!] Falha ao conectar em {ip}:{PORT_TELNET}: {e}")
        print("    -> Verifique se a camera esta ligada e no mesmo Wi-Fi.")
        sys.exit(1)

    # Aguarda prompt inicial
    out = recv_until(t_sock, [r"[#\$]\s*$", r"/ #\s*", r"TinaLinux"], timeout=4)
    print("      Conectado com sucesso ao shell Root!")

    # 2. Descobrir particao MTD do /system (app)
    print("[2/5] Identificando particoes de memoria flash NOR...")
    out = send_cmd(t_sock, "readlink /dev/by-name/app || echo /dev/mtdblock6")
    mtd_target = "/dev/mtd6"
    match = re.search(r"/dev/mtdblock(\d+)", out)
    if match:
        mtd_num = match.group(1)
        mtd_target = f"/dev/mtd{mtd_num}"
    print(f"      Particao /system identificada em: {mtd_target}")

    # 3. Iniciar receptor netcat na camera
    print(f"[3/5] Abrindo porta de transferencia TCP {PORT_TRANSFER} na camera...")
    # Mata qualquer listener antigo e sobe um novo
    send_cmd(t_sock, "killall -9 nc 2>/dev/null ; rm -f /tmp/fw.squashfs")
    time.sleep(0.5)
    t_sock.sendall(f"nc -l -p {PORT_TRANSFER} > /tmp/fw.squashfs &\n".encode("latin1"))
    time.sleep(1.0)

    # 4. Enviar arquivo via socket do PC para a camera
    print(f"[4/5] Enviando {fw_size:,} bytes para a camera via rede...")
    try:
        data_sock = socket.create_connection((ip, PORT_TRANSFER), timeout=10)
    except Exception as e:
        # Tenta modo alternativo sem -p se o nc da camera for versao antiga
        print(f"      Tentando modo alternativo de porta...")
        send_cmd(t_sock, f"nc -l {PORT_TRANSFER} > /tmp/fw.squashfs &")
        time.sleep(1.0)
        data_sock = socket.create_connection((ip, PORT_TRANSFER), timeout=10)

    with open(fw_path, "rb") as f:
        sent = 0
        chunk_size = 16384
        start_time = time.time()
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            data_sock.sendall(chunk)
            sent += len(chunk)
            pct = (sent / fw_size) * 100
            elapsed = time.time() - start_time
            speed = (sent / 1024) / (elapsed if elapsed > 0 else 0.001)
            sys.stdout.write(f"\r      Enviando: {pct:5.1f}% | {sent:,}/{fw_size:,} bytes | {speed:.1f} KB/s")
            sys.stdout.flush()
    data_sock.close()
    print("\n      Upload finalizado com sucesso!")

    # Aguarda gravacao no /tmp
    time.sleep(1.5)
    out = send_cmd(t_sock, "ls -l /tmp/fw.squashfs")
    print(f"      Verificacao no sistema de arquivos: {out.strip()}")

    # 5. Executar o flash e reboot
    print(f"[5/5] Gravando na memoria flash ({mtd_target}) usando flashcp...")
    print("      Encerrando servicos para garantir estabilidade do barramento SPI...")
    send_cmd(t_sock, "killall -9 ipcam open-ipcam 2>/dev/null")
    time.sleep(1.0)

    print(f"      Executando: flashcp -v /tmp/fw.squashfs {mtd_target}")
    t_sock.sendall(f"/usr/sbin/flashcp -v /tmp/fw.squashfs {mtd_target} ; echo FLASH_RESULT:$? ; sync ; sleep 2 ; reboot -f\n".encode("latin1"))

    # Monitora o log do flashcp
    flash_start = time.time()
    success = False
    while time.time() - flash_start < 60:
        try:
            line = t_sock.recv(512).decode("latin1", errors="ignore")
            if not line:
                break
            sys.stdout.write(line)
            sys.stdout.flush()
            if "FLASH_RESULT:0" in line or "100%" in line:
                success = True
        except socket.timeout:
            pass

    t_sock.close()
    print("\n" + "=" * 65)
    if success:
        print("  [✓] GRAVACAO CONCLUIDA COM SUCESSO!")
        print("  A camera esta reiniciando com o novo firmware.")
        print(f"  Em cerca de 15 segundos, acesse o painel web em:")
        print(f"      http://{ip}:8080/")
        print(f"  E o stream RTSP em:")
        print(f"      rtsp://admin:Ztac@2021@{ip}:554/live/camid_0/main")
    else:
        print("  Comando disparado! A camera deve reiniciar em instantes.")
    print("=" * 65)

if __name__ == "__main__":
    main()

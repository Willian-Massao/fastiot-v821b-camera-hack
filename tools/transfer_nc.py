import socket
import time
import hashlib
import sys

def transfer(local_path, remote_path, camera_ip="192.168.15.72", port=9999):
    with open(local_path, "rb") as f:
        data = f.read()
    local_md5 = hashlib.md5(data).hexdigest()
    print(f"Local file: {local_path} ({len(data)} bytes, md5: {local_md5})")

    # Connect to telnet
    s_telnet = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s_telnet.settimeout(5)
    s_telnet.connect((camera_ip, 23))
    time.sleep(0.3)
    s_telnet.recv(1024)

    # Start nc in foreground of this session
    cmd = f"/media/FastIoT_factoryTest/busybox nc -l -p {port} > {remote_path}\n"
    s_telnet.sendall(cmd.encode('utf-8'))
    time.sleep(0.3)

    # Connect to port 9999 and send data
    s_data = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s_data.settimeout(5)
    s_data.connect((camera_ip, port))
    s_data.sendall(data)
    s_data.close()
    
    print("Data sent via port 9999, waiting for nc to finish...")
    time.sleep(0.5)

    # Verify on camera
    check_cmd = f"ls -l {remote_path}; /media/FastIoT_factoryTest/busybox md5sum {remote_path}\necho __END__\n"
    s_telnet.sendall(check_cmd.encode('utf-8'))
    time.sleep(0.5)
    res = s_telnet.recv(4096).decode('utf-8', errors='replace')
    s_telnet.close()
    print("Camera response:\n", res)

if __name__ == "__main__":
    if len(sys.argv) >= 3:
        transfer(sys.argv[1], sys.argv[2])
    else:
        print("Usage: transfer_nc.py <local> <remote>")

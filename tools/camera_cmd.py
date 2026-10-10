import socket
import sys
import time

def run_cmd(cmd, timeout=5):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(('192.168.15.72', 23))
    
    # Read until prompt
    buf = b""
    start = time.time()
    while time.time() - start < 3:
        try:
            chunk = s.recv(1024)
            if not chunk:
                break
            buf += chunk
            if b"/ # " in buf:
                break
        except socket.timeout:
            break

    marker = "__CMD_END__"
    full_cmd = f"{cmd}\necho {marker}\n".encode('utf-8')
    s.sendall(full_cmd)

    output = b""
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = s.recv(4096)
            if not chunk:
                break
            output += chunk
            if marker.encode('utf-8') in output:
                break
        except socket.timeout:
            break
            
    s.close()
    text = output.decode('utf-8', errors='replace')
    # Extract output before marker
    if marker in text:
        text = text[:text.index(marker)]
    # Drop first line if it's the echoed command
    lines = text.splitlines()
    if lines and cmd.split()[0] in lines[0]:
        lines = lines[1:]
    return "\n".join(lines).strip()

if __name__ == "__main__":
    if len(sys.argv) > 1:
        command = " ".join(sys.argv[1:])
        sys.stdout.buffer.write(run_cmd(command).encode('utf-8', errors='replace') + b'\n')
    else:
        sys.stdout.buffer.write(run_cmd("uname -a; uptime").encode('utf-8', errors='replace') + b'\n')

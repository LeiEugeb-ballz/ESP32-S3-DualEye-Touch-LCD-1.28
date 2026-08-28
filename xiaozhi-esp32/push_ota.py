#!/usr/bin/env python3
import sys
import os
import socket
import time

def push_firmware(ip, bin_path, port=3232):
    if not os.path.exists(bin_path):
        print(f"Error: File not found: {bin_path}")
        return False
        
    file_size = os.path.getsize(bin_path)
    print(f"=" * 60)
    print(f"ESP32-S3 Wireless Push OTA Flasher")
    print(f"Target: {ip}:{port}")
    print(f"Firmware: {bin_path} ({file_size / 1024 / 1024:.2f} MB)")
    print(f"=" * 60)

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10.0)
        print(f"Connecting to {ip}:{port}...")
        s.connect((ip, port))
        print("Connected! Streaming firmware...")

        sent_bytes = 0
        t0 = time.time()
        with open(bin_path, "rb") as f:
            while True:
                chunk = f.read(4096)
                if not chunk:
                    break
                s.sendall(chunk)
                sent_bytes += len(chunk)
                
                pct = (sent_bytes / file_size) * 100
                speed = (sent_bytes / 1024) / (time.time() - t0 + 0.001)
                bar = "#" * int(pct / 4) + "-" * (25 - int(pct / 4))
                print(f"\r[{bar}] {pct:5.1f}% | {sent_bytes/1024:.0f}/{file_size/1024:.0f} KB ({speed:.1f} KB/s)", end="", flush=True)

        s.shutdown(socket.SHUT_WR)
        print("\n\nFirmware stream complete. Waiting for device flash confirmation...")
        s.settimeout(20.0)
        resp = s.recv(1024)
        s.close()
        
        resp_str = resp.decode("utf-8", errors="ignore").strip()
        print(f"Device response: {resp_str}")
        if "OK" in resp_str:
            print("\n>>> OTA FLASH SUCCESSFUL! Device is rebooting into new firmware! <<<")
            return True
        else:
            print(f"\nDevice returned error: {resp_str}")
            return False
            
    except Exception as e:
        print(f"\nOTA Failed: {e}")
        return False

if __name__ == "__main__":
    target_ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.88.221"
    bin_file = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "build", "xiaozhi.bin")
    
    push_firmware(target_ip, bin_file)

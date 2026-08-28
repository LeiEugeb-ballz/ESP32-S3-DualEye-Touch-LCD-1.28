import os
import sys
import time
import subprocess
import serial.tools.list_ports

ESP_PYTHON = r"C:\Users\txr45\\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe".replace("\\\\", "\\")
BUILD_DIR = r"C:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32\build"

print("=" * 65)
print("ESP32-S3 Active Flasher & Tester")
print("Waiting for ESP32 connection (COM13, COM14, or any USB Serial)...")
print("=" * 65)

def get_target_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if "303A" in p.hwid or "10C4" in p.hwid or "1A86" in p.hwid or p.device in ["COM13", "COM14", "COM9", "COM18", "COM11", "COM12", "COM7", "COM8"]:
            return p.device
    return None

while True:
    port = get_target_port()
    if port:
        print(f"\n[+] Found target port {port}! Executing flash...")
        cmd = [
            ESP_PYTHON, "-m", "esptool",
            "--chip", "esp32s3",
            "-p", port,
            "-b", "460800",
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            "--flash_mode", "dio",
            "--flash_freq", "80m",
            "--flash_size", "16MB",
            "0x0", os.path.join(BUILD_DIR, "bootloader", "bootloader.bin"),
            "0x8000", os.path.join(BUILD_DIR, "partition_table", "partition-table.bin"),
            "0xd000", os.path.join(BUILD_DIR, "ota_data_initial.bin"),
            "0x20000", os.path.join(BUILD_DIR, "xiaozhi.bin"),
            "0x800000", os.path.join(BUILD_DIR, "generated_assets.bin")
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0:
            print("\n" + "=" * 65)
            print(">>> FLASH SUCCESSFUL! <<<")
            print("=" * 65)
            print(res.stdout)
            print(f"\n[+] Starting live Serial Monitor on {port} at 115200 baud...\n")
            time.sleep(1.0)
            try:
                import serial
                ser = serial.Serial(port, 115200, timeout=0.1)
                t_end = time.time() + 25
                while time.time() < t_end:
                    line = ser.readline()
                    if line:
                        print(line.decode('utf-8', errors='ignore'), end='')
                ser.close()
            except Exception as e:
                print(f"Monitor note: {e}")
            break
        else:
            err = (res.stderr.strip() or res.stdout.strip())
            print(f"[{port}] Handshaking... If not progressing, hold BOOT and press RST.")
            time.sleep(1.5)
    else:
        time.sleep(0.5)

print("\nFlasher finished.")

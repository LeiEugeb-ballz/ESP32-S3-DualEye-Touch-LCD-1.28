import os
import sys
import time
import subprocess
import serial
import serial.tools.list_ports

ESP_PYTHON = r"C:\Users\txr45\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe"
BUILD_DIR = r"C:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32\build"

def find_esp_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        # Check for Espressif VID 303A, CP210x 10C4, CH340 1A86, or COM14
        if "303A" in p.hwid or "10C4" in p.hwid or "1A86" in p.hwid or p.device == "COM14":
            return p.device
    return None

print("=" * 65)
print("ESP32-S3 Continuous Auto-Flash & Monitor Daemon")
print("Waiting for ESP32 board to be connected / powered on...")
print("=" * 65)

flashed = False

while not flashed:
    port = find_esp_port()
    if port:
        print(f"\n[+] Detected ESP32 device on {port}! Starting flash...")
        flash_cmd = [
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
        res = subprocess.run(flash_cmd, capture_output=True, text=True)
        if res.returncode == 0:
            print("\n" + "=" * 65)
            print(">>> FLASH SUCCESSFUL! <<<")
            print("=" * 65)
            print(res.stdout)
            flashed = True
            break
        else:
            print(f"Flash attempt failed on {port}. If needed, hold BOOT & tap RST.")
            print("Error details:", (res.stderr.strip() or res.stdout.strip())[:200])
            time.sleep(1.0)
    else:
        time.sleep(0.5)

if flashed:
    print(f"\n[+] Connecting Serial Monitor on {port} at 115200 baud...\n" + "-" * 65)
    time.sleep(0.5)
    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
        start_t = time.time()
        while time.time() - start_t < 20: # Monitor for 20 seconds
            line = ser.readline()
            if line:
                try:
                    print(line.decode('utf-8', errors='ignore'), end='')
                except:
                    pass
        ser.close()
    except Exception as e:
        print(f"Monitor error: {e}")

print("\nAuto-Flash & Monitor Daemon session finished.")

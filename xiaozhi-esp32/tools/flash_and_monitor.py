import os
import sys
import time
import subprocess

ESP_PYTHON = r"C:\Users\txr45\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe"
BUILD_DIR = r"C:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32\build"
PORT = "COM14"

flash_cmd = [
    ESP_PYTHON, "-m", "esptool",
    "--chip", "esp32s3",
    "-p", PORT,
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

print("=" * 60)
print("ESP32-S3 DualEye Auto-Flasher")
print(f"Target Port: {PORT}")
print("If port shows 'device not functioning', hold BOOT and press RST once.")
print("=" * 60)

max_attempts = 30
for attempt in range(1, max_attempts + 1):
    print(f"[{attempt}/{max_attempts}] Attempting to flash {PORT}...")
    res = subprocess.run(flash_cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print("\n>>> FLASH SUCCESSFUL! <<<\n")
        print(res.stdout)
        break
    else:
        if "device attached to the system is not functioning" in res.stderr or "Could not open" in res.stderr or "Failed to set baud rate" in res.stderr:
            print("Waiting for bootloader... Please hold BOOT and tap RST on board.")
        else:
            print("Output:", res.stderr.strip())
        time.sleep(1.5)

print("Flash loop completed.")

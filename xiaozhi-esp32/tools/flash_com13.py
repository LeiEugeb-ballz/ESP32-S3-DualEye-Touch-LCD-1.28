import os
import sys
import time
import subprocess

ESP_PYTHON = r"C:\Users\txr45\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe"
BUILD_DIR = r"C:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32\build"
PORT = "COM13"

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

print("=" * 65)
print(f"Targeting: {PORT}")
print("Please HOLD the 'BOOT' button and TAP the 'RST' (Reset) button once.")
print("=" * 65)

for attempt in range(1, 40):
    print(f"[{attempt:02d}/40] Waiting for ROM bootloader on {PORT}...", end="", flush=True)
    res = subprocess.run(flash_cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print("\n\n" + "=" * 65)
        print(">>> FLASH SUCCESSFUL! <<<")
        print("=" * 65)
        print(res.stdout)
        break
    else:
        print(" (ready - press BOOT+RST)")
        time.sleep(1.0)

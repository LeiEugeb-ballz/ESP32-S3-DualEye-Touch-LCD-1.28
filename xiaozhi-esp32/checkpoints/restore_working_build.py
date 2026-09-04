#!/usr/bin/env python3
"""
One-click restoration script to flash the canonical Golden Build (step2_10_with_wifi.bin)
to the Waveshare ESP32-S3 DualEye board.
"""
import os
import sys
import subprocess

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM13"
    current_dir = os.path.dirname(os.path.abspath(__file__))
    golden_bin = os.path.join(current_dir, "step2_10_with_wifi.bin")
    
    if not os.path.exists(golden_bin):
        print(f"Error: Golden checkpoint not found at {golden_bin}")
        sys.exit(1)
        
    esp_python = sys.executable
    print(f"=== Restoring Golden Checkpoint to {port} ===")
    print(f"Target Binary: {golden_bin} ({os.path.getsize(golden_bin)} bytes)")
    
    cmd = [
        esp_python, "-m", "esptool",
        "--chip", "esp32s3",
        "-p", port,
        "-b", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0", golden_bin
    ]
    
    print("Executing esptool...")
    res = subprocess.run(cmd)
    if res.returncode == 0:
        print("\n>>> RESTORATION COMPLETE! Board restored to verified working build. <<<")
    else:
        print("\n>>> Flashing failed. Please check port connection. <<<")

if __name__ == "__main__":
    main()

import argparse
import os
import subprocess
import sys
import time

import serial
import serial.tools.list_ports


ESP_PYTHON = r"C:\Users\txr45\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe"
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build_safe")


def is_esp_port(port):
    hwid = (port.hwid or "").upper()
    return (
        "VID:PID=303A:" in hwid
        or "VID:PID=10C4:" in hwid
        or "VID:PID=1A86:" in hwid
    )


def find_esp_port(preferred):
    ports = list(serial.tools.list_ports.comports())
    if preferred:
        for port in ports:
            if port.device.upper() == preferred.upper():
                return port.device
    for port in ports:
        if is_esp_port(port):
            return port.device
    return None


def require_build_artifacts():
    required = [
        os.path.join(BUILD_DIR, "bootloader", "bootloader.bin"),
        os.path.join(BUILD_DIR, "partition_table", "partition-table.bin"),
        os.path.join(BUILD_DIR, "ota_data_initial.bin"),
        os.path.join(BUILD_DIR, "xiaozhi.bin"),
        os.path.join(BUILD_DIR, "generated_assets.bin"),
    ]
    missing = [path for path in required if not os.path.exists(path)]
    if missing:
        raise SystemExit("Missing build artifacts:\n" + "\n".join(missing))
    return required


def flash(port, artifacts):
    bootloader, partition_table, otadata, app, assets = artifacts
    cmd = [
        ESP_PYTHON,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "-p",
        port,
        "-b",
        "460800",
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "write_flash",
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
        "--flash_size",
        "16MB",
        "0x0",
        bootloader,
        "0x8000",
        partition_table,
        "0xd000",
        otadata,
        "0x20000",
        app,
        "0x800000",
        assets,
    ]
    return subprocess.run(cmd, text=True)


def monitor(port, seconds):
    deadline = time.time() + seconds
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        while time.time() < deadline:
            line = ser.readline()
            if line:
                sys.stdout.write(line.decode("utf-8", errors="ignore"))
                sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser(description="Flash this checkout's build_safe app and speech assets.")
    parser.add_argument("--port", default="COM13", help="Preferred serial port. Default: COM13.")
    parser.add_argument("--wait", type=int, default=180, help="Seconds to wait for an ESP32 serial device.")
    parser.add_argument("--monitor", type=int, default=30, help="Seconds to monitor serial output after flashing.")
    args = parser.parse_args()

    artifacts = require_build_artifacts()
    deadline = time.time() + args.wait
    port = None
    while time.time() < deadline:
        port = find_esp_port(args.port)
        if port:
            break
        time.sleep(1)

    if not port:
        ports = "\n".join(f"{p.device}: {p.description} [{p.hwid}]" for p in serial.tools.list_ports.comports())
        raise SystemExit("No ESP32 serial port found. Visible ports:\n" + ports)

    print(f"Flashing {port} from {BUILD_DIR}")
    result = flash(port, artifacts)
    if result.returncode != 0:
        raise SystemExit(result.returncode)

    if args.monitor > 0:
        time.sleep(1)
        print(f"\nMonitoring {port} for {args.monitor}s")
        monitor(port, args.monitor)


if __name__ == "__main__":
    main()

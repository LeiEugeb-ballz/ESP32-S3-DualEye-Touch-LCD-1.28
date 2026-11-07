import argparse
import os
import subprocess
import sys
import time

import serial
import serial.tools.list_ports


ESP_PYTHON = os.environ.get("ESP_PYTHON", sys.executable)
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD_DIR = os.path.join(PROJECT_ROOT, "build_safe")


def is_esp_port(port):
    hwid = (port.hwid or "").upper()
    return (
        "VID:PID=303A:" in hwid
        or "VID:PID=10C4:" in hwid
        or "VID:PID=1A86:" in hwid
    )


def find_esp_port(preferred=None):
    ports = list(serial.tools.list_ports.comports())
    if preferred and preferred.upper() != "AUTO":
        for port in ports:
            if port.device.upper() == preferred.upper() and is_esp_port(port):
                return port.device
        # Preferred port was not found or is not an ESP; scan for any ESP port
        print(f"Preferred port {preferred} not detected as ESP32 device. Scanning all visible ports...")
    for port in ports:
        if is_esp_port(port):
            return port.device
    return None


def get_default_build_dir():
    new_build = os.path.join(PROJECT_ROOT, "build")
    safe_build = os.path.join(PROJECT_ROOT, "build_safe")
    new_bin = os.path.join(new_build, "xiaozhi.bin")
    safe_bin = os.path.join(safe_build, "xiaozhi.bin")
    if os.path.exists(new_bin):
        if not os.path.exists(safe_bin) or os.path.getmtime(new_bin) >= os.path.getmtime(safe_bin):
            return new_build
    return safe_build


def require_build_artifacts(build_dir):
    required = [
        os.path.join(build_dir, "bootloader", "bootloader.bin"),
        os.path.join(build_dir, "partition_table", "partition-table.bin"),
        os.path.join(build_dir, "ota_data_initial.bin"),
        os.path.join(build_dir, "xiaozhi.bin"),
        os.path.join(build_dir, "generated_assets.bin"),
    ]
    missing = [path for path in required if not os.path.exists(path)]
    if missing:
        raise SystemExit(f"Missing build artifacts in {build_dir}:\n" + "\n".join(missing))
    return required



def flash(port, artifacts, baud="460800"):
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
        str(baud),
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
    parser = argparse.ArgumentParser(description="Flash JAPA DualEye firmware and speech assets with dynamic COM port support.")
    parser.add_argument("--port", default=None, help="Serial port (e.g. COM13). If omitted or 'auto', automatically detects ESP32 port.")
    parser.add_argument("--baud", default="460800", help="Flashing baud rate. Default: 460800.")
    parser.add_argument("--build-dir", default=None, help="Build directory with artifacts (defaults to latest between build and build_safe).")
    parser.add_argument("--wait", type=int, default=30, help="Seconds to wait for an ESP32 serial device. Default: 30.")
    parser.add_argument("--monitor", type=int, default=30, help="Seconds to monitor serial output after flashing. Default: 30.")
    args = parser.parse_args()

    build_dir = args.build_dir or get_default_build_dir()
    artifacts = require_build_artifacts(build_dir)
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

    print(f"Flashing {port} from {build_dir} at {args.baud} baud")
    result = flash(port, artifacts, baud=args.baud)
    if result.returncode != 0:
        raise SystemExit(result.returncode)

    if args.monitor > 0:
        time.sleep(1)
        print(f"\nMonitoring {port} for {args.monitor}s")
        monitor(port, args.monitor)


if __name__ == "__main__":
    main()

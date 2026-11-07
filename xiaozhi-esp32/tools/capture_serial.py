"""Bounded serial evidence capture. Never flashes, scans other devices or changes baud."""
import argparse
from contextlib import ExitStack
import sys
import time
import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=180)
    parser.add_argument("--output")
    parser.add_argument("--reset", action="store_true", help="Explicitly reboot the attached ESP32 to capture its boot identity")
    args = parser.parse_args()
    ports = [p for p in list_ports.comports() if p.device.upper() == args.port.upper()]
    if len(ports) != 1 or ports[0].vid != 0x303A or ports[0].pid != 0x1001:
        raise SystemExit("Exact supplied port is not the expected Espressif USB device; no port was opened")
    if not 1 <= args.seconds <= 7200:
        raise SystemExit("Capture duration must be 1-7200 seconds")
    with ExitStack() as stack:
        log = stack.enter_context(open(args.output, "w", encoding="utf-8")) if args.output else None
        def emit(text):
            print(text, end="", flush=True)
            if log:
                log.write(text); log.flush()
        port = serial.Serial(port=None, baudrate=115200, timeout=.2)
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        stack.callback(port.close)
        emit(f"CAPTURE_START port={args.port} seconds={args.seconds} utc={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}\n")
        if args.reset:
            from esptool.reset import HardReset
            emit("EXPLICIT_RESET: Espressif USB Serial/JTAG hard reset\n")
            HardReset(port, uses_usb=True)()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            data = port.readline()
            if data: emit(data.decode("utf-8", errors="replace"))
        emit(f"\nMONITOR_COMPLETE: {args.seconds} seconds\n")


if __name__ == "__main__":
    main()

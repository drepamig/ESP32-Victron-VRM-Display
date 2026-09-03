#!/usr/bin/env python3
import argparse
import sys

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="CYD serial monitor")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    with serial.Serial(args.port, args.baud, timeout=0.25) as device:
        try:
            while True:
                data = device.read(4096)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
        except KeyboardInterrupt:
            return 0


if __name__ == "__main__":
    raise SystemExit(main())

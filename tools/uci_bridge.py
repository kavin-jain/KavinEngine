#!/usr/bin/env python3
"""Relay UCI between stdin/stdout (fastchess) and the ESP32 engine over USB-CDC serial.

Usage: uci_bridge.py /dev/cu.usbmodemXXXX
Exits non-zero if the serial link drops; fastchess then counts the game as a loss (conservative, by design).
"""
import os
import sys
import threading
import time

import serial


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: uci_bridge.py <serial-port>")
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = sys.argv[1], 115200, 0.1
    ser.dtr = ser.rts = False  # don't toggle the ESP32-S3 into reset/bootloader on open
    ser.open()
    time.sleep(0.3)
    ser.reset_input_buffer()

    def pump() -> None:
        buf = b""
        while True:
            try:
                buf += ser.read(4096)
            except serial.SerialException:
                os._exit(2)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                sys.stdout.write(line.rstrip(b"\r").decode(errors="replace") + "\n")
                sys.stdout.flush()

    threading.Thread(target=pump, daemon=True).start()
    for line in sys.stdin:
        try:
            ser.write(line.encode())
            ser.flush()
        except serial.SerialException:
            os._exit(2)
        if line.strip() == "quit":
            break
    time.sleep(0.2)


if __name__ == "__main__":
    main()

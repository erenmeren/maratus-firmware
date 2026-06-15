#!/usr/bin/env python3
"""Passive UART0 console reader for the ESP32-P4 Ditto board.

`idf.py monitor` drives this board's DTR/RTS lines and traps it in download
mode. This reader holds DTR/RTS low (normal boot straps) and just reads the
UART0 console, so the application actually runs while we watch it.

Usage: python tools/read-console.py [seconds]   # default 12s
To capture a fresh boot, tap RST (not BOOT) while it reads, or power-cycle.
"""
import serial, glob, time, sys
ports = sorted(glob.glob('/dev/cu.usbmodem*'))
if not ports: sys.exit("no usbmodem port")
s = serial.Serial(); s.port = ports[0]; s.baudrate = 115200; s.timeout = 0.4
s.dtr = False; s.rts = False         # hold straps for normal boot (no download mode)
s.open(); s.dtr = False; s.rts = False
secs = int(sys.argv[1]) if len(sys.argv) > 1 else 12
end = time.time() + secs
while time.time() < end:
    d = s.read(400)
    if d: sys.stdout.write(d.decode(errors='replace')); sys.stdout.flush()
s.close()

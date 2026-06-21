#!/usr/bin/env python3
"""Raw serial reader that holds normal-boot straps (dtr/rts low->False) so the
ESP32-P4 board does NOT drop into download mode (which idf_monitor triggers).
Usage: serial-read.py <port> <seconds>
"""
import sys, time, serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.dtr = False
s.rts = False
s.timeout = 0.2
s.open()

end = time.time() + secs
buf = b""
while time.time() < end:
    data = s.read(4096)
    if data:
        buf += data
        try:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
        except Exception:
            pass
s.close()

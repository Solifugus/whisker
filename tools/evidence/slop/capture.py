#!/usr/bin/env python3
"""
Run wslop on the P4 from its USB console and keep what it prints.

	. $HOME/esp/esp-idf/export.sh
	python tools/evidence/slop/capture.py LOG [PORT]

Opening the port resets the board, which is harmless to touch -- only a
flash leaves the GT911 stuck (RV-9 design §56). So this waits for the boot
self-tests to fall quiet before typing, then reads until wslop's closing
paragraph or fifteen minutes, whichever comes first.
"""
import sys
import time

import serial

LOG = sys.argv[1]
PORT = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
END = b"BIAS is a different fault"

s = serial.Serial()
s.port = PORT
s.baudrate = 115200
s.timeout = 0.2
s.dtr = False
s.rts = False
s.open()

out = open(LOG, "wb")


def pump(secs):
	buf = b""
	t = time.time()
	while time.time() - t < secs:
		b = s.read(4096)
		if b:
			buf += b
			out.write(b)
			out.flush()
	return buf


# the self-tests run past 20 s; typing before they finish is lost
t = time.time()
quiet = time.time()
while time.time() - t < 90 and time.time() - quiet < 4:
	if pump(0.5):
		quiet = time.time()
s.write(b"\r")
pump(1.5)
s.write(b"wslop > /w0\r")

seen = b""
t0 = time.time()
while time.time() - t0 < 900:
	seen += pump(1.0)
	if END in seen:
		pump(2.0)
		break
out.close()

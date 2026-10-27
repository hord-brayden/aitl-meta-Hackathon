#!/usr/bin/env python3
"""Send one serial console line to the gadget and print its answers.
  gadget/console.py touch=probe      # pen line, raw touch reads, I2C scan on the usual touch pins
  gadget/console.py touch=cal        # force the 3-tap touch calibration on screen
  gadget/console.py wifi=list
Opening the port resets the board on macOS; the script waits for the boot banner before sending."""
import glob, sys, time
try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")
if len(sys.argv) < 2: sys.exit(__doc__)
cmd = sys.argv[1]; wait = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
port = next(iter(sorted(glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*"))), None)
if not port: sys.exit("no board on USB")
s = serial.Serial(None, 115200, timeout=0.2, dsrdtr=False, rtscts=False); s.port = port; s.dtr = False; s.rts = False; s.open()
t0 = time.time(); buf = b""; quiet = 0.0
while time.time() - t0 < 8:
    chunk = s.read(4096)
    if chunk: buf += chunk; quiet = time.time()
    elif quiet and time.time() - quiet > 0.6 and (b"link.app" in buf or b"link.main" in buf): break
s.reset_input_buffer(); s.write((">" + cmd + "\n").encode()); s.flush()
t1 = time.time(); out = b""
while time.time() - t1 < wait: out += s.read(4096)
s.close()
for line in out.decode("utf-8", "replace").splitlines():
    if line.startswith("@"): print(line)

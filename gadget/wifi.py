#!/usr/bin/env python3
"""Teach the gadget a Wi-Fi network over USB, no re-pairing needed (it keeps up to 8 and joins whichever is nearby).

  gadget/wifi.py                 # store every GADGET_WIFI_* network from .env (the board restarts after each), then it joins
  gadget/wifi.py --list          # names of the saved networks (passwords never leave the device)
  gadget/wifi.py --forget NAME   # drop one
  gadget/wifi.py --hidden        # the network does not broadcast its name

Put the credentials in the repo's gitignored .env (never on the command line, which lands in shell history):
  GADGET_WIFI_SSID=...        GADGET_WIFI_PASS=...        # home
  GADGET_WIFI_SSID_2=...      GADGET_WIFI_PASS_2=...      # office; up to _8 (the board keeps eight)
  GADGET_WIFI_HIDDEN_2=1                                  # optional: that network does not broadcast its name
Needs pyserial. Stop anything else holding the port (serial monitor, musecorder serve)."""
import argparse, glob, json, os, sys, time, pathlib

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")

ap = argparse.ArgumentParser()
ap.add_argument("--port", default=None)
ap.add_argument("--list", action="store_true")
ap.add_argument("--forget", metavar="SSID")
ap.add_argument("--hidden", action="store_true")
a = ap.parse_args()

port = a.port or next(iter(sorted(glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*"))), None)
if not port:
    sys.exit("no board on USB")

def talk(cmd, wait_restart):
    """Send one >wifi= line; return the @wifi reply. Opens without toggling DTR/RTS so the board does not reset."""
    s = serial.Serial(None, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
    s.port = port; s.dtr = False; s.rts = False; s.open()
    # macOS asserts DTR on open anyway and the CH340 resets the board: wait for it to come back up
    # (the console task starts early in boot; give it until the log goes quiet or 6 s).
    t0 = time.time(); buf = b""; quiet = 0.0
    while time.time() - t0 < 6:
        chunk = s.read(4096)
        if chunk: buf += chunk; quiet = time.time()
        elif quiet and time.time() - quiet > 0.6 and (b"link.app" in buf or b"link.main" in buf): break
    s.reset_input_buffer()
    s.write((">wifi=" + cmd + "\n").encode()); s.flush()
    t0 = time.time(); buf = b""; reply = None
    while time.time() - t0 < 8:
        buf += s.read(4096)
        for line in buf.decode("utf-8", "replace").splitlines():
            if line.startswith("@wifi"):
                reply = line; break
        if reply: break
    if reply and wait_restart and "restarting" in reply:   # let it reboot before the next network
        t1 = time.time()
        while time.time() - t1 < 10:
            buf += s.read(4096)
            if b"Muse Gadget starting" in buf or b"link.app" in buf[-4000:]: break
    s.close()
    return reply

env = {}
envp = pathlib.Path(__file__).resolve().parent.parent / ".env"
if envp.exists():
    for line in envp.read_text().splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            k, v = line.split("=", 1); env[k.strip()] = v.strip().strip("'\"")
env.update({k: v for k, v in os.environ.items() if k.startswith("GADGET_WIFI_")})

if a.list:
    cmds = [("list", False)]
elif a.forget:
    cmds = [(json.dumps({"forget": a.forget}), False)]
else:
    nets = []
    for n in range(1, 9):
        sfx = "" if n == 1 else f"_{n}"
        ssid = env.get("GADGET_WIFI_SSID" + sfx)
        if not ssid: continue
        nets.append({"ssid": ssid, "password": env.get("GADGET_WIFI_PASS" + sfx, ""),
                     "hidden": a.hidden or env.get("GADGET_WIFI_HIDDEN" + sfx, "0") not in ("", "0", "false", "no")})
    if not nets:
        sys.exit("set GADGET_WIFI_SSID (and GADGET_WIFI_PASS) in .env; GADGET_WIFI_SSID_2 ... _8 for more")
    cmds = [(json.dumps(n), True) for n in nets]

for i, (cmd, wait) in enumerate(cmds):
    reply = talk(cmd, wait)
    if not reply:
        sys.exit("no answer from the board (is it running the gadget build with the wifi console?)")
    label = f"network {i + 1}/{len(cmds)}: " if len(cmds) > 1 else ""
    print(label + reply)
    if not reply.startswith("@wifi ok") and not reply.startswith("@wifi list"):
        sys.exit(1)

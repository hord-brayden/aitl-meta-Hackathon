"""`musecorder` command line: serve, keys, models, doctor, devices, provision, pair, applet, muse-setup."""
from __future__ import annotations
import argparse
import asyncio
import json
import pathlib
import getpass
import logging
import sys

from . import __version__, config, secrets, security
from .session import Shared


def _log_setup(verbose: bool) -> None:
    logging.basicConfig(level=logging.DEBUG if verbose else logging.INFO,
                        format="%(asctime)s %(name)-20s %(levelname)-5s %(message)s", datefmt="%H:%M:%S")
    for noisy in ("httpx", "httpcore", "websockets", "openai", "muse_code"):
        logging.getLogger(noisy).setLevel(logging.WARNING)


async def _serve(args) -> None:
    from .transports.serial_link import SerialTransport
    cfg = config.load()
    shared = Shared(cfg)
    if args.allow_pairing:
        shared.pairing.open(args.allow_pairing)
    await shared.router.refresh()
    ok = [m for m in shared.router.catalog if m.ok]
    print(f"Musecorder bridge {__version__} • {shared.bridge_name}")
    print(f"  models ready: {len(ok)}/{len(shared.router.catalog)}  default: {shared.router.pick_default()}")
    tasks = []
    port = args.port or cfg["bridge"]["serial_port"]
    if port != "off":
        tasks.append(asyncio.create_task(SerialTransport(shared, port, cfg["bridge"]["serial_baud"]).run()))
        print(f"  usb: watching {port}")
    wifi = args.wifi or cfg["bridge"]["wifi"]
    adv = None
    if wifi:
        from .transports.ws_server import WsServer
        ws_port = args.ws_port or cfg["bridge"]["ws_port"]
        mode = cfg["bridge"].get("wifi_mode", "signed")
        tasks.append(asyncio.create_task(WsServer(shared, cfg["bridge"]["bind"], ws_port, mode).run()))
        print(f"  wifi: {'wss' if mode == 'tls' else 'ws'}://{security.BRIDGE_DNS}:{ws_port}/ws  ({mode} mode)")
        if args.allow_pairing:
            print(f"  pairing window open for {args.allow_pairing}s — on the device: Setup > Pair over network")
        if cfg["bridge"].get("discovery"):
            from .discovery import Advertiser
            adv = Advertiser(shared.bridge_name, ws_port, security.cert_fingerprint())
            adv.start()
    if args.ble or cfg["bridge"].get("ble"):
        from .transports.ble_link import BleTransport
        tasks.append(asyncio.create_task(BleTransport(shared).run()))
        print("  ble: scanning for the device (Nordic UART)")
    from .stt import pick as stt_pick, available_engines
    print(f"  speech-to-text: {stt_pick(cfg.get('stt', {}))} (available: {', '.join(available_engines(cfg.get('stt', {}))) or 'none'})")
    from .hooks import HookServer
    shared.hooks = HookServer(shared.broadcast)
    shared.hooks.admin = shared.admin
    tasks.append(asyncio.create_task(shared.now_playing_loop()))
    tasks.append(asyncio.create_task(shared.hooks.run()))
    print("  admin / MCP control plane: http://127.0.0.1:7778 (loopback only)")
    mc = shared.router.muse_code
    if mc is not None:
        from .providers.muse_code_provider import sdk_available
        print(f"  muse code: {mc.muse_bin() or 'not found'} ({'sdk' if sdk_available() else 'exec fallback'})")
    if not tasks:
        print("nothing to do: serial is off and wifi is disabled"); return
    print("  ready. Ctrl-C to stop.\n")
    try:
        await asyncio.gather(*tasks)
    finally:
        if adv:
            adv.stop()
        if mc is not None:
            await mc.close()


def cmd_serve(args) -> None:
    if (args.ble or config.load()["bridge"].get("ble")) and sys.platform == "darwin":
        from . import macapp
        native = pathlib.Path("/Applications/Musecorder Bridge.app")
        if not macapp.in_bundle() and native.exists():
            macapp.ensure_runtime()
            import subprocess
            subprocess.run(["/usr/bin/open", "-a", str(native)])
            print("started /Applications/Musecorder Bridge.app (menu bar). It supervises the bridge; logs: musecorder logs, stop: quit the app.")
            return
        if not macapp.in_bundle():
            log = macapp.launch(sys.argv[1:])
            print("bluetooth mode runs as 'Musecorder Bridge.app' in the background (macOS requires an app for Bluetooth).")
            print(f"  allow the one-time 'Musecorder Bridge would like to use Bluetooth' prompt\n  logs: musecorder logs   (file: {log})\n  stop: musecorder stop")
            return
    _log_setup(args.verbose)
    try:
        asyncio.run(_serve(args))
    except KeyboardInterrupt:
        print("\nbye")


def cmd_keys(args) -> None:
    if args.action == "set":
        if not args.provider:
            sys.exit("usage: musecorder keys set <provider>   (muse | openai | openrouter | <custom>)")
        value = args.value or getpass.getpass(f"{args.provider} API key (input hidden): ")
        if not value.strip():
            sys.exit("empty key")
        print("stored in Keychain" if secrets.set_key(args.provider, value.strip()) else "Keychain write failed")
    elif args.action == "rm":
        print("removed" if secrets.delete(args.provider) else "nothing to remove")
    else:
        cfg = config.load()
        names = [e["name"] for e in cfg["providers"].get("openai_compatible", [])]
        if "muse" not in names:
            names.insert(0, "muse")
        for n in names:
            print(f"  {n:12s} {secrets.source(n)}")


def cmd_models(args) -> None:
    async def run():
        shared = Shared(config.load())
        for m in await shared.router.refresh():
            print(f"  {'●' if m.ok else '○'} {m.id:40s} {m.label:28s} {m.provider}")
    asyncio.run(run())


def cmd_doctor(args) -> None:
    import platform
    from .transports.serial_link import find_port
    from .providers.muse_code_provider import find_muse_bin, sdk_available
    cfg = config.load()
    print(f"Musecorder bridge {__version__} on {platform.platform()} python {platform.python_version()}")
    print(f"  config:  {config.CONFIG_FILE}")
    print(f"  serial:  {find_port(cfg['bridge']['serial_port']) or 'no CYD detected (is it plugged in? CH340 driver?)'}")
    print(f"  tls:     {security.cert_fingerprint()}")
    print(f"  devices: {len(security.Registry().devices)} paired over wifi")
    print(f"  muse:    {find_muse_bin(cfg['providers'].get('muse_code', {}).get('muse_bin', '')) or 'muse CLI not found'}  sdk: {'ok' if sdk_available() else 'missing (pip install muse-code-sdk)'}")
    cmd_keys(argparse.Namespace(action="list"))
    print("  mac:     typing and dictation need Accessibility permission for your terminal app")
    cmd_models(args)


def cmd_devices(args) -> None:
    reg = security.Registry()
    if args.action == "revoke":
        print("revoked" if reg.revoke(args.device_id) else "unknown device")
        return
    for d in reg.devices.values():
        print(f"  {d.id}  {d.name}  via {d.via}  fw {d.fw}")
    if not reg.devices:
        print("  (no wifi-paired devices; USB devices need no pairing)")


async def _provision(args) -> None:
    """Push Wi-Fi + bridge credentials to the device over USB. USB possession == trust."""
    import serial
    from . import protocol
    from .transports.serial_link import find_port
    cfg = config.load()
    port = find_port(args.port or cfg["bridge"]["serial_port"])
    if not port:
        sys.exit("no device on USB")
    ser = serial.Serial(None, cfg["bridge"]["serial_baud"], timeout=0.5, dsrdtr=False, rtscts=False)
    ser.port = port; ser.dtr = False; ser.rts = False; ser.open()
    msgs = []
    if args.ssid:
        pw = args.password if args.password is not None else getpass.getpass("Wi-Fi password: ")
        msgs.append({"t": "prov.wifi", "ssid": args.ssid, "pass": pw})
    if args.bridge:
        reg = security.Registry()
        token = security.new_token()
        host = args.host or security._local_ips()[0]
        msgs.append({"t": "prov.bridge", "host": host, "port": cfg["bridge"]["ws_port"], "token": token,
                     "name": config.bridge_name(cfg), "ca": security.cert_pem()})
        reg.add("usb-provisioned", "Musecorder (usb)", token, via="usb")
        print(f"  bridge host {host}:{cfg['bridge']['ws_port']} + pinned cert + token sent")
    for m in msgs:
        ser.write(protocol.encode(m))
        await asyncio.sleep(0.4)
    deadline = asyncio.get_event_loop().time() + 5
    acks = 0
    while asyncio.get_event_loop().time() < deadline and acks < len(msgs):
        line = ser.readline()
        if line.startswith(b"{"):
            obj = protocol.decode(line)
            if obj and obj.get("t") == "prov.ok":
                acks += 1; print(f"  device acked: {obj.get('what')}")
    ser.close()
    if acks < len(msgs):
        print("  (no ack from device; is `musecorder serve` holding the port? stop it and retry)")


def cmd_provision(args) -> None:
    from .hooks import admin_request
    if getattr(args, "regen_cert", False):
        security.ensure_cert(force=True); print(f"  new certificate: {security.cert_fingerprint()[:23]}…  (restart the bridge app so it serves it)")
    # Prefer the running bridge (it owns the serial port); fall back to opening the port directly.
    if "error" not in admin_request("/admin/status"):
        body = {"bridge": bool(args.bridge), "host": args.host}
        if args.ssid:
            body["ssid"] = args.ssid
            body["password"] = args.password if args.password is not None else getpass.getpass("Wi-Fi password: ")
        res = admin_request("/admin/provision", body, timeout=15)
        if res.get("ok"):
            print(f"  sent via the running bridge: {', '.join(res.get('sent', []))} -> device {', '.join(res.get('devices', []))}")
            print("  the device joins the network now; `musecorder status` or the device's Setup screen shows the result")
        else:
            print(f"  failed: {res.get('error')}")
        return
    asyncio.run(_provision(args))


def cmd_chats(args) -> None:
    from .chat_store import ChatStore
    from . import importers
    store = ChatStore()
    if args.action == "list":
        for c in store.list(50):
            print(f"  {c['id']}  {c['title']}")
        return
    if args.action == "delete":
        print("deleted" if store.delete(args.path) else "not found"); return
    if args.action == "import":
        chat = importers.from_text(pathlib.Path(args.path), store, model=args.model or "", title=args.title or "")
        print(f"  imported {len(chat.messages)} turns -> chat {chat.id} '{chat.title}'")


SETTABLE = {"bleEnabled": "bool", "bleLink": "bool", "wifiEnabled": "bool (restarts the device)", "dictSource": "int (0 host Mac, 1 device mic)",
            "dictTarget": "int (0 ask AI, 1 type into Mac)", "micGain": "int 1-8", "hidTarget": "int (0 bridge, 1 BLE HID)",
            "brightness": "int 10-255", "rotation": "int 0-3", "model": "str", "screenTimeout": "int seconds (0 never)",
            "invertColors": "bool", "bgrOrder": "bool", "longPressMs": "int", "touchDebounce": "int 1-4", "hapticLed": "bool",
            "sdLog": "bool", "dictShortcut": "int 0-3", "reboot": "1", "factoryReset": "1"}


def parse_value(raw: str):
    low = raw.lower()
    if low in ("1", "true", "on", "yes"): return True
    if low in ("0", "false", "off", "no"): return False
    if low.lstrip("-").isdigit(): return int(low)
    try:
        return float(raw) if "." in raw else raw
    except ValueError:
        return raw


def cmd_set(args) -> None:
    from .hooks import admin_request
    if args.key not in SETTABLE:
        sys.exit("settable keys:\n  " + "\n  ".join(f"{k:12s} {v}" for k, v in SETTABLE.items()))
    value = parse_value(args.value)
    if SETTABLE[args.key].startswith("int") and isinstance(value, bool):
        value = int(value)
    res = admin_request("/admin/set", {"key": args.key, "value": value})
    print("ok" if res.get("ok") else f"failed: {res.get('error')}")


def cmd_status(args) -> None:
    from .hooks import admin_request
    res = admin_request("/admin/status")
    if "error" in res:
        sys.exit(res["error"])
    pr = admin_request("/admin/probe")
    if pr.get("keystrokes"):
        print("  mac control: OK (Accessibility granted to the bridge)")
    else:
        print(f"  mac control: BLOCKED - {pr.get('error') or ''}")
        print("    fix: System Settings > Privacy & Security > Accessibility > + > ~/.config/musecorder/Musecorder Bridge.app, then: musecorder stop && musecorder autostart on")
    for d in res.get("devices", []):
        print(f"  {d['id']}  via {d['transport']}  fw {d['fw']}  model {d['model']}")
    if not res.get("devices"):
        print("  no device linked")


# ---------- applets ----------
def _print_result(res: dict) -> None:
    print(json.dumps(res, indent=2, ensure_ascii=False))
    if not res.get("ok", True):
        sys.exit(1)


def cmd_applet(args) -> None:
    from . import applets
    from .hooks import admin_request
    a = args.action
    if a == "validate":
        bad = 0
        for p in args.args or []:
            try:
                doc, _ = applets.load(p)
            except (OSError, applets.AppletError) as e:
                print(f"{p}: {e}"); bad += 1; continue
            errs = applets.validate(doc)
            if errs:
                bad += 1
                print(f"{p}: {len(errs)} error(s)")
                for e in errs:
                    print(f"  - {e}")
            else:
                size = len(applets.compact(doc).encode("utf-8"))
                print(f"{p}: ok ({doc.get('id')}, {size} bytes)")
        sys.exit(1 if bad else 0)
    if a == "install":
        if not args.args:
            sys.exit("usage: musecorder applet install <file.json> [--open]")
        try:
            doc, text = applets.load(args.args[0])
        except (OSError, applets.AppletError) as e:
            sys.exit(str(e))
        errs = applets.validate(doc)
        if errs and not args.force:
            print(f"{args.args[0]}: {len(errs)} error(s) (use --force to send anyway)")
            for e in errs:
                print(f"  - {e}")
            sys.exit(1)
        _print_result(admin_request("/admin/applet/install", {"json": text, "open": bool(args.open)}, timeout=25)); return
    if a == "list":
        _print_result(admin_request("/admin/applet/list")); return
    if a in ("remove", "open"):
        if not args.args:
            sys.exit(f"usage: musecorder applet {a} <id>")
        _print_result(admin_request(f"/admin/applet/{a}", {"id": args.args[0]})); return
    if a == "close":
        _print_result(admin_request("/admin/applet/close", {})); return
    if a == "state":
        if not args.args:
            sys.exit("usage: musecorder applet state <id> [key=value ...]")
        aid, pairs = args.args[0], args.args[1:]
        if not pairs:
            _print_result(admin_request(f"/admin/applet/state?id={aid}")); return
        state = {}
        for kv in pairs:
            k, _, v = kv.partition("=")
            if not _ or not k:
                sys.exit(f"expected key=value, got {kv!r}")
            state[k] = parse_value(v)
        _print_result(admin_request("/admin/applet/state", {"id": aid, "state": state})); return
    if a == "event":
        if len(args.args or []) < 2:
            sys.exit("usage: musecorder applet event <id> <name>")
        _print_result(admin_request("/admin/applet/event", {"id": args.args[0], "name": args.args[1]})); return
    if a == "ui":
        res = admin_request("/admin/applet/ui")
        if res.get("ok") and isinstance(res.get("text"), str):
            print(f"[{res.get('id') or 'no applet'}]"); print(res["text"])
        else:
            _print_result(res)
        return
    if a == "log":
        if not args.args:
            sys.exit("usage: musecorder applet log <id>")
        res = admin_request(f"/admin/applet/log?id={args.args[0]}")
        if res.get("ok"):
            for ln in res.get("lines") or []:
                print(f"  device  {ln}")
            for ln in res.get("bridge_lines") or []:
                print(f"  bridge  {ln}")
            if not (res.get("lines") or res.get("bridge_lines")):
                print("  (empty)")
        else:
            _print_result(res)
        return
    sys.exit(f"unknown applet action {a}")


def cmd_screenshot(args) -> None:
    import base64
    from .hooks import admin_request
    res = admin_request(f"/admin/screenshot?scale={args.scale}", timeout=50)
    if not res.get("ok"):
        sys.exit(res.get("error") or "screenshot failed")
    out = args.out or f"musecorder-{int(__import__('time').time())}.png"
    pathlib.Path(out).write_bytes(base64.b64decode(res["png_base64"]))
    print(f"  {res['w']}x{res['h']} -> {out}")


def cmd_muse_setup(args) -> None:
    from . import muse_setup
    for line in muse_setup.setup(mcp_command=args.mcp_command, skip_skill=args.no_skill, skip_mcp=args.no_mcp):
        print(f"  {line}")


def cmd_mcp(args) -> None:
    from .mcp_server import main as mcp_main
    mcp_main()


def cmd_ble_scan(args) -> None:
    from . import macapp
    if sys.platform == "darwin" and not macapp.in_bundle():
        import time
        out = macapp.LOG.with_name("ble-scan.log")
        out.write_text("")
        macapp.launch(["ble-scan"], log_override=str(out))
        for _ in range(60):
            time.sleep(0.5)
            txt = out.read_text()
            if "scan done" in txt:
                break
        print(out.read_text() or "(no output; is Bluetooth allowed for Musecorder Bridge in System Settings > Privacy & Security > Bluetooth?)")
        return
    from .transports.ble_link import scan_once
    for line in asyncio.run(scan_once(6.0)):
        print(" ", line)
    print("scan done")


def cmd_autostart(args) -> None:
    from . import macapp
    if pathlib.Path("/Applications/Musecorder Bridge.app").exists() and args.action == "on":
        print("Musecorder Bridge.app is installed: use its menu > 'Launch at Login' instead of the LaunchAgent."); return
    if args.action == "status":
        print(macapp.autostart_status())
    else:
        print(macapp.autostart(args.action == "on"))


def cmd_stop(args) -> None:
    from . import macapp
    print("stopped" if macapp.stop() == 0 else "nothing running")


def cmd_logs(args) -> None:
    from . import macapp
    import subprocess
    subprocess.run(["/usr/bin/tail", "-n", str(args.lines), *(["-f"] if args.follow else []), str(macapp.LOG)])


def cmd_install_app(args) -> None:
    from . import macapp
    print(f"installed {macapp.install()}")
    print(f"runtime  {macapp.ensure_runtime(force=True)}")


def cmd_pair(args) -> None:
    args.wifi = True
    args.allow_pairing = args.seconds
    cmd_serve(args)


def main(argv: list[str] | None = None) -> None:
    import os
    if os.environ.get("MUSECORDER_LOG"):
        f = open(os.environ["MUSECORDER_LOG"], "a", buffering=1)
        os.dup2(f.fileno(), 1); os.dup2(f.fileno(), 2)
        sys.stdout = os.fdopen(1, "w", buffering=1); sys.stderr = os.fdopen(2, "w", buffering=1)
    p = argparse.ArgumentParser(prog="musecorder", description="Musecorder bridge — links the Musecorder device to Meta Muse, Muse Code and your Mac")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("-V", "--version", action="version", version=f"musecorder {__version__}")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("serve", help="run the bridge (USB and/or Wi-Fi)")
    s.add_argument("--port", help="serial port, 'auto' or 'off'")
    s.add_argument("--wifi", action="store_true", help="also listen for Wi-Fi devices (TLS WebSocket)")
    s.add_argument("--ws-port", type=int)
    s.add_argument("--ble", action="store_true", help="also link over Bluetooth (Nordic UART; device: Setup > Bluetooth link)")
    s.add_argument("--allow-pairing", type=int, metavar="SECONDS", help="open a Wi-Fi pairing window")
    s.set_defaults(fn=cmd_serve)

    k = sub.add_parser("keys", help="manage provider API keys (macOS Keychain)")
    k.add_argument("action", choices=["list", "set", "rm"])
    k.add_argument("provider", nargs="?", help="muse (Meta Model API) | openai | openrouter | <custom>")
    k.add_argument("value", nargs="?", help="(discouraged on the command line; omit to be prompted)")
    k.set_defaults(fn=cmd_keys)

    sub.add_parser("models", help="list the model catalog").set_defaults(fn=cmd_models)
    sub.add_parser("doctor", help="check ports, keys, TLS, providers, muse CLI").set_defaults(fn=cmd_doctor)

    d = sub.add_parser("devices", help="list / revoke Wi-Fi paired devices")
    d.add_argument("action", nargs="?", default="list", choices=["list", "revoke"])
    d.add_argument("device_id", nargs="?")
    d.set_defaults(fn=cmd_devices)

    pr = sub.add_parser("provision", help="push Wi-Fi and/or bridge credentials to the device over USB")
    pr.add_argument("--port")
    pr.add_argument("--ssid")
    pr.add_argument("--password")
    pr.add_argument("--bridge", action="store_true", help="install this bridge's host, pinned cert and a fresh token")
    pr.add_argument("--host", help="override the host the device should connect to (default: the bridge's mDNS name)")
    pr.add_argument("--regen-cert", action="store_true", help="issue a fresh bridge certificate first (devices must be re-provisioned)")
    pr.set_defaults(fn=cmd_provision)

    ch = sub.add_parser("chats", help="list, delete or import chats (text, markdown, JSON)")
    ch.add_argument("action", choices=["list", "delete", "import"])
    ch.add_argument("path", nargs="?", help="file path or chat id")
    ch.add_argument("--model", help="model the chat should continue with on the device")
    ch.add_argument("--title")
    ch.set_defaults(fn=cmd_chats)

    st = sub.add_parser("set", help="change a device setting through the running bridge (musecorder set bleLink on)")
    st.add_argument("key"); st.add_argument("value")
    st.set_defaults(fn=cmd_set)
    sub.add_parser("status", help="devices linked to the running bridge").set_defaults(fn=cmd_status)

    ap = sub.add_parser("applet", help="push / inspect applets on the device (docs/APPLETS.md)")
    ap.add_argument("action", choices=["install", "list", "remove", "open", "close", "state", "event", "ui", "log", "validate"])
    ap.add_argument("args", nargs="*", help="install <file.json> | remove|open <id> | state <id> [k=v ...] | event <id> <name> | log <id> | validate <file.json> ...")
    ap.add_argument("--open", action="store_true", help="install: open the applet after installing")
    ap.add_argument("--force", action="store_true", help="install: send even when validation fails")
    ap.set_defaults(fn=cmd_applet)

    sc = sub.add_parser("screenshot", help="capture the device screen to a PNG (through the running bridge)")
    sc.add_argument("--scale", type=int, default=1, help="1 = full 320x240 (~5 s), 2 = half size (~1.5 s)")
    sc.add_argument("-o", "--out", help="output file (default musecorder-<time>.png)")
    sc.set_defaults(fn=cmd_screenshot)
    ms = sub.add_parser("muse-setup", help="register the MCP server in ~/.config/muse/settings.json and install the Muse Code skill")
    ms.add_argument("--mcp-command", help="path to musecorder-mcp (default: the one next to this interpreter)")
    ms.add_argument("--no-skill", action="store_true"); ms.add_argument("--no-mcp", action="store_true")
    ms.set_defaults(fn=cmd_muse_setup)
    sub.add_parser("mcp", help="run the MCP server on stdio (same as musecorder-mcp)").set_defaults(fn=cmd_mcp)

    sub.add_parser("install-app", help="(macOS) create the Musecorder Bridge.app bundle used for Bluetooth access").set_defaults(fn=cmd_install_app)
    sub.add_parser("stop", help="stop a background bridge (started with --ble)").set_defaults(fn=cmd_stop)
    sub.add_parser("ble-scan", help="list nearby Bluetooth devices as the bridge sees them (debug)").set_defaults(fn=cmd_ble_scan)
    au = sub.add_parser("autostart", help="start the bridge at login (macOS LaunchAgent) and keep it alive")
    au.add_argument("action", choices=["on", "off", "status"]); au.set_defaults(fn=cmd_autostart)
    lg = sub.add_parser("logs", help="show the background bridge log"); lg.add_argument("-f", "--follow", action="store_true"); lg.add_argument("-n", "--lines", type=int, default=40); lg.set_defaults(fn=cmd_logs)

    pa = sub.add_parser("pair", help="serve with Wi-Fi and a pairing window open")
    pa.add_argument("--seconds", type=int, default=180)
    pa.add_argument("--port"); pa.add_argument("--ws-port", type=int); pa.add_argument("--ble", action="store_true")
    pa.set_defaults(fn=cmd_pair)

    args = p.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    main()

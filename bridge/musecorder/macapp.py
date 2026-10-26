"""macOS app bundle so the bridge may use Bluetooth.

TCC kills any process that touches CoreBluetooth unless its bundle's Info.plist declares
NSBluetoothAlwaysUsageDescription. A bare `python3` has no bundle, so `musecorder serve --ble` re-executes itself through
~/.config/musecorder/Musecorder Bridge.app, whose main executable is a copy of the interpreter. The first run shows the normal
"would like to use Bluetooth" prompt for "Musecorder Bridge"; allow it once.
"""
from __future__ import annotations
import os
import plistlib
import shutil
import site
import sys
from pathlib import Path

from . import config

APP = config.CONFIG_DIR / "Musecorder Bridge.app"
EXE = APP / "Contents" / "MacOS" / "musecorder-bridge"


def _real_python() -> Path:
    return Path(sys.executable).resolve()


def install() -> Path:
    (APP / "Contents" / "MacOS").mkdir(parents=True, exist_ok=True)
    (APP / "Contents" / "Resources").mkdir(parents=True, exist_ok=True)
    plist = {
        "CFBundleName": "Musecorder Bridge", "CFBundleDisplayName": "Musecorder Bridge", "CFBundleIdentifier": "com.musecorder.bridge",
        "CFBundleVersion": "0.2.0", "CFBundleShortVersionString": "0.2.0", "CFBundlePackageType": "APPL",
        "CFBundleExecutable": "musecorder-bridge", "LSBackgroundOnly": True, "LSMinimumSystemVersion": "12.0",
        "NSBluetoothAlwaysUsageDescription": "Musecorder Bridge links your Musecorder device over Bluetooth (Nordic UART) to relay chats, dictation and alerts.",
        "NSMicrophoneUsageDescription": "Not used; dictation audio comes from the device or macOS dictation.",
        "NSAppleEventsUsageDescription": "Musecorder Bridge types text and triggers dictation in the frontmost app on request from your Musecorder device.",
    }
    with (APP / "Contents" / "Info.plist").open("wb") as f:
        plistlib.dump(plist, f)
    _build_launcher(APP / "Contents" / "Info.plist")
    (APP / "Contents" / "PkgInfo").write_text("APPL????")
    # TCC only trusts the usage description of a signed bundle; an ad-hoc signature is enough.
    import subprocess
    subprocess.run(["/usr/bin/codesign", "--force", "--deep", "--sign", "-", str(APP)], capture_output=True)
    subprocess.run(["/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister", "-f", str(APP)], capture_output=True)
    return APP


LAUNCHER_C = r"""
// Musecorder bridge launcher: a native executable with an embedded Info.plist (so TCC accepts Bluetooth use)
// that hosts the Python interpreter in-process instead of exec'ing it.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
  const char* lib = getenv("MUSECORDER_LIBPYTHON");
  if (!lib) { fprintf(stderr, "MUSECORDER_LIBPYTHON not set\n"); return 2; }
  void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
  if (!h) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 2; }
  int (*py_main)(int, char**) = (int (*)(int, char**))dlsym(h, "Py_BytesMain");
  if (!py_main) { fprintf(stderr, "Py_BytesMain not found\n"); return 2; }
  return py_main(argc, argv);
}
"""


def _libpython() -> str:
    import sysconfig
    libdir = sysconfig.get_config_var("LIBDIR") or ""
    ldlib = sysconfig.get_config_var("LDLIBRARY") or ""
    cand = Path(libdir) / ldlib
    if cand.exists():
        return str(cand)
    # framework builds
    fw = Path(sys.base_prefix) / "Python"
    if fw.exists():
        return str(fw)
    raise RuntimeError("could not locate libpython; a shared-library Python build is required for Bluetooth mode")


def _build_launcher(plist: Path) -> None:
    import subprocess
    src = APP / "Contents" / "Resources" / "launcher.c"
    src.write_text(LAUNCHER_C)
    if EXE.exists() and EXE.stat().st_mtime > src.stat().st_mtime - 1 and EXE.stat().st_size < 200000:
        pass
    cmd = ["/usr/bin/clang", "-O2", "-o", str(EXE), str(src), "-Wl,-sectcreate,__TEXT,__info_plist," + str(plist)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("launcher build failed (Xcode command line tools needed):\n" + r.stderr[:400])
    os.chmod(EXE, 0o755)


def in_bundle() -> bool:
    return os.environ.get("MUSECORDER_IN_BUNDLE") == "1"


LOG = config.CONFIG_DIR / "bridge.log"
VENV = config.CONFIG_DIR / "venv"          # a private copy of the bridge outside iCloud Drive / the project tree,
                                            # so the LaunchServices-launched app never needs Files & Folders access


def _project_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def ensure_runtime(force: bool = False) -> Path:
    """Create ~/.config/musecorder/venv and (re)install the bridge into it when the sources are newer."""
    import subprocess
    marker = VENV / ".installed"
    src_mtime = max(f.stat().st_mtime for f in (_project_dir() / "musecorder").rglob("*.py"))
    if not force and marker.exists() and marker.stat().st_mtime >= src_mtime:
        return VENV
    real = _real_python()
    if not (VENV / "bin" / "python3").exists():
        subprocess.run([str(real), "-m", "venv", str(VENV)], check=True)
    pip = [str(VENV / "bin" / "python3"), "-m", "pip", "install", "--quiet", "--upgrade"]
    subprocess.run(pip + ["pip"], check=False, capture_output=True)
    r = subprocess.run(pip + [str(_project_dir()) + "[discovery,ble]"], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("installing the bridge runtime failed:\n" + r.stderr[-600:])
    marker.touch()
    (config.CONFIG_DIR / "project_path").write_text(str(_project_dir()))
    return VENV


def launch(argv: list[str], log_override: str | None = None) -> Path:
    """Start the bridge as its own app through LaunchServices (so TCC attributes Bluetooth use to Musecorder Bridge.app,
    not to the terminal that typed the command). Output goes to ~/.config/musecorder/bridge.log."""
    import subprocess
    install()
    env = _launch_env()
    if log_override:
        env["MUSECORDER_LOG"] = log_override
    cmd = ["/usr/bin/open", "-n", "-a", str(APP)]
    for k, v in env.items():
        cmd += ["--env", f"{k}={v}"]
    cmd += ["--args", "-m", "musecorder.cli", *argv]
    subprocess.run(cmd, check=True)
    return LOG


AGENT_LABEL = "com.musecorder.bridge"
AGENT_PLIST = Path.home() / "Library" / "LaunchAgents" / f"{AGENT_LABEL}.plist"


def _launch_env() -> dict[str, str]:
    venv = ensure_runtime()
    real = _real_python()
    ver = f"python{sys.version_info.major}.{sys.version_info.minor}"
    env = {
        "MUSECORDER_IN_BUNDLE": "1", "MUSECORDER_LOG": str(LOG), "PYTHONUNBUFFERED": "1", "PYTHONNOUSERSITE": "1",
        "PYTHONHOME": str(real.parent.parent), "MUSECORDER_LIBPYTHON": _libpython(),
        "PYTHONPATH": str(venv / "lib" / ver / "site-packages"),
        "HOME": str(Path.home()), "PATH": os.environ.get("PATH", "/usr/local/bin:/usr/bin:/bin"),
    }
    for k in ("GEMINI_API_KEY", "GOOGLE_API_KEY", "ANTHROPIC_API_KEY", "OPENAI_API_KEY", "OPENROUTER_API_KEY", "MUSE_API_KEY", "MUSECORDER_HOME"):
        if os.environ.get(k):
            env[k] = os.environ[k]
    return env


def autostart(enable: bool) -> str:
    """Install / remove a LaunchAgent so the bridge starts at login and is restarted if it dies."""
    import subprocess
    uid = os.getuid()
    if not enable:
        subprocess.run(["/bin/launchctl", "bootout", f"gui/{uid}/{AGENT_LABEL}"], capture_output=True)
        if AGENT_PLIST.exists():
            AGENT_PLIST.unlink()
        return "autostart off"
    install()
    env = _launch_env()
    plist = {
        "Label": AGENT_LABEL, "ProgramArguments": [str(EXE), "-m", "musecorder.cli", "serve"],
        "EnvironmentVariables": env, "RunAtLoad": True, "KeepAlive": True, "ProcessType": "Interactive",
        "StandardOutPath": str(LOG), "StandardErrorPath": str(LOG), "ThrottleInterval": 5,
    }
    AGENT_PLIST.parent.mkdir(parents=True, exist_ok=True)
    with AGENT_PLIST.open("wb") as f:
        plistlib.dump(plist, f)
    subprocess.run(["/bin/launchctl", "bootout", f"gui/{uid}/{AGENT_LABEL}"], capture_output=True)
    r = subprocess.run(["/bin/launchctl", "bootstrap", f"gui/{uid}", str(AGENT_PLIST)], capture_output=True, text=True)
    if r.returncode != 0:
        return f"autostart: launchctl failed: {r.stderr.strip()[:200]}"
    return f"autostart on: {AGENT_PLIST.name} (starts at login, restarts on crash; logs in {LOG})"


def autostart_status() -> str:
    import subprocess
    r = subprocess.run(["/bin/launchctl", "print", f"gui/{os.getuid()}/{AGENT_LABEL}"], capture_output=True, text=True)
    return "autostart on (loaded)" if r.returncode == 0 else ("autostart installed but not loaded" if AGENT_PLIST.exists() else "autostart off")


def stop() -> int:
    import subprocess
    if AGENT_PLIST.exists():   # a KeepAlive agent would just relaunch; unload it for this session
        subprocess.run(["/bin/launchctl", "bootout", f"gui/{os.getuid()}/{AGENT_LABEL}"], capture_output=True)
    r = subprocess.run(["/usr/bin/pkill", "-f", "musecorder.cli.* serve"], capture_output=True)
    r2 = subprocess.run(["/usr/bin/pkill", "-f", "musecorder.cli.* pair"], capture_output=True)
    return 0 if (r.returncode == 0 or r2.returncode == 0) else 1

#!/bin/sh
# Create bridge/.venv and install the bridge in editable mode.
# iCloud Drive sets the "hidden" flag on files it syncs and Python ignores hidden .pth files, which breaks the
# usual editable install when this checkout lives in iCloud; the symlink below keeps `import musecorder` working.
set -e
cd "$(dirname "$0")"
[ -x .venv/bin/python3 ] || python3 -m venv .venv
. .venv/bin/activate
pip install -q --upgrade pip
pip install -q -e ".[dev,discovery]" --config-settings editable_mode=compat
site=$(python -c "import sysconfig; print(sysconfig.get_paths()['purelib'])")
chflags nohidden "$site"/*.pth 2>/dev/null || true
ln -sfn "$PWD/musecorder" "$site/musecorder"
echo "installed: $(musecorder -V)  ($(command -v musecorder))"
echo "next: musecorder doctor · musecorder serve · musecorder muse-setup"

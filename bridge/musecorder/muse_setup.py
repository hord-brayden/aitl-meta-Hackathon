"""`musecorder muse-setup`: register the MCP server with Muse Code and install the project skill for the user.

- ~/.config/muse/settings.json gets an `mcpServers.musecorder` stdio entry (Muse's own `muse mcp --help` names the
  `mcpServers` member; the file must carry `schema_version: 1`). The file is backed up first and other keys are kept.
- The skill at <repo>/.agents/skills/musecorder is copied to ~/.agents/skills/musecorder (Muse imports user skills
  from there), with docs/APPLETS.md refreshed into reference/applets.md.
"""
from __future__ import annotations
import json
import os
import shutil
import sys
import time
from pathlib import Path

SETTINGS = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "muse" / "settings.json"
USER_SKILLS = Path.home() / ".agents" / "skills"
SERVER_NAME = "musecorder"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def skill_source() -> Path:
    return repo_root() / ".agents" / "skills" / SERVER_NAME


def default_mcp_command() -> str:
    cand = Path(sys.executable).parent / "musecorder-mcp"
    if cand.exists():
        return str(cand)
    found = shutil.which("musecorder-mcp")
    return found or str(cand)


def write_settings(command: str, settings: Path = SETTINGS) -> list[str]:
    out: list[str] = []
    cfg: dict = {}
    if settings.exists():
        raw = settings.read_text(encoding="utf-8")
        try:
            cfg = json.loads(raw) if raw.strip() else {}
        except json.JSONDecodeError as e:
            raise RuntimeError(f"{settings} is not valid JSON ({e}); fix it by hand first") from e
        if not isinstance(cfg, dict):
            raise RuntimeError(f"{settings} must contain a JSON object")
        backup = settings.with_name(f"settings.json.bak-{time.strftime('%Y%m%d-%H%M%S')}")
        backup.write_text(raw, encoding="utf-8")
        out.append(f"backed up {settings} -> {backup.name}")
    cfg.setdefault("schema_version", 1)
    servers = cfg.setdefault("mcpServers", {})
    if not isinstance(servers, dict):
        raise RuntimeError("settings.json: mcpServers must be an object")
    entry = {"transport": "stdio", "command": command, "args": []}
    existed = SERVER_NAME in servers
    servers[SERVER_NAME] = entry
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text(json.dumps(cfg, indent=2) + "\n", encoding="utf-8")
    out.append(f"{'updated' if existed else 'added'} mcpServers.{SERVER_NAME} in {settings}: {json.dumps(entry)}")
    return out


def install_skill(dest_root: Path = USER_SKILLS) -> list[str]:
    src = skill_source()
    if not (src / "SKILL.md").exists():
        return [f"skill source not found at {src} (run from the repo checkout)"]
    dest = dest_root / SERVER_NAME
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dest, dirs_exist_ok=True)
    spec = repo_root() / "docs" / "APPLETS.md"
    if spec.exists():
        (dest / "reference").mkdir(exist_ok=True)
        shutil.copyfile(spec, dest / "reference" / "applets.md")
    n = sum(1 for _ in dest.rglob("*") if _.is_file())
    return [f"installed skill {SERVER_NAME} -> {dest} ({n} files)"]


def setup(mcp_command: str | None = None, skip_skill: bool = False, skip_mcp: bool = False) -> list[str]:
    out: list[str] = []
    if not skip_mcp:
        cmd = mcp_command or default_mcp_command()
        if not Path(cmd).exists():
            out.append(f"warning: {cmd} does not exist yet (pip install -e . in the bridge venv creates it)")
        out += write_settings(cmd)
    if not skip_skill:
        out += install_skill()
    out.append("restart Muse Code; then ask it to 'build a counter applet on the Musecorder' (the bridge must be serving)")
    return out

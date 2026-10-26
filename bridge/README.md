# aitl bridge

```
aitl serve [--wifi] [--port auto|/dev/cu.usbserial-110|off] [--allow-pairing 180]
aitl keys  list | set <provider> | rm <provider>       # anthropic gemini openai openrouter muse …
aitl models                                            # catalog with ● ready / ○ missing key
aitl doctor                                            # ports, keys, TLS, providers
aitl provision --ssid NAME [--password …] [--bridge] [--host IP]   # over USB
aitl pair [--seconds 180]                              # serve + Wi-Fi + pairing window
aitl devices [list | revoke <id>]
aitl chats list | import <file> | import-claude-code [latest] [--project X]
aitl hooks install | uninstall | status          # Claude Code permission prompts -> device
aitl set <key> <value>  ·  aitl status           # device settings through the running bridge
aitl serve --ble  ·  aitl logs -f  ·  aitl stop  # Bluetooth mode runs as AITL Bridge.app in the background
aitl install-app                                 # (re)build the app bundle + private runtime in ~/.config/aitl
aitl autostart on | off | status                 # LaunchAgent: bridge at login, kept alive
aitl ble-scan                                    # what the bridge's Bluetooth radio sees (debug)
```

`[stt]` picks the speech engine for device-mic dictation: `engine = "auto"` uses `mlx-whisper` when installed
(`pip install mlx-whisper`, local and free on Apple Silicon), else Gemini with your key.

Config: `~/.config/aitl/config.toml` (created on first run). Edit `[[actions]]` to add one-tap prompts
(`kind = "clipboard"` shows the answer, `"clipboard_type"` also types it into the Mac, `"ask_voice"` dictates a question).
`[providers.openai_compatible]` entries take any OpenAI-style endpoint: set `base_url` and `key_env`.
Meta Muse Spark is pre-declared as `muse`; fill in its `base_url` when Meta publishes an API endpoint.

Providers: `claude-code/*` (Claude Agent SDK, uses your Claude Code login, each AITL chat resumes its own Claude Code session), `claude-*` (Anthropic API key), `gemini-*`, `ollama/*`, `<name>/*` OpenAI-compatible.

Tests: `python -m pytest -q`. Logs: `aitl -v serve`.

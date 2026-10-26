# Demo script (3 minutes) and submission notes

## Setup before recording
- Device on USB, bridge running (`musecorder serve`), `muse login` done, `META_API_KEY` stored (`musecorder keys set muse`).
- `musecorder muse-setup` done once (MCP server + skill registered). Open a terminal in the repo and start `muse`.
- Google Sheet with `examples/sheets/Code.gs` deployed; keep the webhook URL in the clipboard.
- Device on the home screen, nothing installed (`musecorder applet remove <id>` for leftovers).

## Beats
1. **What it is (20 s).** Show the device: chat with Muse Spark from the touchscreen, the model chip, a streamed answer.
2. **The ask (10 s).** In Muse Code: *"I'm counting people through door A at a game. Make the Musecorder a clicker with a
   big number, +1/−1, and post the tally to this webhook every 5 minutes and when I tap Post: <url>."*
3. **Muse works (40 s).** Narrate what the terminal shows: it reads the skill, writes the applet JSON, calls
   `applet_install`, then `applet_screen` to read the screen back, maybe fixes a label. The device changes in front of
   the camera (~2 s after the tool call).
4. **Use it (30 s).** Tap +1 a few times, tap Post, show the Google Sheet row appear with the device id and count.
   Unplug the cable: the applet keeps running (state persists; with Wi-Fi provisioned it posts on its own).
5. **Iterate (30 s).** *"Also count falling edges on GPIO 22 as entries and beep on every tenth person."* Muse edits the
   applet and reinstalls; short a jumper from GND to IO22 to show the count rising.
6. **Muse Code on the device (30 s).** Open CHATS ▸ Muse Code sessions, resume one, ask it to run the tests; the
   permission card appears on the device; tap Allow.
7. **Close (20 s).** The pitch line: "a tricorder Muse rewrites for the job in front of you; when JSON isn't enough,
   Muse rewrites the firmware itself (show `AGENTS.md`)."

## Submission checklist (due 2026-11-04, 9:00 PM ET)
- Repo public or shared with judges; README with the diagram and the captured screenshots.
- 3-minute video following the beats above; subtitles for the terminal parts.
- One paragraph on Muse usage: Model API (`muse-spark-1.3`) for chat, Muse Code SDK (MSP) for sessions and approvals,
  Muse Code + MCP + skill for recoding the device.
- Hardware list: ESP32-2432S024C, USB-C cable, optional INMP441 mic, jumper wire for the GPIO demo.

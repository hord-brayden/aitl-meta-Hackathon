# Demo script (3 minutes) and submission notes

Gadget mode is the story: the phone, the device, nothing else on the table. The Mac-bridge personality gets one
beat at the end as "and when you do have a laptop".

## Setup before recording
- 4" board flashed with the gadget build (`BOARD=cyd40 gadget/build.sh` then `gadget/flash.sh`), paired, green bars,
  the Muse sprite idling. Wi-Fi provisioned so it reconnects on its own. USB only for power.
- Muse app open on the phone on the chat with your Muse; Developer mode on; the gadget shows "last seen now" in
  Settings > Devices.
- `examples/gadget/*.json` handy in the phone's clipboard manager (or just describe the applet; Muse writes it).
- Touch calibrated once (launcher > Touch setup, three taps) so buttons land where they are drawn.
- Optional props: a jumper from GND to IO35 (voltmeter beat), a 3.3 V I²C breakout on 21/22 (scanner beat).

## Beats
1. **What it is (15 s).** The device idle: Meta's status bars, your own Muse sprite. "This is a Muse gadget. It pairs
   with my Muse from the app. Nothing on it is hard-coded for a job."
2. **The ask (10 s).** In the Muse app: *"Install an applet on my gadget that counts people through door A: a big
   number, +1 / −1 / reset buttons, beep every tenth person, and tell me the tally every five minutes."*
3. **Muse works (30 s).** Muse writes the JSON and calls `applet.install`. The screen changes on camera about two
   seconds after Muse says it is done. Tap +1 a few times; show the beep on the tenth; show the bar move.
4. **It talks back (20 s).** Tap "Post to Muse": a message from the gadget lands in the chat and Muse replies. "The
   device can push into my chat; Muse can read its state any time with applet.state."
5. **Switch jobs without touching a cable (30 s).** *"Make it a sketch pad with cyan and pink."* Draw with a finger.
   Then tap the title bar: the launcher lists Door Tally and Sketch; tap Door Tally; the count is still there
   (state persists per applet in its own flash partition).
6. **Hardware is reachable (30 s).** *"Make it a voltmeter on pin 35 that reports the peak every minute."* Touch the
   jumper to 3V3 and GND, watch the bar. Or: *"Scan the I²C bus on 21 and 22 and show what you find."*
7. **Why it cannot brick itself (10 s).** "Applets are data. The firmware image is never written; the store is a
   separate partition; `applet.remove` or the launcher always gets me back to the status screen."
8. **Laptop beat (15 s, optional).** Flash the bridge personality: Muse Code sessions and tool approvals on the same
   touchscreen, Muse Code driving the LVGL applet engine through MCP.
9. **Close (10 s).** "A tricorder Muse rewrites for the job in front of you, over the account you already pay for."

## Talking points for the judges
- **Creativity:** the device programs itself through the assistant you already talk to; applets are a 4 KB JSON
  dialect designed so a language model writes them correctly on the first try (the whole spec rides in the
  command description Muse receives at `link.register`).
- **Technical ambition:** Meta's ESP-IDF gadget firmware ported to a $15 classic ESP32 with no PSRAM (session
  buffers, TLS and Wi-Fi queues trimmed until pairing fits in ~30 KB free), a C++ applet runtime with an expression
  evaluator inside that firmware, touch on the shared LCD SPI bus, the device posting into the Muse chat over the
  same Noise session Meta's own boards use, and a bit-exact port of your Muse avatar into Meta's sprite format.
- **Does it work:** paired, listed in the app, every command invoked from the phone, applets installed and
  persisted, buttons and canvas on the real touchscreen, sound, ADC, I²C, chat push with a Muse reply.

## Known limits (say them before the judges find them)
- No PSRAM on this board: HTTPS from an applet and `display.draw_url` with https fail for lack of a 17 KB block;
  plain http on the LAN works. Muse's chat *reply* is read back onto the screen only on PSRAM boards (`muse.ask`);
  here it posts only.
- The VM refuses `/fs/write` and `/identity` for this device credential (`403 route not allowed for this
  credential's scopes`), so `vm.write` is blocked; `muse.say` and `applet.state` cover the same ground.
- Five stored applets, 4 KB each.

## Submission checklist (due 2026-11-04, 9:00 PM ET)
- [ ] Repo public (or shared with the judges' account) with this README and the photos in `docs/img/gadget-*.jpg`.
- [ ] 3-minute video following the beats above; subtitles for the chat text.
- [ ] One paragraph on Muse usage: Muse Gadget SDK (pairing, control session, `link.register` commands, chat route),
      Muse app as the only UI, Muse Code + MCP + skill in bridge mode.
- [ ] Hardware list: Hosyond 4.0" ESP32-32E (or ESP32-2432S024C), USB-C cable, jumper wire, optional I²C breakout.

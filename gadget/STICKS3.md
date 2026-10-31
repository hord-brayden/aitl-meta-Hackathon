# M5Stack StickS3 — Musecorder capability & safety scaffold

A reference for what Muse (and you) can safely make this gadget do, the exact
parameters, and the hard limits — so an applet can't brick it. The StickS3 runs
the **Musecorder/Link stack** (`BOARD=sticks3`, `MUSE_ENABLED` off), pairs in the
normal Muse app as **`MuseGadget-Stick`**, and Muse installs JSON applets over the
link exactly like the CYD boards. This board is a personal playground — it is **not**
part of the hackathon submission (that stays the CYD43 + CYD4).

## The board
- **SoC:** ESP32-S3-PICO-1-N8R8 — 8 MB flash, **8 MB octal PSRAM** (RAM is roomy, unlike the no-PSRAM CYDs).
- **Screen:** 1.14" ST7789, **135 × 240, portrait, no touch.**
- **Buttons:** blue/front (KEY1, GPIO11) and side (KEY2, GPIO12). Left = power/download, not app-usable.
- **IR:** transmitter on **GPIO46**, receiver on GPIO42. The IR LED is on the PMIC's switchable **5 V rail**.
- **IMU:** **BMI270** 6-axis (accel+gyro) on the internal I²C bus at **0x68**.
- **Audio:** MEMS mic + **ES8311** codec (I²S) + **AW8737** 1 W amp (8 Ω speaker).
- **Power:** M5PM1 PMIC at I²C **0x6E** (owns the LCD/audio rail, the amp, the 5 V-out, battery/USB voltage, green LED). 250 mAh battery.

## Build & flash (known-good)
```sh
BOARD=sticks3 gadget/build.sh
# flash from ~/esp/muse-gadget-sdk/esp32/build-sticks3 with the IDF-env esptool:
#   python -m esptool --chip esp32s3 -p /dev/cu.usbmodem* -b 460800 \
#     --before default-reset --after watchdog-reset write-flash "@flash_args"
```
`--after watchdog-reset` boots the app cleanly on macOS (plain hard-reset re-enters
download mode). The board advertises a **random BLE address each boot**, so it
re-appears as a *new* device after a reflash/reboot and must be re-paired. (Ask if
you want a stable address so it stays paired — the reason it was random is now fixed.)

## What an applet CAN do today
Install with `applet.install` (JSON ≤ 4 KB). All the standard verbs work; the
StickS3-relevant ones:

| Area | Verbs | Notes on this board |
|---|---|---|
| **IR remote** | `ir.nec{addr,cmd,pin}`, `ir.raw{us:[mark,space,…],pin}` | **New.** 38 kHz carrier. `pin` defaults to **46** here; the 5 V rail powers on automatically. `ir.raw` takes 1–256 microsecond durations, alternating mark/space, starting with a mark. |
| **Screen** | `draw.rect`, `draw.text`, `clear`, screen items (`text`,`big`,`bar`,`buttons`,`canvas`) | 135 × 240 portrait. Keep text ≤ scale 2 for width; the idle auto-fits. |
| **Buttons** | events `boot:press` (blue), `btn:down` (side) | No touch — drive menus/actions from these two events. |
| **GPIO/PWM/ADC** | `gpio.write/read`, `pwm`, `adc.read`, `gpio:PIN:rise\|fall\|change` watches | **Only on free pins — see the pin table below.** Most GPIOs are taken. |
| **I²C (external)** | `i2c.scan/read/write{addr,reg,…,sda,scl}` | Use the **Grove port** pins with the 5 V rail on. **Do not** target the internal bus (sda 47 / scl 48) — the display already owns it (see below). |
| **Muse / net** | `muse.say`, `muse.ask`, `http`, `vm.write` | Same as the CYDs. `http` uses little RAM; still keep payloads small. |
| **Logic** | `set/inc/dec/toggle`, `if`, `every:SECONDS`, `log`, `exit`, `${expr}` | — |

### Example: a TV power applet
```json
{"id":"tv","name":"TV Remote",
 "screen":[{"text":"TV"},{"button":"Power","event":"pwr"}],
 "on":{"pwr":[{"ir.nec":{"addr":0,"cmd":2}}],
       "boot:press":[{"ir.nec":{"addr":0,"cmd":2}}]}}
```
(NEC address/command vary by TV; capture them from the remote or try a code set.)

## Hardware that is present but NOT yet bridged to applets
Document these so Muse doesn't write applets that silently fail:
- **IMU (BMI270 @ 0x68):** sits on the **internal** I²C bus (sda 47 / scl 48), which the
  display driver holds. Applet `i2c.*` opens its *own* bus and must not fight that one,
  so the IMU is not reachable from an applet yet. BMI270 also needs a ~8 KB config
  upload before it reports motion. → needs a native `imu.read` verb (roadmap).
- **Microphone / speaker (ES8311 + I²S):** there is no audio verb in the applet
  engine, and `tone` expects a simple buzzer this board doesn't have. → needs a
  native audio path (roadmap).
- **IR receive (GPIO42):** only transmit is wired. Decoding needs the RMT RX path,
  and per M5 the **amp must be off** or it swamps the receiver. → roadmap.
- **BLE HID keyboard:** this is a **separate boot mode** (Settings → *HID keyboard
  mode*, which reboots), not an applet action. In HID mode the device is a keyboard,
  not a Muse gadget; toggle back to return to Musecorder.

## Memory & storage limits
- **Applet size ≤ 4 KB**, **≤ 5 applets**, event lists bounded (≤ 8 timers, ≤ 4 gpio watches).
- **No persistence on this board.** There is no `applets` NVS partition and no SD card,
  so applets **run live but do not survive a reboot** — nothing is stored, and
  `persist` / saved state are no-ops. Re-install after a power cycle. (A custom
  partition table could add storage later; not done, to keep pairing data untouched.)
- **8 MB PSRAM** means RAM is comfortable, but still: keep allocations modest, keep
  `http` responses small, and **never block** the loop (the UI and link run on it).

## Pin safety — do not brick it
**Never** `gpio.write`/`pwm` these (they are wired to hardware or boot straps):

| Pins | Why — leave alone |
|---|---|
| **26–37** | **Octal flash + PSRAM.** Touching these = instant brick. |
| 21, 38, 39, 40, 41, 45 | LCD (RST, backlight, MOSI, SCLK, CS, DC) |
| 47, 48 | Internal I²C — **PMIC (0x6E), BMI270 (0x68), ES8311.** Wrong PMIC writes can cut power. |
| 14, 15, 16, 17, 18 | I²S audio |
| 11, 12 | Buttons (the UI uses them) |
| 46, 42 | IR TX / RX |
| 0, 3 | Boot strapping |
| 19, 20 | Native USB (serial/JTAG) |

**Safe for applets:** the **Grove / HY2.0-4P port** (two GPIOs broken out — check the
[StickS3 pinout](https://docs.m5stack.com/en/core/StickS3) for the exact numbers on
your unit) once the 5 V rail is on. Free GPIO is otherwise scarce on this board — the
real I/O surface is the **screen, the two buttons, and IR.** When in doubt, run
`i2c.scan` / read before writing, and never write a pin from the table above.

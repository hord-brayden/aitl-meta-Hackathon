# Screen map (320×240 landscape by default; every layout is flex-based and also works in portrait)

```
BOOT        orb · "Musecorder" · tagline · quiet boot log
SETUP       Welcome (orb, "Hi, I'm your Muse device.") → touch calibration → orientation → colors
HOME        top bar: orb (status: breathing = idle, fast = thinking, amber = needs you, grey = offline) · wordmark · bell · link pill
            chips: model · chat (hold = new chat) · page up/down
            conversation: user messages in right-aligned bubbles, Muse replies plain with an orb dot; floating stop/copy/paste
            dock: Dictate · Ask · Apps · Settings
MODELS      grouped by provider, gradient dot marks the current one
CHATS       New chat (gradient pill) · chats · Muse Code sessions; hold a chat to delete
APPS        2-column tiles with gradient badges; "Back to <app>" when one runs in the background; "Mac actions" pill
APP         a running applet: back · name · ☰ (launcher) · the applet's widgets (docs/APPLETS.md). Hold BOOT 3 s to leave
MAC ACTIONS one-tap prompts from config.toml + media / key tiles
ASK         textarea + keyboard; top row: ✕ · mic · Ask ▸ · Mac ▸
SETTINGS    grouped cards: Connection · Bluetooth · Voice · Display · Touch · Apps · Storage · About
PAIR        bridge name · 6-digit code · certificate fingerprint · They match / Reject
CALIBRATE   3 rings (hold each 0.65 s) → "Check it" verify screen with a dot under your finger → Looks right / Redo
TOUCH TEST  draw on a grid; raw + mapped coordinates; Recalibrate / Clear / Done
```

Design language (`firmware/src/ui/theme.h`): a Muse-flavoured look. Charcoal surfaces (#0f1117 page, #171a23 cards,
#1f2330 pills), text #f3f4f8 / muted #9aa3b5, one iridescent accent (teal #2ee6d6 → magenta #e879f9) used only on the orb,
primary pills, badges and progress indicators; success #34d399, warning #fbbf24, danger #fb7185. Cards radius 16, actions
are full pills, tiles radius 12–14, no borders (elevation by lighter surface), hairlines only between rows. Montserrat
12/14/16/18/20/28 + 48 for applet numerals; sentence case everywhere. The orb (`theme::orb`) is the signature element: a
gradient circle with a breathing halo that doubles as the device's status. Touch targets ≥ 34 px.

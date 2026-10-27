#!/usr/bin/env python3
"""Port Meta's muse-gadget-sdk ESP32 firmware to the Sunton CYD 2.4" (ILI9341). Idempotent: run it on a fresh clone
or again after `git pull` inside the SDK. Usage: python3 gadget/port/apply.py gadget/sdk/esp32"""
import pathlib, shutil, sys, re

root = pathlib.Path(sys.argv[1]).resolve()
here = pathlib.Path(__file__).resolve().parent
MARK = "HOMEHUB_LED_BACKEND_CYD_ILI9341"

def edit(path: pathlib.Path, pairs, must=True):
    s = path.read_text(encoding="utf-8"); orig = s
    for old, new in pairs:
        if old not in s:
            if must: sys.exit(f"apply.py: pattern not found in {path.name}: {old[:70]!r} (SDK changed; update gadget/port/apply.py)")
            continue
        s = s.replace(old, new, 1)
    if s != orig:
        path.write_text(s, encoding="utf-8"); print("  patched", path.relative_to(root))

shutil.copy(here / "sdkconfig.cyd24", root / "devices" / "sdkconfig.cyd24")
shutil.copy(here / "sdkconfig.cyd40", root / "devices" / "sdkconfig.cyd40")
shutil.copy(here / "partitions_cyd24_4mb.csv", root / "partitions_cyd24_4mb.csv")
print("  copied devices/sdkconfig.cyd24, devices/sdkconfig.cyd40, partitions_cyd24_4mb.csv")

k = root / "main" / "Kconfig.projbuild"
if MARK not in k.read_text():
    edit(k, [
        ('        config HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n            bool "Waveshare ESP32-C6-LCD-1.47 inch ST7789 display (status bars)"\n            depends on IDF_TARGET_ESP32C6\n',
         '        config HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n            bool "Waveshare ESP32-C6-LCD-1.47 inch ST7789 display (status bars)"\n            depends on IDF_TARGET_ESP32C6\n\n        config HOMEHUB_LED_BACKEND_CYD_ILI9341\n            bool "Sunton CYD 2.4/2.8 inch ILI9341 display (status bars)"\n            depends on IDF_TARGET_ESP32\n\n        config HOMEHUB_LED_BACKEND_CYD_ST7796\n            bool "Hosyond 4.0 inch ST7796 display (status bars)"\n            depends on IDF_TARGET_ESP32\n'),
        ('        default y if HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n', '        default y if HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n        default y if HOMEHUB_LED_BACKEND_CYD_ILI9341\n        default y if HOMEHUB_LED_BACKEND_CYD_ST7796\n'),
    ])

c = root / "main" / "led_status.c"
if MARK not in c.read_text():
    s = c.read_text(encoding="utf-8")
    both = "CONFIG_HOMEHUB_LED_BACKEND_IDEASPARK_ST7789 || CONFIG_HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789"
    n = s.count(f"#if {both}")
    s = s.replace(f"#if {both}", f"#if {both} || CONFIG_{MARK} || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796")
    s = s.replace('#include "esp_lcd_panel_st7789.h"\n', f'#include "esp_lcd_panel_st7789.h"\n#if CONFIG_{MARK}\n#include "esp_lcd_ili9341.h"\n#endif\n#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n#include "esp_lcd_st7796.h"\n#endif\n', 1)
    block = f'''#elif CONFIG_{MARK}
// Sunton ESP32-2432S024C / 2432S028: 240x320 ILI9341 on HSPI, backlight on GPIO27, reset tied to EN.
#define LCD_NAME         "CYD ILI9341"
#define LCD_HOST         SPI2_HOST
#define LCD_PIN_SCLK     14
#define LCD_PIN_MOSI     13
#define LCD_PIN_CS       15
#define LCD_PIN_DC       2
#define LCD_PIN_RST      -1
#define LCD_PIN_BL       27
#define LCD_PCLK_HZ      (40 * 1000 * 1000)
#define LCD_H_RES        240
#define LCD_V_RES        320
#define LCD_X_GAP        0
#define LCD_BAR_ROWS     12
#define LCD_ANIM_SCALE   4
#define LCD_DOT_MARGIN   6
#define LCD_BUF_CAPS     MALLOC_CAP_DMA
#elif CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796
// Hosyond 4.0" ESP32-32E: 320x480 ST7796 on HSPI, backlight GPIO27, reset tied to EN, panel needs inversion.
#define LCD_NAME         "CYD ST7796"
#define LCD_HOST         SPI2_HOST
#define LCD_PIN_SCLK     14
#define LCD_PIN_MOSI     13
#define LCD_PIN_CS       15
#define LCD_PIN_DC       2
#define LCD_PIN_RST      -1
#define LCD_PIN_BL       27
#define LCD_PCLK_HZ      (40 * 1000 * 1000)
#define LCD_H_RES        320
#define LCD_V_RES        480
#define LCD_X_GAP        0
#define LCD_BAR_ROWS     16
#define LCD_ANIM_SCALE   4
#define LCD_DOT_MARGIN   8
#define LCD_BUF_CAPS     MALLOC_CAP_DMA
#endif

#if CONFIG_HOMEHUB_DISPLAY'''
    old = "#define LCD_BUF_CAPS     MALLOC_CAP_DMA\n#endif\n\n#if CONFIG_HOMEHUB_DISPLAY"
    if old not in s: sys.exit("apply.py: backend block anchor not found in led_status.c")
    s = s.replace(old, "#define LCD_BUF_CAPS     MALLOC_CAP_DMA\n" + block, 1)
    old = "    if (err == ESP_OK) err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel);"
    new = f"#if CONFIG_{MARK}\n    if (err == ESP_OK) err = esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel);\n#elif CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n    if (err == ESP_OK) err = esp_lcd_new_panel_st7796(io, &panel_cfg, &s_panel);\n#else\n    if (err == ESP_OK) err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel);\n#endif"
    if old not in s: sys.exit("apply.py: panel creation anchor not found")
    s = s.replace(old, new, 1)
    old = "    if (err == ESP_OK) err = esp_lcd_panel_invert_color(s_panel, true);"
    new = f"#if CONFIG_{MARK} || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n    if (err == ESP_OK) err = esp_lcd_panel_invert_color(s_panel, false);   // CYD panels are not inverted\n#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n    if (err == ESP_OK) err = esp_lcd_panel_mirror(s_panel, true, false);   // the Hosyond 4-inch panel scans right to left\n#endif\n#else\n    if (err == ESP_OK) err = esp_lcd_panel_invert_color(s_panel, true);\n#endif"
    if old not in s: sys.exit("apply.py: invert anchor not found")
    s = s.replace(old, new, 1)
    old = "        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,"
    new = f"#if CONFIG_{MARK} || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,   // CYD panels take BGR; flip if orange shows as blue\n#else\n        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,\n#endif"
    if old not in s: sys.exit("apply.py: rgb order anchor not found")
    s = s.replace(old, new, 1)
    c.write_text(s, encoding="utf-8"); print(f"  patched main/led_status.c ({n} backend guards widened)")

y = root / "main" / "idf_component.yml"
if "esp_lcd_ili9341" not in y.read_text():
    edit(y, [('  espressif/led_strip: "~3.0"\n', '  espressif/led_strip: "~3.0"\n  espressif/esp_lcd_ili9341: "^2"\n  espressif/esp_lcd_st7796: "^1"\n')])
print("port applied")

# --- CYD: random static BLE address (phones cache the GATT table per address; this radio was a BLE keyboard under Musecorder) ---
b = root / "main" / "ble_server.c"
t = b.read_text()
if "CYD_RANDOM_ADDR" not in t:
    t = t.replace("    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,",
                  "#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ILI9341 || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796   /* CYD_RANDOM_ADDR */\n    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER,\n#else\n    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,\n#endif", 1)
    t = t.replace("    s_synced = true;\n    start_advertising();",
                  "#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ILI9341 || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n    { ble_addr_t ra; if (ble_hs_id_gen_rnd(1, &ra) == 0 && ble_hs_id_set_rnd(ra.val) == 0) ESP_LOGI(TAG, \"random static addr %02x:%02x:%02x:%02x:%02x:%02x\", ra.val[5], ra.val[4], ra.val[3], ra.val[2], ra.val[1], ra.val[0]); else ESP_LOGW(TAG, \"random addr failed; using public\"); }\n#endif\n    s_synced = true;\n    start_advertising();", 1)
    b.write_text(t); print("  patched main/ble_server.c (random static address on CYD boards)")

# --- CYD: small control-session buffers (no PSRAM: the default 81 KB of session buffers never fit beside TLS -> "session buffer alloc failed") ---
n = root / "main" / "noise_control.cpp"
t = n.read_text()
if "CYD_SMALL_SESSION" not in t:
    t = t.replace("#if SMALL_CONTROL_SESSION && (CONFIG_MUSE_BOARD_M5STACK_CARDPUTER_ADV || CONFIG_MUSE_BOARD_AI_PASSPORT)\n#define CARDPUTER_CONTROL_SESSION 1",
                  "#if (SMALL_CONTROL_SESSION && (CONFIG_MUSE_BOARD_M5STACK_CARDPUTER_ADV || CONFIG_MUSE_BOARD_AI_PASSPORT)) || CONFIG_HOMEHUB_LED_BACKEND_CYD_ILI9341 || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796   /* CYD_SMALL_SESSION */\n#define CARDPUTER_CONTROL_SESSION 1", 1)
    t = t.replace("#if SMALL_CONTROL_SESSION && CONFIG_MUSE_BOARD_AI_PASSPORT\n#define WS_RX_BUF_SIZE (SVC_FRAME_SCRATCH + 1024)",
                  "#if (SMALL_CONTROL_SESSION && CONFIG_MUSE_BOARD_AI_PASSPORT) || CONFIG_HOMEHUB_LED_BACKEND_CYD_ILI9341 || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n#define WS_RX_BUF_SIZE (SVC_FRAME_SCRATCH + 1024)", 1)
    if "CYD_SMALL_SESSION" not in t: sys.exit("apply.py: noise_control anchors not found")
    n.write_text(t); print("  patched main/noise_control.cpp (small control-session buffers on CYD boards)")

# --- CYD: your Muse as the status-screen sprite (gadget/port/avatar/happy_anim.c/.h from gadget/port/gen_avatar_anim.py) ---
av = pathlib.Path(__file__).resolve().parent / "avatar"
for name in ("happy_anim.c", "happy_anim.h"):
    srcf = av / name
    if srcf.exists():
        dst = root / "avatar" / name
        if not dst.exists() or dst.read_bytes() != srcf.read_bytes():
            dst.write_bytes(srcf.read_bytes()); print(f"  copied avatar/{name} (your Muse's sprite)")

# --- CYD: Musecorder applet runtime (gadget/port/musecorder -> main/, commands applet.*) ---
mc = pathlib.Path(__file__).resolve().parent / "musecorder"
for name in ("musecorder.cpp", "musecorder.h"):
    srcf = mc / name; dst = root / "main" / name
    if not dst.exists() or dst.read_bytes() != srcf.read_bytes():
        dst.write_bytes(srcf.read_bytes()); print(f"  copied main/{name}")
cm = root / "main" / "CMakeLists.txt"; t = cm.read_text()
if '"musecorder.cpp"' not in t:
    t = t.replace('    "app.c"\n', '    "app.c"\n    "musecorder.cpp"\n', 1); cm.write_text(t); print("  patched main/CMakeLists.txt (musecorder.cpp)")
kc = root / "main" / "Kconfig.projbuild"; t = kc.read_text()
if "HOMEHUB_MUSECORDER" not in t:
    t = t.replace("    config HOMEHUB_SENSECAP_SENSORS\n", (
        "    config HOMEHUB_MUSECORDER\n"
        "        bool \"Musecorder applet runtime (applet.install/remove/state/event)\"\n"
        "        depends on HOMEHUB_LED_BACKEND_CYD_ILI9341 || HOMEHUB_LED_BACKEND_CYD_ST7796\n"
        "        default y\n"
        "        help\n"
        "            Lets Muse install small JSON tools (applets) on the gadget over the control\n"
        "            session: state, timers, BOOT button, GPIO, HTTP, writes into the VM's files and\n"
        "            a text screen on the status display. See main/musecorder.h.\n"
        "\n"
        "    config HOMEHUB_SENSECAP_SENSORS\n"), 1)
    kc.write_text(t); print("  patched main/Kconfig.projbuild (HOMEHUB_MUSECORDER)")
nc = root / "main" / "noise_control.cpp"; t = nc.read_text()
if "MUSECORDER_INSTALL_DESC" not in t:
    t = t.replace('#include "noise_control.h"\n', '#include "noise_control.h"\n#if CONFIG_HOMEHUB_MUSECORDER\n#include "musecorder.h"\n#endif\n', 1)
    reg = (
        "#if CONFIG_HOMEHUB_MUSECORDER\n"
        "    {\n"
        "        cJSON *json_required = cJSON_CreateObject();\n"
        "        cJSON_AddItemToObject(json_required, \"json\", string_param(\"The applet, as a JSON object or a JSON string.\"));\n"
        "        add_command(commands, \"applet.install\", MUSECORDER_INSTALL_DESC, json_required, nullptr);\n"
        "        add_command(commands, \"applet.remove\", MUSECORDER_REMOVE_DESC, nullptr, nullptr);\n"
        "        cJSON *set_optional = cJSON_CreateObject();\n"
        "        cJSON *set_param = cJSON_CreateObject();\n"
        "        cJSON_AddStringToObject(set_param, \"type\", \"object\");\n"
        "        cJSON_AddStringToObject(set_param, \"description\", \"State keys to assign before reading, e.g. {\\\"count\\\": 0}.\");\n"
        "        cJSON_AddItemToObject(set_optional, \"set\", set_param);\n"
        "        add_command(commands, \"applet.state\", MUSECORDER_STATE_DESC, nullptr, set_optional);\n"
        "        cJSON *name_required = cJSON_CreateObject();\n"
        "        cJSON_AddItemToObject(name_required, \"name\", string_param(\"The event name (a key under the applet's on object).\"));\n"
        "        add_command(commands, \"applet.event\", MUSECORDER_EVENT_DESC, name_required, nullptr);\n"
        "    }\n"
        "#endif\n"
        "#if CONFIG_HOMEHUB_VOICE\n")
    t = t.replace("#if CONFIG_HOMEHUB_VOICE\n", reg, 1)
    t = t.replace('    ESP_LOGI(TAG, "link.register acked; tunnel may open");',
                  '    ESP_LOGI(TAG, "link.register acked; tunnel may open");\n#if CONFIG_HOMEHUB_MUSECORDER\n    musecorder_vm_connected();\n#endif', 1)
    if "musecorder_vm_connected" not in t: sys.exit("apply.py: register-ack anchor not found")
    nc.write_text(t); print("  patched main/noise_control.cpp (applet.* registered)")
ap = root / "main" / "app.c"; t = ap.read_text()
if "musecorder_command" not in t:
    t = t.replace('#include "button.h"\n', '#include "button.h"\n#if CONFIG_HOMEHUB_MUSECORDER\n#include "musecorder.h"\n#endif\n', 1)
    t = t.replace("static void on_button_short_press(void) {\n    factory_test_on_button_press();\n",
                  "static void on_button_short_press(void) {\n    factory_test_on_button_press();\n#if CONFIG_HOMEHUB_MUSECORDER\n    if (musecorder_on_tap()) return;   // a running applet owns the BOOT tap\n#endif\n", 1)
    t = t.replace("    noise_ctrl_session_generation_t session_generation) {\n#if CONFIG_HOMEHUB_BLE_COMMANDS\n    if (ble_commands_handles(command)) {",
                  "    noise_ctrl_session_generation_t session_generation) {\n#if CONFIG_HOMEHUB_MUSECORDER\n    {\n        cJSON *mc = musecorder_command(command, params);\n        if (mc) return mc;\n    }\n#endif\n#if CONFIG_HOMEHUB_BLE_COMMANDS\n    if (ble_commands_handles(command)) {", 1)
    t = t.replace("        ESP_LOGW(TAG, \"button init failed — physical setup reset unavailable\");\n    }",
                  "        ESP_LOGW(TAG, \"button init failed — physical setup reset unavailable\");\n    }\n#if CONFIG_HOMEHUB_MUSECORDER\n    musecorder_init();\n#endif", 1)
    for needle in ("musecorder_on_tap", "musecorder_command(command", "musecorder_init();"):
        if needle not in t: sys.exit("apply.py: app.c anchor missing for " + needle)
    ap.write_text(t); print("  patched main/app.c (applet dispatch, BOOT tap, init)")

# --- CYD: smaller animation stripes (the anim/image buffer is DMA memory the image fetcher also needs) ---
ls = root / "main" / "led_status.c"; t = ls.read_text()
if "CYD_STRIPE_CELLS" not in t:
    t = t.replace("#define LCD_ANIM_STRIPE_CELLS 8\n", "#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ILI9341 || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796   /* CYD_STRIPE_CELLS */\n#define LCD_ANIM_STRIPE_CELLS 4\n#else\n#define LCD_ANIM_STRIPE_CELLS 8\n#endif\n", 1)
    if "CYD_STRIPE_CELLS" not in t: sys.exit("apply.py: stripe anchor not found")
    ls.write_text(t); print("  patched main/led_status.c (4-cell animation stripes on CYD boards)")

# --- CYD: smaller TX reserve (no tunnel on these boards; the image fetcher keeps this much DMA memory free) ---
nt = root / "main" / "noise_tunnel_internal.h"; t = nt.read_text()
if "CYD_TX_RESERVE" not in t:
    t = t.replace("#define NOISE_TX_DMA_RESERVE_BYTES (16 * 1024)\n", "#include \"sdkconfig.h\"\n#if (CONFIG_HOMEHUB_LED_BACKEND_CYD_ILI9341 || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796) && !CONFIG_HOMEHUB_TUNNEL   /* CYD_TX_RESERVE */\n#define NOISE_TX_DMA_RESERVE_BYTES (6 * 1024)\n#else\n#define NOISE_TX_DMA_RESERVE_BYTES (16 * 1024)\n#endif\n", 1)
    if "CYD_TX_RESERVE" not in t: sys.exit("apply.py: tx reserve anchor not found")
    nt.write_text(t); print("  patched main/noise_tunnel_internal.h (6 KB TX reserve on CYD boards)")

# --- CYD: raw button presses for applets (button.c only compiles the press hook for voice builds) ---
bt = root / "main" / "button.c"; t = bt.read_text()
if "CONFIG_HOMEHUB_MUSECORDER" not in t:
    t = t.replace("#if CONFIG_HOMEHUB_VOICE\n", "#if CONFIG_HOMEHUB_VOICE || CONFIG_HOMEHUB_MUSECORDER\n")
    bt.write_text(t); print("  patched main/button.c (press hook available to the applet runtime)")

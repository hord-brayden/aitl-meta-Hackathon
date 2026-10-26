# Board profiles

One header per board. Select with the PlatformIO env (`pio run -e <env> -t upload`):

| env | board | panel | touch | notes |
|---|---|---|---|---|
| `cyd28c` | ESP32-2432S028 capacitive 2.8" (this unit) | ILI9341 240x320 | CST820 I2C | default |
| `cyd28c_st7789` | same family, ST7789 panel variant | ST7789 | CST820 | if `cyd28c` shows white |
| `cyd28r` | ESP32-2432S028R resistive 2.8" | ILI9341 240x320 | XPT2046 (bit-banged, own pins) | the classic CYD |
| `cyd24c` | ESP32-2432S024C capacitive 2.4" | ILI9341 240x320 | CST816 I2C | curdle's board |
| `cyd35r` | ESP32-3248S035R resistive 3.5" | ST7796 480x320 | XPT2046 | wider UI, same pins |
| `hosyond40r` | Hosyond 4.0" ESP32-32E (LCDWIKI E32R40T) | ST7796 480x320 | XPT2046 on the LCD bus | big UI, LED R on IO22, amp enable IO4 |

A profile defines pins, panel class, touch class, native resolution and the mic header. Everything else
(rotation, colour flags, calibration) is a runtime setting, so one binary per board covers panel quirks.

# ESP32-S3-Touch-LCD-1.46B — On-screen Claude Assistant

A standalone conversational assistant for the **Waveshare ESP32-S3-Touch-LCD-1.46B**
(round 1.46" 412×412 touch LCD). You type a question on the **on-screen keyboard**,
it calls the **Claude Messages API** over HTTPS, and the reply is shown on the LCD —
no computer needed.

> This is **phase 1** (touch + text). Phase 2 (voice: mic → speech-to-text →
> Claude → text-to-speech → speaker) comes later and needs extra paid services.

## Hardware it targets

- **MCU:** ESP32-S3R8 (8 MB OCTAL PSRAM, 16 MB flash)
- **LCD:** SPD2010, 412×412 round IPS, **QSPI**
- **Touch:** SPD2010 capacitive (I2C `0x53`)
- **Expander:** TCA9554 — drives LCD reset, touch reset, SD CS (the resets are
  **not** direct GPIOs, which is the usual gotcha on this board)

Pin map lives in [`main/bsp_pins.h`](main/bsp_pins.h).

## Software stack

Built on the official Espressif components (pulled automatically on first build
via [`main/idf_component.yml`](main/idf_component.yml)):

- `lvgl/lvgl` 9.x + `espressif/esp_lvgl_port`
- `espressif/esp_lcd_spd2010` (display) + `espressif/esp_lcd_touch_spd2010` (touch)
- `espressif/esp_io_expander_tca9554` (reset/CS lines)

`main/claude_client.c` and `main/wifi.c` are the same proven code from the serial
version.

## Build & flash

Needs **ESP-IDF v5.3+** (uses the new I2C master driver and esp_lvgl_port v2).

```bash
idf.py set-target esp32s3
idf.py menuconfig     # AI Assistant Configuration -> WiFi + Anthropic API key
idf.py build
idf.py -p COM10 flash monitor   # use your board's port
```

Console is over the native USB Serial/JTAG (set in `sdkconfig.defaults`), matching
this board's single USB-C port.

## How it works

```
[touch keyboard] --typed text--> [LVGL UI] --queue--> [Claude worker task]
                                                            |  HTTPS POST /v1/messages
                                                            v
                                                     [api.anthropic.com]
                                                            |  reply
                                                            v
[LCD shows "Claude: ..."] <----------------------------- [worker]
```

- The keyboard's enter event pushes the prompt onto a FreeRTOS queue.
- A dedicated worker task calls Claude (blocking TLS) so the UI never freezes.
- Replies are written back to the on-screen log under the LVGL lock.

## ⚠️ First-bringup checklist (read if the screen is black/wrong)

This firmware was written from the board's documented pin map but **not yet
verified on hardware**. If the first flash misbehaves, these are the usual spots:

1. **Black screen** → the most likely cause is the **EXIO→expander-pin mapping**
   for the resets in `bsp_pins.h`. We assumed `EXIOn == Pn` (LCD_RST = P2,
   TP_RST = P1). If blank, try shifting by one (e.g. LCD_RST = `IO_EXPANDER_PIN_NUM_1`).
   Also double-check the TCA9554 I2C address (`..._ADDRESS_000` = 0x20).
2. **Garbled / wrong colors** → flip `.flags.swap_bytes` in `display.c`.
3. **Mirrored / rotated image** → adjust `.rotation` / `mirror_x` / `mirror_y`
   in `display.c`.
4. **Touch offset or unresponsive** → toggle `swap_xy` / `mirror_*` in the touch
   `tp_config`, and confirm the touch INT pin (GPIO4).

The serial logs (`idf.py monitor`) print each init step, so you can see how far
boot gets before anything goes wrong.

## Security

The API key is compiled in via `menuconfig` (stored in the git-ignored
`sdkconfig`). Fine for a personal device; don't commit a real key, and remember
anyone with flash access can read it.

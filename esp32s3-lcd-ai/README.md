# ESP32-S3-Touch-LCD-1.46B — Claude Voice Assistant

A standalone **voice assistant** for the **Waveshare ESP32-S3-Touch-LCD-1.46B**
(round 1.46" 412×412 touch LCD with mic + speaker). Tap the screen, speak, and
the device transcribes your voice, asks **Claude**, and **speaks the answer back**
— with an animated face on screen. Claude can also fetch **live weather** and
**crypto prices**, and control your **Home Assistant** smart home by voice.

No computer needed at runtime — it talks to the cloud APIs over WiFi directly.

## What it does

```
tap screen ─▶ record mic (5s) ─▶ ElevenLabs STT ─▶ Claude (+ tools) ─▶ ElevenLabs TTS ─▶ speaker
                 │                                       │
            animated face reacts          weather / crypto / Home Assistant
            (listen→think→speak)                 (Claude tool use)
```

- **Voice in:** on-board mic → ElevenLabs Speech-to-Text (Scribe)
- **Brain:** Claude Messages API with **tool use**
- **Voice out:** ElevenLabs Text-to-Speech (Indonesian-capable)
- **Animated face:** blinking eyes that turn green while listening, amber while
  thinking, and grow a talking mouth while speaking
- **Tools Claude can call:**
  - `get_weather` — live weather via wttr.in
  - `get_crypto_price` — live prices via CoinGecko (USD + IDR)
  - `ha_list_entities` / `ha_call_service` / `ha_get_state` — Home Assistant

## Hardware

- **MCU:** ESP32-S3R8 (8 MB OCTAL PSRAM, 16 MB flash)
- **LCD:** SPD2010, 412×412 round IPS, **QSPI**
- **Touch:** SPD2010 capacitive (I2C `0x53`), polled (no INT wired)
- **Expander:** TCA9554 — drives LCD/touch reset + SD CS
- **Audio:** I2S DAC/amp (speaker) + I2S mic, **no codec chip** (raw I2S)
- **Power latch:** GPIO7 must be driven HIGH at boot (also enables the amp)

Full pin map in [`main/bsp_pins.h`](main/bsp_pins.h) — verified against the
Waveshare XiaoZhi reference config for this board:

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| Speaker BCLK | 48 | | Mic SCK | 15 |
| Speaker LRCK | 38 | | Mic WS | 2 |
| Speaker DOUT | 47 | | Mic DIN | 39 |
| I2C SCL/SDA | 10 / 11 | | Power latch | 7 |

## Build & flash

Needs **ESP-IDF v5.3+**.

```bash
idf.py set-target esp32s3
idf.py menuconfig      # see "AI Assistant Configuration" below
idf.py build
idf.py -p COM10 flash monitor   # use your board's port
```

After flashing, reset the board **without holding BOOT** (unplug/replug, or press
RESET) so it leaves download mode and runs the app. Console is the native USB
Serial/JTAG.

### Configuration (`menuconfig` → "AI Assistant Configuration")

| Setting | Notes |
|---|---|
| WiFi SSID / Password | 2.4 GHz network |
| Anthropic API key | `sk-ant-...` from console.anthropic.com |
| Claude model ID | default `claude-opus-4-8` |
| Max response tokens | default 1024 |
| ElevenLabs API key | used for **both** STT and TTS |
| ElevenLabs voice ID | which voice speaks |
| ElevenLabs TTS model | `eleven_multilingual_v2` (Indonesian) |
| Home Assistant base URL | e.g. `http://192.168.0.10:8123` (optional) |
| Home Assistant token | long-lived access token (optional) |

Home Assistant is optional — leave URL/token empty to disable smart-home tools.

## Usage

1. Wait for the spoken greeting and "Tap untuk bicara".
2. **Tap anywhere** on the screen → eyes turn green ("Mendengarkan…").
3. Speak (~5 seconds).
4. Eyes turn amber while it transcribes + thinks, then the mouth animates as it
   speaks the answer.

Example things to say (Indonesian):
- "Cuaca di Bogor sekarang gimana?"
- "Berapa harga Bitcoin sekarang?"
- "Nyalakan lampu ruang tamu" / "Set AC kamar ke 24 derajat"
- "Suhu kamar berapa?"

## How the smart-home control works

Claude is given three Home Assistant tools. When you ask it to control a device,
it first calls `ha_list_entities` (a compact list rendered by HA's `/api/template`,
so the device never parses huge JSON), matches your words to an `entity_id`, then
calls `ha_call_service`. Naming your devices clearly in HA improves matching.

## Source layout

| File | Role |
|---|---|
| `main.c` | boot, power latch, init, voice-turn task |
| `wifi.c` | WiFi connect, public DNS, power-save off |
| `board.c` | I2C, TCA9554 expander, resets, backlight, power latch |
| `display.c` | SPD2010 QSPI panel + LVGL + resilient touch indev |
| `chat_ui.c` | animated assistant face (LVGL) |
| `audio.c` | I2S speaker out + mic in (32-bit mono frames) |
| `tts.c` / `stt.c` | ElevenLabs text-to-speech / speech-to-text |
| `claude_client.c` | Claude Messages API + tool-use loop |
| `tools.c` | weather, crypto, Home Assistant tools |

## Notes learned bringing this up

- The speaker frame on this board is **32-bit mono, left slot**; each 16-bit PCM
  sample is left-justified (`sample << 16`) with a digital volume gain.
- A single failed touch I2C read must **not** abort — we use a custom resilient
  LVGL input device (the stock `esp_lvgl_port` touch read reboots on one error).
- TTS audio is **fully buffered** before playback to avoid network-stall stutter,
  and **WiFi power-save is disabled** so large audio downloads don't drop.

## Security

API keys and the HA token are compiled in via `menuconfig` (stored in the
git-ignored `sdkconfig`). Fine for a personal device; never commit real secrets,
and remember anyone with flash access can read them. If a key is ever exposed
(e.g. in a screenshot), regenerate it.

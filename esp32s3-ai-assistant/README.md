# ESP32-S3 AI Assistant (Claude)

A tiny conversational AI assistant for the **ESP32-S3**, written in **C with
ESP-IDF**. The board connects to WiFi, you type a question over the serial
monitor, and it sends the prompt to the **Claude Messages API** over HTTPS and
prints the reply.

```
You: what is the capital of indonesia?
(thinking...)

Claude: The capital of Indonesia is Jakarta.
```

## How it works

```
[ Serial monitor ] --typed prompt--> [ ESP32-S3 ]
                                          |  HTTPS POST /v1/messages
                                          v
                                   [ api.anthropic.com ]
                                          |  JSON reply
                                          v
[ Serial monitor ] <---printed reply-- [ ESP32-S3 ]
```

- `main/main.c` – console input loop (works with both UART and USB Serial/JTAG consoles)
- `main/wifi.c` – WiFi station connection
- `main/claude_client.c` – builds the JSON request, calls the API over TLS, parses the reply (cJSON)

Because there is no official Anthropic SDK for C, this talks to the REST API
directly with `esp_http_client`. TLS uses the bundled Mozilla root CA set
(`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`), so no certificate wrangling is needed.

## Prerequisites

1. **ESP-IDF v5.1 or newer** installed and exported
   ([install guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)).
   After install, run `. $HOME/esp/esp-idf/export.sh` (or `%IDF_PATH%\export.bat` on Windows).
2. An **ESP32-S3** board connected over USB.
3. An **Anthropic API key** from <https://console.anthropic.com> (starts with `sk-ant-...`).
4. A 2.4 GHz WiFi network (the ESP32-S3 does not support 5 GHz).

## Build & flash

From inside this folder:

```bash
# 1. Select the chip target
idf.py set-target esp32s3

# 2. Configure WiFi + API key
idf.py menuconfig
#    -> "AI Assistant Configuration"
#       - WiFi SSID
#       - WiFi Password
#       - Anthropic API key
#       - (optional) Claude model ID, e.g. claude-haiku-4-5 for a cheaper/faster option

# 3. Build, flash, and open the serial monitor (replace the port with yours)
idf.py -p /dev/ttyUSB0 flash monitor
```

Common ports:
- Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`
- Windows: `COM3`, `COM4`, …

Exit the monitor with `Ctrl+]`.

Once it boots and connects, type a question at the `You:` prompt and press Enter.

## Configuration notes

- **Model:** defaults to `claude-opus-4-8` (most capable). For a small hobby
  device you may prefer `claude-haiku-4-5` (cheapest and fastest) — change it in
  `menuconfig` under *AI Assistant Configuration → Claude model ID*.
- **Response length:** `Max response tokens` (default 1024) bounds the reply
  size so the on-device buffer stays small.
- **Single-turn:** each prompt is independent (no conversation memory) to keep
  RAM usage low. To add multi-turn context, accumulate previous user/assistant
  messages into the `messages` array in `claude_client.c`.

## Security

The API key is compiled into the firmware via `menuconfig` (stored in the
generated `sdkconfig`, which is git-ignored here). This is fine for a personal
device, but:

- **Don't commit a real key** to a public repo.
- Anyone who can read the flash can extract the key — only flash it to hardware
  you control.
- For production, proxy requests through your own server instead of putting the
  key on the device.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `WiFi connection failed` | Check SSID/password; ensure it's a 2.4 GHz network. |
| `No API key configured` | Set the key in `idf.py menuconfig` and reflash. |
| `API returned HTTP 401` | Invalid/expired API key. |
| `API returned HTTP 400` | Check the model ID is spelled exactly (e.g. `claude-opus-4-8`). |
| TLS / connection errors | Make sure the certificate bundle is enabled (it is, via `sdkconfig.defaults`). |
| Nothing happens when you type | Make sure the monitor port matches the console; try pressing Enter. |
| Stack overflow / crash on first request | TLS needs stack — `sdkconfig.defaults` sets the main task stack to 16 KB; keep it. |

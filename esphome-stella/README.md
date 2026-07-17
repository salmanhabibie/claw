# Stella Smart Diffuser — Integrasi ESPHome

Konfigurasi ESPHome untuk menghubungkan diffuser **Stella Smart** (BLE) ke
Home Assistant, memakai komponen eksternal
[rcayadi/esphome-stella-smart](https://github.com/rcayadi/esphome-stella-smart).

> Catatan: folder ini terpisah dari isi utama repo `claw` (yang merupakan
> tool banding file & filter email). Ditaruh di sini atas permintaan,
> supaya konfigurasinya tersimpan versi-kontrol.

## Kebutuhan

- Board **ESP32** (generic devkit, mis. ESP32-DevKitC / NodeMCU-32S) yang
  jaraknya cukup dekat (BLE) ke diffuser Stella Smart.
- [ESPHome](https://esphome.io) (CLI atau add-on Home Assistant) versi
  terbaru.
- Home Assistant dengan integrasi ESPHome aktif.
- MAC address diffuser Stella Smart-mu.

## Cara pakai

1. **Cari MAC address diffuser.** Pakai app BLE scanner seperti
   [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile)
   di HP, cari perangkat dengan nama mirip "Stella"/"Diffuser", lalu catat
   MAC address-nya. Alternatif: flash board ESP32 dulu dengan
   `esp32_ble_tracker` + `logger` untuk scan BLE di sekitar dan lihat log-nya.

2. **Salin `secrets.yaml.example` → `secrets.yaml`** di folder ini, isi
   SSID/password WiFi, password OTA, dan `api_encryption_key`
   (generate lewat `esphome secrets stella-diffuser.yaml` atau dari Home
   Assistant saat menambah device baru). File `secrets.yaml` sudah masuk
   `.gitignore`, jangan sampai ke-commit.

3. **Edit `stella-diffuser.yaml`**: ganti nilai `stella_mac_address` di blok
   `substitutions:` (paling atas file) dengan MAC address diffuser kamu yang
   sebenarnya. Nilai ini otomatis dipakai di blok `ble_client` dan
   `stella_smart`, jadi cuma perlu diganti di satu tempat.

4. **Compile & flash** (sambungkan ESP32 via USB untuk flash pertama kali):

   ```bash
   esphome run stella-diffuser.yaml
   ```

5. **Tambahkan ke Home Assistant.** Board otomatis muncul di
   Settings → Devices & Services → ESPHome (kalau `api:` + mDNS aktif dan
   satu jaringan). Kalau tidak otomatis muncul, tambahkan manual pakai
   IP board.

## Entitas yang muncul di Home Assistant

| Entitas                      | Tipe          | Fungsi                                  |
|-------------------------------|---------------|------------------------------------------|
| Stella Baterai                | Sensor        | Level baterai 0–100%                     |
| Stella Jumlah Semprot         | Sensor        | Total hitungan semprot                   |
| Stella Daya                   | Switch        | Nyala/mati diffuser                      |
| Stella Semprot Sekarang       | Button        | Trigger satu kali semprot manual         |
| Stella Reset Hitungan         | Button        | Reset counter semprot                    |
| Stella Durasi Semprot         | Select        | 10 min / 20 min / 40 min                 |
| Stella Mode Timer             | Select        | X (mati) / 6h / 8h / 12h                 |
| Stella Versi Firmware         | Text sensor   | Versi firmware diffuser (diagnostic)     |

## Troubleshooting

- **Gagal connect BLE**: pastikan diffuser dalam jangkauan, tidak sedang
  terhubung ke app HP lain (BLE biasanya cuma bisa 1 koneksi aktif),
  dan MAC address sudah benar (huruf besar, format `AA:BB:CC:DD:EE:FF`).
- **Component tidak ketemu saat compile**: pastikan koneksi internet ada
  saat compile (ESPHome men-download `external_components` dari GitHub),
  atau pin versi dengan `ref:` ke commit/tag tertentu kalau mau lebih stabil.
- Untuk detail schema/opsi lanjutan, cek langsung
  [source component-nya](https://github.com/rcayadi/esphome-stella-smart/tree/main/components/stella_smart).

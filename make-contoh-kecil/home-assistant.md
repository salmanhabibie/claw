# Home Assistant → Make → Notifikasi HP

Contoh: saat ada kejadian di rumah (pintu dibuka, sensor gerak, dll),
Home Assistant memanggil webhook Make, lalu Make mengirim **notifikasi push ke HP**
lewat [ntfy.sh](https://ntfy.sh) (gratis, tanpa daftar akun).

```
Home Assistant  →  Webhook Make  →  ntfy.sh  →  HP kamu
```

Scenario **"Home Assistant - Notifikasi Kejadian Rumah"** sudah aktif di akun Make.

- **URL webhook:** `https://hook.us1.make.com/2h1uy6rgbdqspz1euhi5immqr5apgn25`
- **Topik ntfy:** `rumah-salman-fd13-8k2q`

## Langkah 1 — Terima notifikasi di HP

Pilih salah satu:

- **App (disarankan):** install **ntfy** dari Play Store / App Store → buka →
  **Subscribe to topic** → ketik `rumah-salman-fd13-8k2q`.
- **Browser:** buka https://ntfy.sh/rumah-salman-fd13-8k2q dan biarkan tab-nya terbuka.

> Topik ntfy bersifat publik — siapa pun yang tahu namanya bisa membacanya.
> Karena itu namanya dibuat acak. Jangan kirim data sensitif lewat sini.

## Langkah 2 — Uji dulu tanpa Home Assistant

Buka alamat ini di browser:

```
https://hook.us1.make.com/2h1uy6rgbdqspz1euhi5immqr5apgn25?pesan=Tes+dari+browser
```

Beberapa detik kemudian notifikasi "Tes dari browser" muncul di app/halaman ntfy.
Kalau ini jalan, berarti rangkaian Make → HP sudah beres.

## Langkah 3 — Sambungkan Home Assistant

Tambahkan ke `configuration.yaml` Home Assistant:

```yaml
rest_command:
  lapor_ke_make:
    url: "https://hook.us1.make.com/2h1uy6rgbdqspz1euhi5immqr5apgn25"
    method: POST
    content_type: "application/json"
    payload: '{"pesan": "{{ pesan }}"}'
```

Restart Home Assistant, lalu buat automation (Settings → Automations → New,
atau lewat YAML). Contoh: lapor saat pintu depan dibuka —

```yaml
automation:
  - alias: "Lapor pintu depan dibuka"
    trigger:
      - platform: state
        entity_id: binary_sensor.pintu_depan   # ganti dengan entity milikmu
        to: "on"
    action:
      - service: rest_command.lapor_ke_make
        data:
          pesan: "Pintu depan dibuka!"
```

Ganti `binary_sensor.pintu_depan` dengan entity sensor milikmu
(lihat daftarnya di HA: Developer Tools → States).

Automation lain tinggal panggil `rest_command.lapor_ke_make` dengan `pesan` berbeda,
misalnya "Ada gerakan di ruang tamu" atau "Suhu kamar di atas 30°C".

> Cara ini **tidak butuh** Home Assistant bisa diakses dari internet —
> HA yang menghubungi Make, bukan sebaliknya.

## Ganti tujuan notifikasi (opsional)

Kalau nanti mau ganti ntfy dengan **Telegram** atau **Gmail**: buka scenario di Make,
hapus modul HTTP, ganti dengan modul Telegram *Send a Message* / Gmail *Send an Email*,
lalu buat koneksinya saat diminta.

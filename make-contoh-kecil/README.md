# Contoh Kecil — App Sapaan di Make.com

Contoh app (scenario) paling sederhana di [Make.com](https://www.make.com):
sebuah **API sapaan** yang menerima nama lewat webhook dan langsung membalas JSON.

## Cara kerja

Hanya 2 modul:

```
[1] Custom Webhook  →  [2] Webhook Response
```

1. **Custom webhook** — menerima request HTTP (mis. `{"nama": "Salman"}`).
2. **Webhook response** — membalas JSON:

```json
{
  "pesan": "Halo, Salman!",
  "dari": "Contoh kecil app Make",
  "waktu": "2026-08-14T22:30:00.000Z"
}
```

Jika `nama` tidak dikirim, balasannya `"Halo, teman!"` (pakai fungsi `ifempty`).

## Cara pakai (scenario sudah dibuat & aktif di akunmu)

Scenario **"Contoh Kecil - App Sapaan (Webhook)"** sudah aktif di team *My Team*.
Uji dari terminal / browser:

```bash
# Dengan nama
curl -X POST "https://hook.us1.make.com/ldtq083u9gjqoh1c3qkqr6e61o5cyrcf" \
  -H "Content-Type: application/json" \
  -d '{"nama": "Salman"}'

# Tanpa nama (balasan: "Halo, teman!")
curl "https://hook.us1.make.com/ldtq083u9gjqoh1c3qkqr6e61o5cyrcf"
```

Bisa juga dibuka langsung di browser:
`https://hook.us1.make.com/ldtq083u9gjqoh1c3qkqr6e61o5cyrcf?nama=Salman`

## Impor ulang blueprint (opsional)

File [`blueprint.json`](blueprint.json) adalah cetak biru scenario ini.
Untuk memakainya di akun/team lain:

1. Di Make, buat scenario baru → menu **⋯** → **Import Blueprint** → pilih `blueprint.json`.
2. Klik modul webhook → buat webhook **baru** (ID webhook `2797986` milik akun asal,
   jadi harus diganti).
3. Simpan lalu aktifkan scenario (tombol **ON**).

## Contoh lanjutan: Home Assistant

Lihat [`home-assistant.md`](home-assistant.md) — kejadian di rumah (pintu dibuka, dll)
dikirim Home Assistant ke webhook Make, lalu diteruskan sebagai notifikasi push ke HP.

## Ide pengembangan

- Tambah modul (Gmail, Google Sheets, Telegram, dll) di antara webhook dan response.
- Tambah **Router** untuk balasan berbeda berdasarkan isi request.
- Ganti balasan JSON dengan HTML untuk halaman mini.

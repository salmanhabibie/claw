# claw — Pembanding & Filter Email TXT

Aplikasi sederhana untuk file `.txt`, dengan dua fitur:

1. **Bandingkan File** — membandingkan dua file (File 1 vs File 2) dan menghitung
   **berapa baris yang hilang**. Pencocokan berdasarkan isi baris (urutan acak aman).
2. **Filter Email** — kelompokkan & hitung email per penyedia
   (gmail.com, hotmail.com, comcast.net, yahoo.com, dll), lalu filter / ambil yang dibutuhkan.

Tersedia dua versi: aplikasi web dan CLI.

## 1. Aplikasi Web (paling mudah)

Buka file [`index.html`](index.html) di browser (klik dua kali). Tidak perlu install apa pun.

1. Masukkan **File 1** (acuan/asli) dan **File 2** (pembanding) — bisa lewat
   klik/seret file, atau tempel langsung teksnya.
2. Klik **Bandingkan**.
3. Hasil menampilkan:
   - Jumlah baris masing-masing file
   - **Baris yang HILANG** (ada di File 1, tidak ada di File 2)
   - **Baris TAMBAHAN** (ada di File 2, tidak ada di File 1)

Opsi: abaikan spasi awal/akhir, abaikan huruf besar/kecil, abaikan baris kosong.

## 2. Versi CLI (Python)

Tidak butuh library tambahan, cukup Python 3.

```bash
# Bandingkan dua file
python3 compare_txt.py file1.txt file2.txt

# Tampilkan juga isi baris yang berbeda
python3 compare_txt.py file1.txt file2.txt --show
```

Contoh hasil:

```
==================================================
File 1 : file1.txt  (5 baris)
File 2 : file2.txt  (4 baris)
==================================================
Baris HILANG di File 2   : 2
Baris TAMBAHAN di File 2 : 1
==================================================
```

### Opsi CLI

| Opsi            | Keterangan                                   |
|-----------------|----------------------------------------------|
| `--show`        | Tampilkan isi tiap baris yang berbeda        |
| `--ignore-case` | Abaikan huruf besar/kecil                    |
| `--no-trim`     | Jangan abaikan spasi di awal/akhir baris     |
| `--no-blank`    | Abaikan baris kosong                         |

> Exit code `0` jika kedua file sama, `1` jika ada perbedaan (berguna untuk skrip otomatis).

## 3. Filter Email

### Versi Web
Buka [`index.html`](index.html) → pilih tab **✉️ Filter Email**:
1. Masukkan daftar email (file/tempel — urutan acak tidak masalah).
2. Klik **Deteksi Domain** → muncul semua domain beserta jumlahnya.
3. Centang domain yang diinginkan (atau pakai tombol cepat: Gmail, Hotmail, Comcast.net, dll).
4. Hasil bisa di-**Copy** atau **Download .txt**.
   - **Mode buang**: centang domain yang ingin *disembunyikan* (sisanya yang tampil).

### Versi CLI (Python)

```bash
# Ringkasan jumlah email per domain
python3 filter_email.py emails.txt

# Ambil hanya gmail.com & hotmail.com, simpan ke file
python3 filter_email.py emails.txt --only gmail.com hotmail.com -o hasil.txt

# Buang comcast.net & yahoo.com
python3 filter_email.py emails.txt --exclude comcast.net yahoo.com

# Hapus email duplikat
python3 filter_email.py emails.txt --unique
```

| Opsi              | Keterangan                                  |
|-------------------|---------------------------------------------|
| `--only DOMAIN…`  | Hanya tampilkan email dari domain tsb       |
| `--exclude DOMAIN…` | Sembunyikan email dari domain tsb         |
| `-o, --output FILE` | Simpan hasil ke file                      |
| `--unique`        | Hapus email duplikat                        |

## Catatan

Perbandingan dilakukan per-baris dan memperhitungkan baris duplikat, jadi kalau
sebuah baris muncul 3x di File 1 tapi hanya 1x di File 2, maka 2 baris dihitung hilang.

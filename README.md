# claw — Pembanding File TXT

Aplikasi sederhana untuk membandingkan dua file `.txt` (File 1 vs File 2) dan
menghitung **berapa baris yang hilang**. Tersedia dua versi: aplikasi web dan CLI.

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

## Catatan

Perbandingan dilakukan per-baris dan memperhitungkan baris duplikat, jadi kalau
sebuah baris muncul 3x di File 1 tapi hanya 1x di File 2, maka 2 baris dihitung hilang.

# claw — Kumpulan Aplikasi Kecil

Kumpulan aplikasi sederhana, masing-masing ada versi web dan CLI:

1. **Bandingkan File** — membandingkan dua file `.txt` (File 1 vs File 2) dan menghitung
   **berapa baris yang hilang**. Pencocokan berdasarkan isi baris (urutan acak aman).
2. **Filter Email** — kelompokkan & hitung email per penyedia
   (gmail.com, hotmail.com, comcast.net, yahoo.com, dll), lalu filter / ambil yang dibutuhkan.
3. **Deteksi & Pisahkan Gambar Dokumen** — deteksi otomatis gambar
   **kartu identitas (KTP/ID)**, **SIM / driver license**, dan **surat/dokumen**,
   lalu pisahkan per kategori. [Lihat caranya di bawah.](#4-deteksi--pisahkan-gambar-dokumen)

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

## 4. Deteksi & Pisahkan Gambar Dokumen

Deteksi otomatis isi gambar dan pisahkan menjadi 4 kelompok:

| Kategori             | Contoh                                        | Folder hasil         |
|----------------------|-----------------------------------------------|----------------------|
| 🪪 Kartu Identitas   | KTP, ID card                                  | `kartu-identitas/`   |
| 🚗 SIM               | SIM Indonesia, driver license luar negeri     | `sim-driver-license/`|
| 📄 Surat / Dokumen   | surat resmi, surat keterangan, dokumen teks   | `surat-dokumen/`     |
| ❓ Lainnya           | gambar yang tidak terdeteksi                  | `lainnya/`           |

Deteksi memakai **OCR** (baca teks di gambar) + kata kunci
(mis. "KARTU TANDA PENDUDUK", "NIK", "SURAT IZIN MENGEMUDI", "DRIVER LICENSE",
"Kepada Yth", "Dengan hormat") ditambah heuristik bentuk gambar
(kartu = landscape rasio kartu, surat = portrait banyak teks).

### Versi Web

Buka [`deteksi_dokumen.html`](deteksi_dokumen.html) di browser:

1. Klik/seret gambar (boleh banyak sekaligus) → klik **🔍 Deteksi & Pisahkan**.
2. Hasil tampil terkelompok per kategori, lengkap dengan kata kunci yang
   ditemukan dan teks hasil OCR.
3. Salah kategori? Pindahkan lewat dropdown di tiap gambar.
4. Unduh hasil: **ZIP per kategori**, atau **ZIP semua** (berisi subfolder per kategori).

Semua diproses **di browser** — gambar tidak diunggah ke server mana pun.
Butuh internet hanya saat pertama kali (mengunduh mesin OCR Tesseract.js);
jika offline, deteksi memakai heuristik bentuk saja dan kategori bisa diatur manual.

### Versi Windows (.exe) — paling praktis

Tersedia **`PisahDokumen.exe`**: aplikasi jendela (GUI) untuk Windows,
tidak perlu install Python/tesseract apa pun — mesin OCR sudah dibundel di dalamnya.

Cara mendapatkan:

1. Buka tab **Actions** di repo GitHub ini.
2. Pilih run terbaru **"Build EXE (Windows)"** (yang hijau ✅).
3. Di bagian **Artifacts**, unduh **`PisahDokumen-windows`** → ekstrak → jalankan `PisahDokumen.exe`.

Cara pakai: pilih folder gambar → (opsional) folder hasil → klik
**Mulai Deteksi & Pisahkan**. Gambar otomatis tersalin/terpindah ke subfolder
`kartu-identitas/`, `sim-driver-license/`, `surat-dokumen/`, `lainnya/`.

> Catatan: exe di-build otomatis oleh GitHub Actions
> ([`.github/workflows/build-exe.yml`](.github/workflows/build-exe.yml))
> dari [`pisah_dokumen_gui.py`](pisah_dokumen_gui.py). Saat pertama dibuka,
> Windows SmartScreen mungkin memberi peringatan karena exe tidak
> bertanda tangan digital — klik "More info" → "Run anyway".

### Versi CLI (Python)

Butuh Pillow + pytesseract + program tesseract:

```bash
pip install pillow pytesseract
# Ubuntu/Debian:
sudo apt install tesseract-ocr tesseract-ocr-ind
```

Pemakaian:

```bash
# Salin gambar ke subfolder per kategori (di folder yang sama)
python3 pisah_dokumen.py folder_gambar/

# Simpan hasil ke folder lain, dan pindahkan (bukan salin)
python3 pisah_dokumen.py folder_gambar/ -o hasil/ --move

# Cek dulu hasil deteksinya tanpa memindahkan apa pun
python3 pisah_dokumen.py folder_gambar/ --dry-run
```

| Opsi              | Keterangan                                        |
|-------------------|---------------------------------------------------|
| `-o, --output DIR`| Folder tujuan (default: folder input)             |
| `--move`          | Pindahkan file (default: salin)                   |
| `--lang LANG`     | Bahasa OCR tesseract (default: `ind+eng`)         |
| `--dry-run`       | Hanya tampilkan hasil deteksi, tidak menyentuh file |

## Catatan

Perbandingan dilakukan per-baris dan memperhitungkan baris duplikat, jadi kalau
sebuah baris muncul 3x di File 1 tapi hanya 1x di File 2, maka 2 baris dihitung hilang.

#!/usr/bin/env python3
"""Pisahkan gambar dokumen (KTP/ID, SIM/driver license, surat) ke subfolder.

Membaca teks tiap gambar dengan OCR (pytesseract), lalu menyalin/memindahkan
gambar ke subfolder sesuai kategori:

    kartu-identitas/    -> KTP / ID card
    sim-driver-license/ -> SIM / driver license
    surat-dokumen/      -> surat / dokumen teks
    lainnya/            -> tidak terdeteksi

Contoh:
    python3 pisah_dokumen.py folder_gambar/
    python3 pisah_dokumen.py folder_gambar/ -o hasil/ --move
    python3 pisah_dokumen.py folder_gambar/ --dry-run
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Butuh Pillow. Install dengan: pip install pillow")

try:
    import pytesseract
except ImportError:
    sys.exit(
        "Butuh pytesseract. Install dengan: pip install pytesseract\n"
        "dan pastikan program tesseract-ocr terpasang "
        "(mis. apt install tesseract-ocr tesseract-ocr-ind)"
    )

EKSTENSI = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif", ".tif", ".tiff"}

FOLDER = {
    "ktp": "kartu-identitas",
    "sim": "sim-driver-license",
    "surat": "surat-dokumen",
    "lainnya": "lainnya",
}

# Kata kunci per kategori: (regex, bobot) — sama dengan versi web.
KEYWORDS = {
    "sim": [
        (r"surat\s+izin\s+mengemudi", 12),
        (r"driv(?:er'?s?|ing)\s+licen[cs]e", 12),
        (r"operator'?s?\s+licen[cs]e", 8),
        (r"\bd\.?m\.?v\.?\b", 5),
        (r"motor\s+vehicle", 5),
        (r"\bsim\b", 4),
        (r"\bpolri\b", 5),
        (r"kepolisian", 4),
        (r"\bclass\b", 2),
        (r"\bexpires?\b", 2),
    ],
    "ktp": [
        (r"kartu\s+tanda\s+penduduk", 12),
        (r"kartu\s+identitas", 8),
        (r"\bnik\b", 9),
        (r"identity\s+card", 8),
        (r"national\s+id(?:entity)?", 6),
        (r"kewarganegaraan", 7),
        (r"gol\.?\s*darah", 6),
        (r"status\s+perkawinan", 6),
        (r"seumur\s+hidup", 6),
        (r"tempat\s*/?\s*tgl\.?\s*lahir", 5),
        (r"kel\w*\s*/\s*desa", 4),
        (r"provinsi", 3),
        (r"kecamatan", 3),
        (r"agama", 3),
        (r"berlaku\s+hingga", 3),
    ],
    "surat": [
        (r"surat\s+(keterangan|pernyataan|kuasa|undangan|edaran|tugas|"
         r"permohonan|peringatan|perjanjian)", 10),
        (r"dengan\s+hormat", 9),
        (r"to\s+whom\s+it\s+may\s+concern", 9),
        (r"kepada\s*yth", 8),
        (r"hormat\s+(kami|saya)", 8),
        (r"perihal", 7),
        (r"sincerely", 7),
        (r"lampiran", 6),
        (r"demikian\s+(surat|pernyataan)", 6),
        (r"(?:best|kind)\s+regards|regards\s*,", 6),
        (r"\byth\b", 4),
        (r"\bdear\b", 4),
        (r"tanda\s+tangan", 3),
    ],
}

AMBANG = 5  # skor minimal supaya tidak masuk "lainnya"


def klasifikasi(teks: str, lebar: int, tinggi: int):
    """Kembalikan (kategori, skor, kata_kunci_yang_cocok)."""
    t = re.sub(r"\s+", " ", (teks or "").lower())
    jumlah_kata = len(t.split())
    skor = {"ktp": 0, "sim": 0, "surat": 0}
    cocok = {"ktp": [], "sim": [], "surat": []}

    for kategori, daftar in KEYWORDS.items():
        for pola, bobot in daftar:
            m = re.search(pola, t)
            if m:
                skor[kategori] += bobot
                cocok[kategori].append(m.group(0).strip())

    # Heuristik bentuk: surat biasanya portrait & banyak kata,
    # kartu biasanya landscape dengan rasio ± kartu ATM.
    if lebar and tinggi:
        rasio = lebar / tinggi
        if tinggi > lebar * 1.1 and jumlah_kata >= 40:
            skor["surat"] += 5
        if 1.3 <= rasio <= 1.85:
            skor["ktp"] += 1
            skor["sim"] += 1

    terbaik, tertinggi = "lainnya", 0
    for kategori in ("sim", "ktp", "surat"):
        if skor[kategori] > tertinggi:
            terbaik, tertinggi = kategori, skor[kategori]
    if tertinggi < AMBANG:
        return "lainnya", tertinggi, []
    return terbaik, tertinggi, cocok[terbaik]


def nama_unik(tujuan: Path) -> Path:
    """Hindari menimpa file dengan nama sama di folder tujuan."""
    if not tujuan.exists():
        return tujuan
    n = 1
    while True:
        n += 1
        kandidat = tujuan.with_name(f"{tujuan.stem}-{n}{tujuan.suffix}")
        if not kandidat.exists():
            return kandidat


def main():
    p = argparse.ArgumentParser(
        description="Pisahkan gambar dokumen (KTP, SIM, surat) ke subfolder dengan OCR.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("folder", help="Folder berisi gambar yang mau dipisahkan")
    p.add_argument("-o", "--output", default=None,
                   help="Folder tujuan (default: sama dengan folder input)")
    p.add_argument("--move", action="store_true",
                   help="Pindahkan file (default: salin)")
    p.add_argument("--lang", default="ind+eng",
                   help="Bahasa OCR tesseract (default: ind+eng)")
    p.add_argument("--dry-run", action="store_true",
                   help="Hanya tampilkan hasil deteksi, tidak menyalin/memindahkan")
    args = p.parse_args()

    sumber = Path(args.folder)
    if not sumber.is_dir():
        sys.exit(f"Folder tidak ditemukan: {sumber}")
    tujuan = Path(args.output) if args.output else sumber

    gambar = sorted(
        f for f in sumber.iterdir()
        if f.is_file() and f.suffix.lower() in EKSTENSI
    )
    if not gambar:
        sys.exit(f"Tidak ada file gambar di {sumber}")

    hitung = {k: 0 for k in FOLDER}
    print(f"Memproses {len(gambar)} gambar dari {sumber} (OCR: {args.lang})\n")

    for i, path in enumerate(gambar, 1):
        try:
            with Image.open(path) as img:
                lebar, tinggi = img.size
                teks = pytesseract.image_to_string(img, lang=args.lang)
        except Exception as e:  # gambar korup / bahasa OCR tidak terpasang
            print(f"[{i}/{len(gambar)}] {path.name}: GAGAL ({e})")
            teks, lebar, tinggi = "", 0, 0

        kategori, skor, cocok = klasifikasi(teks, lebar, tinggi)
        hitung[kategori] += 1
        info = f", cocok: {', '.join(cocok[:3])}" if cocok else ""
        print(f"[{i}/{len(gambar)}] {path.name} -> {FOLDER[kategori]} (skor {skor}{info})")

        if not args.dry_run:
            folder_kat = tujuan / FOLDER[kategori]
            folder_kat.mkdir(parents=True, exist_ok=True)
            target = nama_unik(folder_kat / path.name)
            if args.move:
                shutil.move(str(path), target)
            else:
                shutil.copy2(path, target)

    print("\n" + "=" * 50)
    for kategori, folder in FOLDER.items():
        print(f"{folder:<20}: {hitung[kategori]} gambar")
    print("=" * 50)
    if args.dry_run:
        print("(dry-run: tidak ada file yang disalin/dipindahkan)")
    else:
        aksi = "dipindahkan" if args.move else "disalin"
        print(f"Selesai — file {aksi} ke {tujuan}")


if __name__ == "__main__":
    main()

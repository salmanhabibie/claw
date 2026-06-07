#!/usr/bin/env python3
"""Pembanding dua file TXT: cek berapa baris yang hilang.

Membandingkan File 1 (acuan) dengan File 2 (pembanding) dan melaporkan:
  - Baris yang HILANG  : ada di File 1, tidak ada di File 2
  - Baris TAMBAHAN      : ada di File 2, tidak ada di File 1

Contoh:
    python compare_txt.py file1.txt file2.txt
    python compare_txt.py file1.txt file2.txt --ignore-case --no-blank
"""
import argparse
import sys
from collections import Counter


def baca_baris(path, trim, ignore_case, ignore_blank):
    """Baca file menjadi daftar (nomor_baris, isi_asli, isi_ternormalisasi)."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            isi = f.read()
    except FileNotFoundError:
        sys.exit(f"Error: file tidak ditemukan -> {path}")
    except OSError as e:
        sys.exit(f"Error membaca {path}: {e}")

    hasil = []
    for i, baris in enumerate(isi.splitlines(), start=1):
        norm = baris
        if trim:
            norm = norm.strip()
        if ignore_case:
            norm = norm.lower()
        if ignore_blank and norm == "":
            continue
        hasil.append((i, baris, norm))
    return hasil


def selisih(sumber, target):
    """Baris di `sumber` yang tidak terpenuhi oleh `target` (memperhitungkan duplikat)."""
    tersedia = Counter(n for _, _, n in target)
    hilang = []
    for nomor, asli, norm in sumber:
        if tersedia[norm] > 0:
            tersedia[norm] -= 1
        else:
            hilang.append((nomor, asli))
    return hilang


def main():
    p = argparse.ArgumentParser(
        description="Bandingkan dua file TXT dan hitung baris yang hilang."
    )
    p.add_argument("file1", help="File 1 (acuan / asli)")
    p.add_argument("file2", help="File 2 (pembanding)")
    p.add_argument("--no-trim", action="store_true",
                   help="Jangan abaikan spasi di awal/akhir baris")
    p.add_argument("--ignore-case", action="store_true",
                   help="Abaikan huruf besar/kecil")
    p.add_argument("--no-blank", action="store_true",
                   help="Abaikan baris kosong")
    p.add_argument("--show", action="store_true",
                   help="Tampilkan isi tiap baris yang berbeda")
    args = p.parse_args()

    trim = not args.no_trim
    l1 = baca_baris(args.file1, trim, args.ignore_case, args.no_blank)
    l2 = baca_baris(args.file2, trim, args.ignore_case, args.no_blank)

    hilang = selisih(l1, l2)      # ada di file1, tak ada di file2
    tambahan = selisih(l2, l1)    # ada di file2, tak ada di file1

    print("=" * 50)
    print(f"File 1 : {args.file1}  ({len(l1)} baris)")
    print(f"File 2 : {args.file2}  ({len(l2)} baris)")
    print("=" * 50)
    print(f"Baris HILANG di File 2   : {len(hilang)}")
    print(f"Baris TAMBAHAN di File 2 : {len(tambahan)}")
    print("=" * 50)

    if args.show:
        if hilang:
            print("\n-- HILANG (ada di File 1, tidak di File 2) --")
            for nomor, asli in hilang:
                print(f"  baris {nomor}: {asli}")
        if tambahan:
            print("\n-- TAMBAHAN (ada di File 2, tidak di File 1) --")
            for nomor, asli in tambahan:
                print(f"  baris {nomor}: {asli}")

    # exit code 1 kalau ada perbedaan (berguna untuk skrip otomatis)
    sys.exit(1 if (hilang or tambahan) else 0)


if __name__ == "__main__":
    main()

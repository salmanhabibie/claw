#!/usr/bin/env python3
"""Filter / kelompokkan email berdasarkan penyedia (domain).

Membaca file berisi email (urutan acak tidak masalah), menghitung jumlah
email per domain, dan bisa memfilter hanya domain tertentu.

Contoh:
    # Lihat ringkasan jumlah email per domain
    python filter_email.py emails.txt

    # Ambil hanya gmail.com & hotmail.com, simpan ke file
    python filter_email.py emails.txt --only gmail.com hotmail.com -o hasil.txt

    # Buang (exclude) comcast.net & yahoo.com
    python filter_email.py emails.txt --exclude comcast.net yahoo.com
"""
import argparse
import re
import sys
from collections import Counter

EMAIL_RE = re.compile(r"[a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,}")


def baca_email(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            isi = f.read()
    except FileNotFoundError:
        sys.exit(f"Error: file tidak ditemukan -> {path}")
    except OSError as e:
        sys.exit(f"Error membaca {path}: {e}")
    return [m.group(0) for m in EMAIL_RE.finditer(isi)]


def domain_of(email):
    return email[email.rfind("@") + 1:].lower()


def main():
    p = argparse.ArgumentParser(
        description="Filter email berdasarkan penyedia (gmail.com, hotmail.com, comcast.net, dll)."
    )
    p.add_argument("file", help="File berisi daftar email")
    p.add_argument("--only", nargs="+", metavar="DOMAIN",
                   help="Hanya tampilkan email dari domain ini")
    p.add_argument("--exclude", nargs="+", metavar="DOMAIN",
                   help="Sembunyikan email dari domain ini")
    p.add_argument("-o", "--output", metavar="FILE",
                   help="Simpan hasil ke file (kalau tidak, tampilkan di layar)")
    p.add_argument("--unique", action="store_true",
                   help="Hapus email duplikat")
    args = p.parse_args()

    emails = baca_email(args.file)
    if args.unique:
        seen = set()
        emails = [e for e in emails if not (e.lower() in seen or seen.add(e.lower()))]

    counts = Counter(domain_of(e) for e in emails)

    # ----- ringkasan per domain -----
    print("=" * 50)
    print(f"File          : {args.file}")
    print(f"Total email   : {len(emails)}")
    print(f"Jumlah domain : {len(counts)}")
    print("=" * 50)
    print("Email per domain (terbanyak di atas):")
    for domain, n in counts.most_common():
        print(f"  {domain:<28} {n}")
    print("=" * 50)

    # ----- terapkan filter -----
    only = {d.lower() for d in args.only} if args.only else None
    exclude = {d.lower() for d in args.exclude} if args.exclude else None

    hasil = []
    for e in emails:
        d = domain_of(e)
        if only is not None and d not in only:
            continue
        if exclude is not None and d in exclude:
            continue
        hasil.append(e)

    if only is None and exclude is None:
        # tidak ada filter: cukup tampilkan ringkasan saja
        return

    print(f"Hasil filter  : {len(hasil)} email")
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write("\n".join(hasil) + ("\n" if hasil else ""))
        print(f"Tersimpan ke  : {args.output}")
    else:
        print("-" * 50)
        for e in hasil:
            print(e)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Aplikasi GUI: pisahkan gambar dokumen (KTP/ID, SIM, surat) ke subfolder.

Versi jendela (Tkinter) dari pisah_dokumen.py — tinggal pilih folder, klik
"Mulai", dan gambar akan dipisahkan otomatis ke:

    kartu-identitas/    -> KTP / ID card
    sim-driver-license/ -> SIM / driver license
    surat-dokumen/      -> surat / dokumen teks
    lainnya/            -> tidak terdeteksi

Dipakai untuk membuat PisahDokumen.exe lewat PyInstaller (lihat
.github/workflows/build-exe.yml) — tesseract ikut dibundel di dalam exe,
jadi pengguna Windows tidak perlu install apa pun.
"""

import os
import queue
import shutil
import sys
import threading
from pathlib import Path

import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk

IMPOR_GAGAL = None
try:
    from PIL import Image
    import pytesseract
    from pisah_dokumen import EKSTENSI, FOLDER, klasifikasi, nama_unik
except BaseException as e:  # pisah_dokumen bisa memanggil sys.exit
    IMPOR_GAGAL = str(e)

NAMA_APLIKASI = "Pisah Dokumen — KTP / SIM / Surat"


def siapkan_tesseract() -> bool:
    """Arahkan pytesseract ke tesseract yang dibundel exe, atau cari di sistem."""
    if getattr(sys, "frozen", False):  # berjalan dari PyInstaller
        base = Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
        bundel = base / "tesseract" / "tesseract.exe"
        if bundel.exists():
            pytesseract.pytesseract.tesseract_cmd = str(bundel)
            os.environ["TESSDATA_PREFIX"] = str(bundel.parent / "tessdata")
            return True
    if shutil.which("tesseract"):
        return True
    umum = Path(r"C:\Program Files\Tesseract-OCR\tesseract.exe")
    if umum.exists():
        pytesseract.pytesseract.tesseract_cmd = str(umum)
        return True
    return False


class Aplikasi:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title(NAMA_APLIKASI)
        root.geometry("680x560")
        root.minsize(560, 460)

        self.antrean = queue.Queue()
        self.sedang_jalan = False

        bingkai = ttk.Frame(root, padding=12)
        bingkai.pack(fill="both", expand=True)

        # --- Pilih folder ---
        self.var_input = tk.StringVar()
        self.var_output = tk.StringVar()
        self._baris_folder(bingkai, "Folder gambar:", self.var_input, self._pilih_input)
        self._baris_folder(bingkai, "Folder hasil (opsional):", self.var_output, self._pilih_output)

        # --- Opsi ---
        opsi = ttk.Frame(bingkai)
        opsi.pack(fill="x", pady=(4, 8))
        self.var_aksi = tk.StringVar(value="salin")
        ttk.Radiobutton(opsi, text="Salin file", variable=self.var_aksi, value="salin").pack(side="left")
        ttk.Radiobutton(opsi, text="Pindahkan file", variable=self.var_aksi, value="pindah").pack(side="left", padx=(12, 0))
        self.var_dry = tk.BooleanVar(value=False)
        ttk.Checkbutton(opsi, text="Hanya deteksi (jangan sentuh file)", variable=self.var_dry).pack(side="left", padx=(18, 0))

        # --- Tombol & progres ---
        baris_aksi = ttk.Frame(bingkai)
        baris_aksi.pack(fill="x", pady=(0, 8))
        self.tombol_mulai = ttk.Button(baris_aksi, text="🔍  Mulai Deteksi & Pisahkan", command=self._mulai)
        self.tombol_mulai.pack(side="left")
        self.label_status = ttk.Label(baris_aksi, text="Siap.")
        self.label_status.pack(side="left", padx=12)

        self.progres = ttk.Progressbar(bingkai, mode="determinate")
        self.progres.pack(fill="x", pady=(0, 8))

        # --- Log ---
        self.log = scrolledtext.ScrolledText(bingkai, height=16, state="disabled", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)

        root.after(100, self._pompa_antrean)

        if IMPOR_GAGAL:
            messagebox.showerror(
                NAMA_APLIKASI,
                "Komponen tidak lengkap:\n\n" + IMPOR_GAGAL +
                "\n\nJika menjalankan dari kode sumber, install dulu:\n"
                "pip install pillow pytesseract",
            )
            root.destroy()
            return
        if not siapkan_tesseract():
            messagebox.showwarning(
                NAMA_APLIKASI,
                "Program tesseract-ocr tidak ditemukan.\n\n"
                "Deteksi butuh OCR. Install dari:\n"
                "https://github.com/UB-Mannheim/tesseract/wiki\n"
                "atau pakai PisahDokumen.exe hasil build resmi "
                "(tesseract sudah dibundel di dalamnya).",
            )

    # ---------- pembangun UI ----------
    def _baris_folder(self, induk, label, var, perintah):
        baris = ttk.Frame(induk)
        baris.pack(fill="x", pady=3)
        ttk.Label(baris, text=label, width=22).pack(side="left")
        ttk.Entry(baris, textvariable=var).pack(side="left", fill="x", expand=True, padx=(0, 6))
        ttk.Button(baris, text="Pilih…", command=perintah).pack(side="left")

    def _pilih_input(self):
        folder = filedialog.askdirectory(title="Pilih folder berisi gambar")
        if folder:
            self.var_input.set(folder)

    def _pilih_output(self):
        folder = filedialog.askdirectory(title="Pilih folder hasil")
        if folder:
            self.var_output.set(folder)

    # ---------- proses ----------
    def _mulai(self):
        if self.sedang_jalan:
            return
        sumber = Path(self.var_input.get().strip() or ".")
        if not self.var_input.get().strip() or not sumber.is_dir():
            messagebox.showerror(NAMA_APLIKASI, "Pilih dulu folder berisi gambar.")
            return
        tujuan = Path(self.var_output.get().strip()) if self.var_output.get().strip() else sumber

        gambar = sorted(
            f for f in sumber.iterdir()
            if f.is_file() and f.suffix.lower() in EKSTENSI
        )
        if not gambar:
            messagebox.showerror(NAMA_APLIKASI, f"Tidak ada file gambar di:\n{sumber}")
            return

        self.sedang_jalan = True
        self.tombol_mulai.config(state="disabled")
        self.progres.config(maximum=len(gambar), value=0)
        self._bersihkan_log()
        threading.Thread(
            target=self._proses,
            args=(gambar, tujuan, self.var_aksi.get(), self.var_dry.get()),
            daemon=True,
        ).start()

    def _proses(self, gambar, tujuan, aksi, dry_run):
        hitung = {k: 0 for k in FOLDER}
        self.antrean.put(("log", f"Memproses {len(gambar)} gambar…\n"))

        for i, path in enumerate(gambar, 1):
            try:
                with Image.open(path) as img:
                    lebar, tinggi = img.size
                    teks = pytesseract.image_to_string(img, lang="ind+eng")
            except Exception as e:
                self.antrean.put(("log", f"[{i}] {path.name}: GAGAL ({e})"))
                teks, lebar, tinggi = "", 0, 0

            kategori, skor, cocok = klasifikasi(teks, lebar, tinggi)
            hitung[kategori] += 1
            info = f"  (cocok: {', '.join(cocok[:3])})" if cocok else ""
            self.antrean.put(("log", f"[{i}] {path.name}  ->  {FOLDER[kategori]}{info}"))

            if not dry_run:
                try:
                    folder_kat = tujuan / FOLDER[kategori]
                    folder_kat.mkdir(parents=True, exist_ok=True)
                    target = nama_unik(folder_kat / path.name)
                    if aksi == "pindah":
                        shutil.move(str(path), target)
                    else:
                        shutil.copy2(path, target)
                except Exception as e:
                    self.antrean.put(("log", f"      GAGAL menyimpan: {e}"))

            self.antrean.put(("progres", i))

        ringkas = "\n" + "=" * 46 + "\n"
        for kategori, folder in FOLDER.items():
            ringkas += f"{folder:<20}: {hitung[kategori]} gambar\n"
        ringkas += "=" * 46
        if dry_run:
            ringkas += "\n(hanya deteksi — tidak ada file yang disentuh)"
        else:
            kata = "dipindahkan" if aksi == "pindah" else "disalin"
            ringkas += f"\nSelesai — file {kata} ke: {tujuan}"
        self.antrean.put(("log", ringkas))
        self.antrean.put(("selesai", None))

    # ---------- antrean UI ----------
    def _pompa_antrean(self):
        try:
            while True:
                jenis, data = self.antrean.get_nowait()
                if jenis == "log":
                    self._tulis_log(data)
                elif jenis == "progres":
                    self.progres.config(value=data)
                    self.label_status.config(text=f"Memproses {data}/{int(self.progres['maximum'])}…")
                elif jenis == "selesai":
                    self.sedang_jalan = False
                    self.tombol_mulai.config(state="normal")
                    self.label_status.config(text="Selesai.")
        except queue.Empty:
            pass
        self.root.after(100, self._pompa_antrean)

    def _tulis_log(self, baris):
        self.log.config(state="normal")
        self.log.insert("end", baris + "\n")
        self.log.see("end")
        self.log.config(state="disabled")

    def _bersihkan_log(self):
        self.log.config(state="normal")
        self.log.delete("1.0", "end")
        self.log.config(state="disabled")


def main():
    root = tk.Tk()
    try:
        ttk.Style().theme_use("vista")
    except tk.TclError:
        pass
    Aplikasi(root)
    root.mainloop()


if __name__ == "__main__":
    main()

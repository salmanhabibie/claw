# Script Fix Windows Login Profile Delay
# Jalankan sebagai Administrator!
# PowerShell -NoProfile -ExecutionPolicy Bypass -File fix_windows_login_delay.ps1

Write-Host "Windows Login Delay - Quick Fix Tool" -ForegroundColor Green
Write-Host "Pastikan jalankan sebagai ADMINISTRATOR!" -ForegroundColor Red
Write-Host "`n"

# Menu
do {
    Write-Host "Pilih opsi:" -ForegroundColor Cyan
    Write-Host "1. Bersihkan Temporary Files (temp, prefetch, recycle bin)"
    Write-Host "2. Disable OneDrive Startup (sering penyebab delay)"
    Write-Host "3. Disable Network Discovery (mempercepat login domain)"
    Write-Host "4. Reset Network Stack"
    Write-Host "5. Repair Windows System Files (sfc /scannow)"
    Write-Host "6. Jalankan Disk Cleanup"
    Write-Host "7. Check Disk Bad Sectors (chkdsk - perlu restart)"
    Write-Host "8. Update Group Policy (gpupdate)"
    Write-Host "9. Jalankan semua fixes di atas"
    Write-Host "0. Keluar"
    Write-Host ""

    $choice = Read-Host "Masukkan pilihan (0-9)"

    switch ($choice) {
        "1" {
            Write-Host "`nBersihkan Temporary Files..." -ForegroundColor Yellow

            # Hapus temp files
            Remove-Item -Path "$env:TEMP\*" -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item -Path "$env:WINDIR\Temp\*" -Recurse -Force -ErrorAction SilentlyContinue

            # Hapus prefetch
            Remove-Item -Path "$env:WINDIR\Prefetch\*" -Force -ErrorAction SilentlyContinue

            # Kosongkan Recycle Bin
            Clear-RecycleBin -Force -ErrorAction SilentlyContinue

            Write-Host "✓ Temporary files dibersihkan" -ForegroundColor Green
        }

        "2" {
            Write-Host "`nDisable OneDrive Startup..." -ForegroundColor Yellow

            $oneDrivePath = "$env:ProgramFiles\Microsoft OneDrive\OneDrive.exe"
            if (Test-Path $oneDrivePath) {
                Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "OneDrive" -Force -ErrorAction SilentlyContinue
                Write-Host "✓ OneDrive startup disabled" -ForegroundColor Green
            } else {
                Write-Host "⚠️  OneDrive tidak terinstall" -ForegroundColor Yellow
            }
        }

        "3" {
            Write-Host "`nDisable Network Discovery (Group Policy)..." -ForegroundColor Yellow

            # Disable network discovery service
            Get-Service -Name "NcaSvc" -ErrorAction SilentlyContinue | Set-Service -StartupType Disabled
            Get-Service -Name "fdPHost" -ErrorAction SilentlyContinue | Set-Service -StartupType Disabled

            Write-Host "✓ Network Discovery disabled" -ForegroundColor Green
            Write-Host "⚠️  Perlu restart untuk efek penuh" -ForegroundColor Yellow
        }

        "4" {
            Write-Host "`nReset Network Stack (Winsock Reset)..." -ForegroundColor Yellow

            netsh int ip reset
            netsh winsock reset catalog

            Write-Host "✓ Network stack reset" -ForegroundColor Green
            Write-Host "⚠️  PERLU RESTART COMPUTER!" -ForegroundColor Red
        }

        "5" {
            Write-Host "`nRepair Windows System Files..." -ForegroundColor Yellow
            Write-Host "Ini bisa memakan waktu 15-30 menit..." -ForegroundColor Yellow

            sfc /scannow

            Write-Host "`n✓ System file check selesai" -ForegroundColor Green
        }

        "6" {
            Write-Host "`nJalankan Disk Cleanup..." -ForegroundColor Yellow
            Write-Host "GUI akan terbuka. Pilih items untuk dihapus." -ForegroundColor Yellow

            cleanmgr
        }

        "7" {
            Write-Host "`nJalankan Disk Check (chkdsk)..." -ForegroundColor Yellow
            Write-Host "⚠️  PERLU RESTART! Disk akan dicek saat boot berikutnya." -ForegroundColor Red
            $confirm = Read-Host "Lanjutkan? (y/n)"

            if ($confirm -eq "y") {
                chkdsk /f
                Write-Host "✓ Disk check di-schedule untuk boot berikutnya" -ForegroundColor Green
            }
        }

        "8" {
            Write-Host "`nUpdate Group Policy..." -ForegroundColor Yellow

            gpupdate /force

            Write-Host "✓ Group policy updated" -ForegroundColor Green
        }

        "9" {
            Write-Host "`nMenjalankan SEMUA fixes..." -ForegroundColor Yellow

            # 1. Temp files
            Write-Host "1/8 Bersihkan temp files..." -ForegroundColor Cyan
            Remove-Item -Path "$env:TEMP\*" -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item -Path "$env:WINDIR\Temp\*" -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item -Path "$env:WINDIR\Prefetch\*" -Force -ErrorAction SilentlyContinue
            Clear-RecycleBin -Force -ErrorAction SilentlyContinue

            # 2. OneDrive
            Write-Host "2/8 Disable OneDrive..." -ForegroundColor Cyan
            Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "OneDrive" -Force -ErrorAction SilentlyContinue

            # 3. Network Discovery
            Write-Host "3/8 Disable Network Discovery..." -ForegroundColor Cyan
            Get-Service -Name "NcaSvc" -ErrorAction SilentlyContinue | Set-Service -StartupType Disabled
            Get-Service -Name "fdPHost" -ErrorAction SilentlyContinue | Set-Service -StartupType Disabled

            # 4. Network Reset
            Write-Host "4/8 Reset Network Stack..." -ForegroundColor Cyan
            netsh int ip reset
            netsh winsock reset catalog

            # 5. SFC Scan
            Write-Host "5/8 Scan System Files (bisa lama)..." -ForegroundColor Cyan
            sfc /scannow

            # 6. DISM Repair
            Write-Host "6/8 Repair Windows Image..." -ForegroundColor Cyan
            DISM /Online /Cleanup-Image /RestoreHealth

            # 7. Group Policy
            Write-Host "7/8 Update Group Policy..." -ForegroundColor Cyan
            gpupdate /force

            # 8. Trim SSD (jika ada)
            Write-Host "8/8 Optimize Storage..." -ForegroundColor Cyan
            Optimize-Volume -DriveLetter C -Defrag -ErrorAction SilentlyContinue

            Write-Host "`n✓ Semua fixes selesai!" -ForegroundColor Green
            Write-Host "⚠️  RESTART COMPUTER SEKARANG!" -ForegroundColor Red
            $restart = Read-Host "Restart sekarang? (y/n)"
            if ($restart -eq "y") {
                Restart-Computer -Force
            }
        }

        "0" {
            Write-Host "Keluar..." -ForegroundColor Yellow
            break
        }

        default {
            Write-Host "Pilihan tidak valid!" -ForegroundColor Red
        }
    }

    if ($choice -ne "0") {
        Write-Host ""
        Read-Host "Tekan Enter untuk lanjut"
        Clear-Host
    }

} while ($choice -ne "0")

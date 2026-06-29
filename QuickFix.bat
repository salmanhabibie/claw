@echo off
REM Windows Login Delay - Quick Fix Batch Script
REM Klik kanan → Run as Administrator

color 0A
title Windows Login Delay - Quick Fix

echo.
echo ========================================
echo Windows Login Delay - Quick Fix Tool
echo ========================================
echo.
echo Pastikan script dijalankan sebagai ADMINISTRATOR!
echo.
pause

:menu
cls
echo ========================================
echo Pilih opsi perbaikan:
echo ========================================
echo.
echo 1. Bersihkan Temporary Files (CEPAT!)
echo 2. Disable OneDrive Startup
echo 3. Update Group Policy
echo 4. Bersihkan Recycle Bin
echo 5. Jalankan Disk Cleanup (GUI)
echo 6. Bersihkan Cache Windows Update
echo 7. Optimize Windows Startup
echo 8. JALANKAN SEMUA FIXES di atas
echo 9. Jalankan PowerShell Full Diagnostic
echo 0. Keluar
echo.
echo ========================================
set /p choice="Masukkan pilihan (0-9): "

if "%choice%"=="1" goto clean_temp
if "%choice%"=="2" goto disable_onedrive
if "%choice%"=="3" goto update_gp
if "%choice%"=="4" goto clean_bin
if "%choice%"=="5" goto disk_cleanup
if "%choice%"=="6" goto update_cache
if "%choice%"=="7" goto optimize
if "%choice%"=="8" goto all_fixes
if "%choice%"=="9" goto powershell_diag
if "%choice%"=="0" goto end
goto menu

:clean_temp
echo.
echo [*] Bersihkan Temporary Files...
del /q /f /s "%TEMP%\*" 2>nul
del /q /f /s "%WINDIR%\Temp\*" 2>nul
del /q /f /s "%WINDIR%\Prefetch\*" 2>nul
echo [+] Temp files dibersihkan!
goto menu

:disable_onedrive
echo.
echo [*] Disable OneDrive Startup...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "OneDrive" /f 2>nul
taskkill /f /im OneDrive.exe 2>nul
echo [+] OneDrive startup disabled!
goto menu

:update_gp
echo.
echo [*] Update Group Policy...
gpupdate /force
echo [+] Group Policy updated!
goto menu

:clean_bin
echo.
echo [*] Bersihkan Recycle Bin...
rd /s /q %SystemDrive%\$Recycle.Bin 2>nul
echo [+] Recycle Bin cleaned!
goto menu

:disk_cleanup
echo.
echo [*] Jalankan Disk Cleanup...
cleanmgr
goto menu

:update_cache
echo.
echo [*] Bersihkan Windows Update Cache...
echo.
echo PERHATIAN: Ini akan menghapus update cache. Windows akan download ulang jika ada update.
set /p confirm="Lanjutkan? (y/n): "
if /i "%confirm%"=="y" (
    takeown /f "%WINDIR%\SoftwareDistribution\Download" /r /d y 2>nul
    icacls "%WINDIR%\SoftwareDistribution\Download" /grant:r "%USERNAME%":F /t 2>nul
    del /q /f /s "%WINDIR%\SoftwareDistribution\Download\*" 2>nul
    echo [+] Update cache dibersihkan!
)
goto menu

:optimize
echo.
echo [*] Optimize Windows Startup Services...
REM Disable unnecessary services
for %%i in (DiagTrack, dmwappushservice, TabletInputService, SharedAccess, lfsvc) do (
    sc config "%%i" start= disabled 2>nul
    net stop "%%i" 2>nul
)
echo [+] Startup services dioptimasi!
goto menu

:all_fixes
echo.
echo [*] JALANKAN SEMUA FIXES...
echo.

echo [1/8] Bersihkan Temp Files...
del /q /f /s "%TEMP%\*" 2>nul
del /q /f /s "%WINDIR%\Temp\*" 2>nul
del /q /f /s "%WINDIR%\Prefetch\*" 2>nul

echo [2/8] Disable OneDrive...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "OneDrive" /f 2>nul
taskkill /f /im OneDrive.exe 2>nul

echo [3/8] Bersihkan Recycle Bin...
rd /s /q %SystemDrive%\$Recycle.Bin 2>nul

echo [4/8] Update Group Policy...
gpupdate /force

echo [5/8] Optimize Startup Services...
for %%i in (DiagTrack, dmwappushservice, TabletInputService, SharedAccess) do (
    sc config "%%i" start= disabled 2>nul
)

echo [6/8] Bersihkan Update Cache...
takeown /f "%WINDIR%\SoftwareDistribution\Download" /r /d y 2>nul
icacls "%WINDIR%\SoftwareDistribution\Download" /grant:r "%USERNAME%":F /t 2>nul
del /q /f /s "%WINDIR%\SoftwareDistribution\Download\*" 2>nul

echo [7/8] System File Check (ini bisa lama)...
sfc /scannow

echo [8/8] DISM Repair...
DISM /Online /Cleanup-Image /RestoreHealth

echo.
echo [+] SEMUA FIXES SELESAI!
echo.
echo ========================================
echo RESTART COMPUTER SEKARANG!
echo ========================================
echo.
set /p restart="Restart sekarang? (y/n): "
if /i "%restart%"=="y" (
    shutdown /r /t 30 /c "Windows Login Delay fixes applied - restarting in 30 seconds"
    echo Computer akan restart dalam 30 detik...
)
goto menu

:powershell_diag
echo.
echo [*] Jalankan PowerShell Diagnostic...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0diagnosa_windows_login.ps1"
pause
goto menu

:end
echo.
echo Terima kasih. Script ditutup.
echo.
pause
exit /b

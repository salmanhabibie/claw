# Script Diagnosa Windows Login Profile Delay
# Jalankan sebagai Administrator
# PowerShell -NoProfile -ExecutionPolicy Bypass -File diagnosa_windows_login.ps1

param([switch]$Export)

$ErrorActionPreference = "SilentlyContinue"
$results = @()

# Fungsi helper untuk print dengan format
function Print-Section {
    param([string]$title)
    Write-Host "`n" -ForegroundColor Cyan
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Host $title -ForegroundColor Green
    Write-Host "=" * 60 -ForegroundColor Cyan
}

function Print-Item {
    param([string]$label, [string]$value, [string]$color = "White")
    Write-Host "$label : " -ForegroundColor Yellow -NoNewline
    Write-Host $value -ForegroundColor $color
}

function Print-Warning {
    param([string]$msg)
    Write-Host "⚠️  $msg" -ForegroundColor Red
}

function Print-OK {
    param([string]$msg)
    Write-Host "✓ $msg" -ForegroundColor Green
}

# 1. CEK STARTUP PROGRAMS
Print-Section "1. STARTUP PROGRAMS (Task Scheduler & Registry)"

$startupRegPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$startupRegPathMachine = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Run"
$startupRegPathW32 = "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Run"

$startupApps = @()
$startupApps += Get-ItemProperty $startupRegPath -ErrorAction SilentlyContinue | Select-Object -Property * -ExcludeProperty PSPath, PSParentPath, PSChildName, PSDrive, PSProvider
$startupApps += Get-ItemProperty $startupRegPathMachine -ErrorAction SilentlyContinue | Select-Object -Property * -ExcludeProperty PSPath, PSParentPath, PSChildName, PSDrive, PSProvider
$startupApps += Get-ItemProperty $startupRegPathW32 -ErrorAction SilentlyContinue | Select-Object -Property * -ExcludeProperty PSPath, PSParentPath, PSChildName, PSDrive, PSProvider

$count = ($startupApps | Measure-Object).Count
if ($count -gt 3) {
    Print-Warning "$count aplikasi di startup (normal: 3-5)"
    $startupApps | ForEach-Object { $_ | Get-Member -MemberType NoteProperty | Where-Object { $_.Name -notmatch "^PS" } | ForEach-Object { Write-Host "  - $($_.Name)" -ForegroundColor Yellow } }
} else {
    Print-OK "$count aplikasi di startup (normal)"
}

# 2. CEK DISK SPACE
Print-Section "2. DISK SPACE"

Get-Volume | Where-Object { $_.DriveLetter -ne $null } | ForEach-Object {
    $percentFree = [math]::Round(($_.SizeRemaining / $_.Size) * 100, 2)
    $label = $_.DriveLetter
    if ($percentFree -lt 15) {
        Print-Warning "Drive $label : $percentFree% free (KRITIS!)"
    } elseif ($percentFree -lt 25) {
        Write-Host "⚠️  Drive $label : $percentFree% free (hampir penuh)" -ForegroundColor Yellow
    } else {
        Print-OK "Drive $label : $percentFree% free"
    }
}

# 3. CEK STARTUP SERVICES
Print-Section "3. STARTUP SERVICES (Slow to Start)"

$slowServices = Get-Service | Where-Object { $_.StartType -eq "Automatic" -and $_.Status -eq "Running" } | ForEach-Object {
    $wmiService = Get-WmiObject Win32_Service -Filter "Name='$($_.Name)'"
    if ($wmiService.ProcessId -eq 0) {
        $_
    }
}

if ($slowServices) {
    Print-Warning "Ditemukan services yang possibly hanging:"
    $slowServices | ForEach-Object { Write-Host "  - $($_.Name) : $($_.DisplayName)" -ForegroundColor Yellow }
} else {
    Print-OK "Semua services berjalan normal"
}

# 4. CEK NETWORK DRIVES / MAPPED DRIVES
Print-Section "4. NETWORK DRIVES"

$netDrives = Get-SmbMapping -ErrorAction SilentlyContinue
if ($netDrives) {
    Print-Warning "Ditemukan network drives (bisa memperlambat login):"
    $netDrives | ForEach-Object {
        Write-Host "  - $($_.LocalPath) → $($_.RemotePath)" -ForegroundColor Yellow
        Write-Host "    Status: $($_.Status)" -ForegroundColor Yellow
    }
} else {
    Print-OK "Tidak ada network drives"
}

# 5. CEK WINDOWS UPDATE STATUS
Print-Section "5. WINDOWS UPDATE"

$updateSession = New-Object -ComObject Microsoft.Update.Session
$updateSearcher = $updateSession.CreateUpdateSearcher()
$searchResult = $updateSearcher.Search("IsInstalled=0")

if ($searchResult.Updates.Count -gt 0) {
    Print-Warning "$($searchResult.Updates.Count) Windows Update pending. Segera install!"
    $searchResult.Updates | ForEach-Object { Write-Host "  - $($_.Title)" -ForegroundColor Yellow }
} else {
    Print-OK "Semua Windows Update terinstall"
}

# 6. CEK DISK HEALTH
Print-Section "6. DISK HEALTH (S.M.A.R.T)"

$diskStatus = Get-PhysicalDisk | ForEach-Object {
    $disk = $_
    $status = $disk.HealthStatus
    $model = $disk.Model

    if ($status -eq "Warning" -or $status -eq "Unhealthy") {
        Print-Warning "Disk $model : Status=$status (PERLU DIGANTI!)"
        $_
    } elseif ($status -eq "Healthy") {
        Print-OK "Disk $model : Healthy"
    }
}

# 7. CEK ANTIVIRUS & SECURITY
Print-Section "7. ANTIVIRUS & SECURITY"

$wmiAV = Get-WmiObject -Namespace "root\SecurityCenter2" -Class AntivirusProduct -ErrorAction SilentlyContinue
if ($wmiAV) {
    Print-Item "Antivirus Terdeteksi" $wmiAV.displayName
    if ($wmiAV.productState -ne 266240) {
        Print-Warning "Antivirus tidak aktif atau outdated!"
    }
} else {
    Print-Warning "Tidak ada antivirus terdeteksi"
}

# 8. CEK TEMPORARY FILES
Print-Section "8. TEMPORARY FILES"

$tempSize = 0
$tempPath = "$env:TEMP"
if (Test-Path $tempPath) {
    $tempSize = (Get-ChildItem -Path $tempPath -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum / 1GB
    Print-Item "Temp folder size" "$([math]::Round($tempSize, 2)) GB"
    if ($tempSize -gt 1) {
        Print-Warning "Temp folder terlalu besar, segera bersihkan"
    }
}

# 9. CEK EVENT VIEWER (Errors at Boot)
Print-Section "9. EVENT VIEWER - BOOT ERRORS (Last 50)"

$bootEvents = Get-EventLog -LogName System -Source "EventLog" -EntryType Error -Newest 50 -ErrorAction SilentlyContinue |
    Where-Object { $_.TimeGenerated -gt (Get-Date).AddDays(-1) }

if ($bootEvents) {
    Print-Warning "Ditemukan $($bootEvents.Count) error events:"
    $bootEvents | ForEach-Object {
        Write-Host "  - $($_.Message.Substring(0, [math]::Min(80, $_.Message.Length)))" -ForegroundColor Yellow
    }
} else {
    Print-OK "Tidak ada error events dalam 24 jam terakhir"
}

# 10. CEK PROCESSES BERJALAN
Print-Section "10. TOP 10 PROCESSES DENGAN MEMORY USAGE"

Get-Process | Sort-Object -Property WorkingSet -Descending | Select-Object -First 10 | ForEach-Object {
    $memMB = [math]::Round($_.WorkingSet / 1MB, 2)
    $name = $_.ProcessName
    if ($memMB -gt 500) {
        Write-Host "  - $name : $memMB MB" -ForegroundColor Yellow
    } else {
        Write-Host "  - $name : $memMB MB"
    }
}

# 11. CEK CREDENTIAL CACHE
Print-Section "11. CREDENTIAL MANAGER"

$credentials = cmdkey /list 2>$null
if ($credentials -match "Target:") {
    Print-Item "Cached Credentials" "Ada (bisa memperlambat login jika banyak)"
} else {
    Print-OK "Tidak ada cached credentials"
}

# 12. CEK GROUP POLICIES
Print-Section "12. GROUP POLICIES"

$gpUpdate = gpupdate /force 2>&1
if ($LASTEXITCODE -eq 0) {
    Print-OK "Group Policy updated successfully"
} else {
    Print-Warning "Group Policy update failed - mungkin penyebab delay"
}

# SUMMARY & REKOMENDASI
Print-Section "REKOMENDASI PERBAIKAN"

$fixes = @(
    "1. Nonaktifkan startup programs yang tidak perlu (msconfig → Startup tab)",
    "2. Bersihkan temp files: Disk Cleanup atau HDD Wipe",
    "3. Install Windows Update pending",
    "4. Disable Network Drive mapping jika tidak perlu",
    "5. Scan disk untuk bad sectors: chkdsk /f (perlu restart)",
    "6. Update driver chipset & network card",
    "7. Cek Event Viewer untuk specific errors",
    "8. Jika HDD tua/rusak, upgrade ke SSD",
    "9. Jalankan System File Checker: sfc /scannow",
    "10. Reset network stack: netsh int ip reset (perlu restart)"
)

$fixes | ForEach-Object { Write-Host $_ -ForegroundColor Cyan }

# Export to file
if ($Export) {
    $reportPath = "$env:UserProfile\Desktop\windows_login_diagnosa.txt"
    Get-Content $PSCommandPath | Out-File -FilePath $reportPath
    Write-Host "`nReport disimpan ke: $reportPath" -ForegroundColor Green
}

Write-Host "`n" -ForegroundColor Cyan
Write-Host "Script diagnosa selesai. Jalankan kembali setelah perbaikan." -ForegroundColor Green

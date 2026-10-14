# Flash the firmware as soon as the board enters download mode (Windows).
#
#   powershell -ExecutionPolicy Bypass -File tools\flash-when-ready.ps1 [-Port COM4]
#
# Then: unplug USB, hold BOOT, plug back in while holding, release after ~3 s.
# Needs PlatformIO (pio) on PATH. Waits up to 20 minutes.
param([string]$Port = "COM4")
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONUTF8 = "1"; $env:PYTHONIOENCODING = "utf-8"
function PortUp { [System.IO.Ports.SerialPort]::GetPortNames() -contains $Port }

Write-Host "Unplug USB, hold BOOT, plug back in while holding, release after ~3 s..."
$deadline = (Get-Date).AddMinutes(20)
$sawGone = -not (PortUp)
while ((Get-Date) -lt $deadline) {
    if (-not $sawGone) {
        if (-not (PortUp)) { $sawGone = $true; Write-Host "Board unplugged - now plug it back in while holding BOOT." }
    } elseif (PortUp) {
        Write-Host "Board found on $Port - flashing..."
        Start-Sleep 3
        pio run -d "$root\firmware" -t upload --upload-port $Port
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Done. Hold PWR 8 s (off), then press PWR once (on)."
            exit 0
        }
        Write-Host "Flash failed - the board probably wasn't in download mode. Try the BOOT sequence again."
        $sawGone = $false
    }
    Start-Sleep -Milliseconds 700
}
Write-Host "Timed out."
exit 1

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [string]$BinFile = "esp32_wake_firmware.bin"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Test-Command($Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

if (-not (Test-Path $BinFile)) {
    throw "Firmware file not found: $BinFile (make sure it is in the same folder as this script)."
}

if (-not (Test-Command "python") -and -not (Test-Command "py")) {
    Write-Host "Python not found. Installing Python via winget..."
    if (-not (Test-Command "winget")) {
        throw "winget not found. Please install Python manually from https://www.python.org/downloads/ and re-run this script."
    }
    winget install --id Python.Python.3.12 -e --source winget --accept-package-agreements --accept-source-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
}

$pythonCmd = if (Test-Command "python") { "python" } else { "py" }

& $pythonCmd -m esptool version 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installing esptool..."
    & $pythonCmd -m pip install --upgrade esptool
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install esptool. Please install it manually: pip install esptool"
    }
}

Write-Host "=== Flashing $BinFile to $Port ==="
& $pythonCmd -m esptool --chip esp32c3 -p $Port -b 921600 write_flash 0x0 $BinFile
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "=== Flash complete ==="

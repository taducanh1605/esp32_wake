param(
    [string]$Port = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Test-Command($Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-Python {
    if (Test-Command "python") { return }
    if (Test-Command "py") { return }

    Write-Host "Python not found. Installing Python via winget..."
    if (-not (Test-Command "winget")) {
        throw "winget not found. Please install Python manually from https://www.python.org/downloads/ and re-run this script."
    }
    winget install --id Python.Python.3.12 -e --source winget --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install Python via winget. Please install it manually."
    }

    # Refresh PATH for the current session
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
}

function Install-PlatformIO {
    if (Test-Command "pio") { return }

    Write-Host "PlatformIO CLI not found. Installing PlatformIO Core..."
    $pythonCmd = if (Test-Command "python") { "python" } else { "py" }

    & $pythonCmd -m pip install --upgrade pip
    & $pythonCmd -m pip install --upgrade platformio
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install PlatformIO via pip. Please install it manually: pip install platformio"
    }

    # PlatformIO installs scripts into the Python Scripts folder; refresh PATH for current session
    $scriptsPath = & $pythonCmd -c "import sysconfig; print(sysconfig.get_path('scripts'))"
    if ($scriptsPath -and (Test-Path $scriptsPath)) {
        $env:Path = "$scriptsPath;$env:Path"
    }

    if (-not (Test-Command "pio")) {
        throw "PlatformIO was installed but 'pio' is still not found on PATH. Please restart your terminal and try again."
    }
}

Install-Python
Install-PlatformIO

$uploadArgs = @("run", "-e", "esp32-c3-devkitm-1", "--target", "upload")

if ($Port) {
    $uploadArgs += @("--upload-port", $Port)
}

Write-Host "=== Flashing ESP32-C3 project ==="
Write-Host "Working directory: $Root"
if ($Port) {
    Write-Host "Using port: $Port"
} else {
    Write-Host "No port provided. PlatformIO will auto-detect the connected board."
}

& pio @uploadArgs

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "=== Flash complete ==="

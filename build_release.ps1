param(
    [string]$OutputName = "esp32_wake_firmware.bin"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Test-Command($Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

if (-not (Test-Command "pio")) {
    throw "PlatformIO CLI not found in PATH. Run .\flash.ps1 once first (it installs Python + PlatformIO), or install PlatformIO manually."
}
if (-not (Test-Command "python")) {
    throw "Python not found in PATH. Run .\flash.ps1 once first (it installs Python), or install Python manually."
}

Write-Host "=== Building firmware ==="
& pio run -e esp32-c3-devkitm-1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$BuildDir = Join-Path $Root ".pio\build\esp32-c3-devkitm-1"
$Bootloader = Join-Path $BuildDir "bootloader.bin"
$Partitions = Join-Path $BuildDir "partitions.bin"
$Firmware = Join-Path $BuildDir "firmware.bin"

# boot_app0.bin (OTA select data) ships with the Arduino ESP32 framework package, not the build output.
$PlatformioHome = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $env:USERPROFILE ".platformio" }
$BootApp0 = Join-Path $PlatformioHome "packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
$EspTool = Join-Path $PlatformioHome "packages\tool-esptoolpy\esptool.py"

foreach ($f in @($Bootloader, $Partitions, $Firmware, $BootApp0, $EspTool)) {
    if (-not (Test-Path $f)) {
        throw "Required file not found: $f"
    }
}

$PartitionBytes = [System.IO.File]::ReadAllBytes($Partitions)
$AppCapacity = 0
for ($offset = 0; $offset + 32 -le $PartitionBytes.Length; $offset += 32) {
    if ([BitConverter]::ToUInt16($PartitionBytes, $offset) -ne 0x50aa) { break }
    if ($PartitionBytes[$offset + 2] -eq 0 -and [BitConverter]::ToUInt32($PartitionBytes, $offset + 4) -eq 0x10000) {
        $AppCapacity = [BitConverter]::ToUInt32($PartitionBytes, $offset + 8)
        break
    }
}
$ImageSize = (Get-Item $Firmware).Length
if ($AppCapacity -eq 0 -or $ImageSize -gt $AppCapacity) {
    throw "Firmware image ($ImageSize bytes) exceeds or has no app partition at 0x10000 ($AppCapacity bytes)."
}
Write-Host "Firmware image fits app partition: $ImageSize / $AppCapacity bytes"

$ReleaseDir = Join-Path $Root "release"
New-Item -ItemType Directory -Force -Path $ReleaseDir | Out-Null
$MergedBin = Join-Path $ReleaseDir $OutputName

Write-Host "=== Merging bootloader + partitions + app into a single flashable image ==="
& python $EspTool --chip esp32c3 merge_bin `
    --flash_mode dio --flash_freq 80m --flash_size 4MB `
    -o $MergedBin `
    0x0 $Bootloader `
    0x8000 $Partitions `
    0xe000 $BootApp0 `
    0x10000 $Firmware

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Copy-Item -Path (Join-Path $Root "flash_merged.ps1") -Destination $ReleaseDir -Force
Copy-Item -Path (Join-Path $Root "flash_merged.bat") -Destination $ReleaseDir -Force
Copy-Item -Path (Join-Path $Root "windows") -Destination $ReleaseDir -Recurse -Force
Copy-Item -Path (Join-Path $Root "README.md") -Destination $ReleaseDir -Force
Copy-Item -Path (Join-Path $Root "ESP32_API.md") -Destination $ReleaseDir -Force

Write-Host "=== Release ready ==="
Write-Host "Share the '$ReleaseDir' folder. It contains:"
Write-Host "  - $OutputName (single image, flash at offset 0x0)"
Write-Host "  - flash_merged.ps1 / flash_merged.bat (flash it with one command, no PlatformIO needed)"
Write-Host "  - windows/install-helper.bat (USB IP helper; run as administrator for boot mode)"
Write-Host "  - README.md / ESP32_API.md (setup instructions and API contract)"

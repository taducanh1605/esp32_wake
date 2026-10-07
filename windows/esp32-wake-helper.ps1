param(
    [switch]$FunctionsOnly,
    [string]$PortName,
    [string]$InterfaceAlias,
    [string]$LogDirectory,
    [ValidateRange(10, 300)][int]$IntervalSeconds = 30
)

$ErrorActionPreference = 'Stop'

function Test-WakeGlobalIPv6([string]$Address) {
    $parsed = $null
    if (-not [System.Net.IPAddress]::TryParse($Address, [ref]$parsed)) { return $false }
    return $parsed.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetworkV6 -and
        (($parsed.GetAddressBytes()[0] -band 0xe0) -eq 0x20)
}

function Test-WakePrivateIPv4([string]$Address) {
    $parsed = $null
    if (-not [System.Net.IPAddress]::TryParse($Address, [ref]$parsed)) { return $false }
    if ($parsed.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) { return $false }
    $bytes = $parsed.GetAddressBytes()
    return $bytes[0] -eq 10 -or ($bytes[0] -eq 172 -and $bytes[1] -ge 16 -and $bytes[1] -le 31) -or
        ($bytes[0] -eq 192 -and $bytes[1] -eq 168)
}

function Select-WakeAddresses([object[]]$Addresses) {
    $preferred = @($Addresses | Where-Object { $_.AddressState -eq 'Preferred' })
    $ipv6 = $preferred | Where-Object { Test-WakeGlobalIPv6 $_.IPAddress } |
        Sort-Object Metric, IsTemporary, IPAddress | Select-Object -First 1
    $ipv4Candidates = @($preferred | Where-Object { Test-WakePrivateIPv4 $_.IPAddress })
    if ($ipv6) {
        $ipv4Candidates = @($ipv4Candidates | Where-Object { $_.InterfaceIndex -eq $ipv6.InterfaceIndex })
    }
    $ipv4 = $ipv4Candidates | Sort-Object Metric, IPAddress | Select-Object -First 1
    return [pscustomobject]@{
        IPv4 = $(if ($ipv4) { [string]$ipv4.IPAddress } else { '' })
        IPv6 = $(if ($ipv6) { ([System.Net.IPAddress]::Parse($ipv6.IPAddress)).ToString() } else { '' })
    }
}

function Get-WakeNetworkAddresses {
    $adapters = @(Get-NetAdapter -Physical | Where-Object { $_.Status -eq 'Up' })
    if ($InterfaceAlias) { $adapters = @($adapters | Where-Object { $_.Name -eq $InterfaceAlias }) }
    $transient = @{}
    foreach ($adapter in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        foreach ($unicast in $adapter.GetIPProperties().UnicastAddresses) {
            $transient[$unicast.Address.ToString()] = $unicast.IsTransient
        }
    }
    $candidates = @()
    foreach ($adapter in $adapters) {
        foreach ($family in @('IPv6', 'IPv4')) {
            $prefix = if ($family -eq 'IPv6') { '::/0' } else { '0.0.0.0/0' }
            $routes = @(Get-NetRoute -InterfaceIndex $adapter.ifIndex -AddressFamily $family -ErrorAction SilentlyContinue |
                Where-Object { $_.DestinationPrefix -eq $prefix })
            if ($routes.Count -eq 0) { continue }
            $ipInterface = Get-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily $family
            $routeMetric = ($routes | Measure-Object RouteMetric -Minimum).Minimum
            foreach ($address in @(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily $family -AddressState Preferred -ErrorAction SilentlyContinue)) {
                if ($address.SkipAsSource) { continue }
                $candidates += [pscustomobject]@{
                    IPAddress = $address.IPAddress
                    InterfaceIndex = $adapter.ifIndex
                    AddressState = [string]$address.AddressState
                    IsTemporary = [bool]$transient[$address.IPAddress]
                    Metric = [int]$ipInterface.InterfaceMetric + [int]$routeMetric
                }
            }
        }
    }
    return Select-WakeAddresses $candidates
}

function Get-WakePorts {
    if ($PortName) { return @($PortName) }
    $ports = @(Get-CimInstance Win32_PnPEntity -Filter "PNPClass = 'Ports'" |
        Where-Object { $_.PNPDeviceID -match '^USB\\VID_(303A|10C4|1A86|0403)' -and $_.Name -match '\(COM\d+\)' } |
        ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } })
    return @($ports | Sort-Object -Unique)
}

function Wait-WakeReply($Serial, [string]$Prefix, [int]$TimeoutSeconds = 5) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        try {
            $line = $Serial.ReadLine().Trim()
            if ($line.StartsWith($Prefix, [System.StringComparison]::Ordinal)) { return $line }
        } catch [System.TimeoutException] { }
    }
    throw 'ESP32 handshake/acknowledgement timed out. Check firmware and close Serial Monitor.'
}

function Open-WakeDevice {
    foreach ($candidate in @(Get-WakePorts)) {
        $serial = $null
        try {
            $serial = New-Object System.IO.Ports.SerialPort($candidate, 115200, 'None', 8, 'One')
            $serial.DtrEnable = $false
            $serial.RtsEnable = $false
            $serial.ReadTimeout = 500
            $serial.WriteTimeout = 1000
            $serial.NewLine = "`n"
            $serial.Encoding = [System.Text.Encoding]::ASCII
            $serial.Open()
            $nonce = [guid]::NewGuid().ToString('N')
            $serial.WriteLine("WAKE_HELLO $nonce")
            $reply = Wait-WakeReply $serial "WAKE_DEVICE $nonce "
            if ($reply -notmatch "^WAKE_DEVICE $nonce ([0-9A-Fa-f:]{17})$") { throw 'Invalid device identity' }
            $identity = "$($Matches[1]) on $candidate"
            if ($script:LastDeviceIdentity -ne $identity) {
                Write-WakeLog "Connected to ESP32 $identity"
                $script:LastDeviceIdentity = $identity
            }
            return [pscustomobject]@{ Serial = $serial; Nonce = $nonce }
        } catch {
            if ($serial) { $serial.Dispose() }
            Write-WakeLog "Cannot use ${candidate}: $($_.Exception.Message)"
        }
    }
    return $null
}

function Send-WakeNetworkReport($Network) {
    $device = Open-WakeDevice
    if (-not $device) { return $null }
    try {
        $ipv4 = if ($Network.IPv4) { $Network.IPv4 } else { '-' }
        $ipv6 = if ($Network.IPv6) { $Network.IPv6 } else { '-' }
        $device.Serial.WriteLine("WAKE_NET $($device.Nonce) $ipv4 $ipv6")
        $reply = Wait-WakeReply $device.Serial "WAKE_ACK $($device.Nonce) "
        if ($reply -notmatch "^WAKE_ACK $($device.Nonce) (AUTO|MANUAL)$") {
            throw 'ESP32 rejected network addresses'
        }
        return [pscustomobject]@{ IPv4 = $ipv4; IPv6 = $ipv6; Reply = $reply }
    } finally {
        $device.Serial.Dispose()
    }
}

function Write-WakeLog([string]$Message) {
    $line = '{0:u} {1}' -f [datetime]::Now, $Message
    Write-Host $line
    if ($script:LogPath) {
        try {
            if ((Test-Path $script:LogPath) -and (Get-Item $script:LogPath).Length -gt 1MB) {
                Move-Item $script:LogPath ($script:LogPath + '.old') -Force
            }
            Add-Content -LiteralPath $script:LogPath -Value $line
        } catch { Write-Warning 'Cannot write helper log.' }
    }
}

if ($FunctionsOnly) { return }

if (-not $LogDirectory) { $LogDirectory = Join-Path $env:LOCALAPPDATA 'ESP32Wake' }
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$script:LogPath = Join-Path $LogDirectory 'helper.log'
$mutex = New-Object System.Threading.Mutex($false, 'Local\ESP32WakeHelper')
$ownsMutex = $false
try {
    try { $ownsMutex = $mutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { Write-WakeLog 'Helper already running.'; return }
    Write-WakeLog 'Helper started; waiting for ESP32 USB serial device.'
    $lastAddresses = ''
    $lastSent = [datetime]::MinValue
    while ($true) {
        $waitSeconds = [Math]::Min($IntervalSeconds, 60)
        try {
            $network = Get-WakeNetworkAddresses
            $addresses = "$($network.IPv4)|$($network.IPv6)"
            if ($addresses -ne $lastAddresses -or ([datetime]::UtcNow - $lastSent).TotalSeconds -ge 60) {
                $report = Send-WakeNetworkReport $network
                if ($report) {
                    if ($addresses -ne $lastAddresses) {
                        Write-WakeLog "Network IPv4=$($report.IPv4) IPv6=$($report.IPv6); $($report.Reply)"
                    }
                    $lastAddresses = $addresses
                    $lastSent = [datetime]::UtcNow
                } else {
                    $waitSeconds = 10
                }
            }
        } catch {
            Write-WakeLog $_.Exception.Message
            $waitSeconds = 10
        }
        Start-Sleep -Seconds $waitSeconds
    }
} finally {
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
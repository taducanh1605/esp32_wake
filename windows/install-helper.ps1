param([switch]$Uninstall, [switch]$AtBoot)
$ErrorActionPreference = 'Stop'
$taskName = 'ESP32WakeHelper'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$AtBoot = $AtBoot -or $admin
if ($AtBoot -and -not $admin) { throw 'Boot mode requires an Administrator terminal. Run install-helper.bat as administrator, or omit -AtBoot for sign-in mode.' }
$root = if ($AtBoot) { Join-Path $env:ProgramFiles 'ESP32Wake' } else { Join-Path $env:LOCALAPPDATA 'ESP32Wake' }
$helper = Join-Path $root 'esp32-wake-helper.ps1'
$startup = Join-Path ([Environment]::GetFolderPath('Startup')) 'ESP32 Wake Helper.lnk'
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (-not $AtBoot -and (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)) {
    throw 'Boot-mode helper already installed. Manage it from an Administrator terminal using -AtBoot.'
}

function Stop-InstalledHelper([string]$Path) {
    Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" |
        Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and $_.CommandLine.Contains('"' + $Path + '"') } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
}

if ($AtBoot) {
    $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($task) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
}
Stop-InstalledHelper $helper
if (Test-Path $startup) { Remove-Item -LiteralPath $startup }
if ($Uninstall) {
    if (Test-Path $helper) { Remove-Item -LiteralPath $helper }
    Write-Host 'Helper removed. Logs were kept.'
    return
}

New-Item -ItemType Directory -Path $root -Force | Out-Null
if ($AtBoot) {
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetOwner(([Security.Principal.SecurityIdentifier]'S-1-5-32-544'))
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $rule = New-Object Security.AccessControl.FileSystemAccessRule(
            ([Security.Principal.SecurityIdentifier]$sid), 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule(
        ([Security.Principal.SecurityIdentifier]'S-1-5-32-545'), 'ReadAndExecute', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
    Set-Acl -LiteralPath $root -AclObject $acl
    Stop-InstalledHelper (Join-Path $env:LOCALAPPDATA 'ESP32Wake\esp32-wake-helper.ps1')
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'esp32-wake-helper.ps1') -Destination $helper -Force
Unblock-File -LiteralPath $helper
if ($AtBoot) {
    $fileAcl = New-Object Security.AccessControl.FileSecurity
    $fileAcl.SetOwner(([Security.Principal.SecurityIdentifier]'S-1-5-32-544'))
    $fileAcl.SetAccessRuleProtection($false, $false)
    Set-Acl -LiteralPath $helper -AclObject $fileAcl
}
$arguments = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $helper + '"'
if ($AtBoot) {
    $logDirectory = Join-Path $env:ProgramData 'ESP32Wake'
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    $arguments += ' -LogDirectory "' + $logDirectory + '"'
    $action = New-ScheduledTaskAction -Execute $powershell -Argument $arguments
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([timespan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName $taskName
    Write-Host "Installed for Windows startup, before sign-in. Log: $logDirectory\helper.log"
} else {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($startup)
    $shortcut.TargetPath = $powershell
    $shortcut.Arguments = $arguments
    $shortcut.WorkingDirectory = $root
    $shortcut.WindowStyle = 7
    $shortcut.Save()
    Start-Process -FilePath $powershell -ArgumentList $arguments
    Write-Host "Installed for this user's sign-in. Log: $root\helper.log"
    Write-Host 'For remote cold boots without sign-in, install using Administrator mode (-AtBoot).'
}
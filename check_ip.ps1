$ping = New-Object System.Net.NetworkInformation.Ping

2..254 | ForEach-Object {
    $ip = "192.168.1.$_"

    try {
        $reply = $ping.Send($ip, 200)

        if ($reply.Status -eq 'Success') {
            try {
                $name = [System.Net.Dns]::GetHostEntry($ip).HostName
            }
            catch {
                $name = "Unknown"
            }

            [PSCustomObject]@{
                IPAddress = $ip
                HostName  = $name
            }
        }
    }
    catch {}
}
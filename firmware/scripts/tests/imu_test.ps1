$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/serial_test.ps1"

function New-ImuPayload([uint32]$Sequence = 1, [byte]$Status = 0, [byte]$Flags = 3, [byte]$RangeG = 8) {
    $data = [byte[]]::new(28)
    [Array]::Copy([BitConverter]::GetBytes($Sequence), 0, $data, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]1000), 0, $data, 4, 4)
    [Array]::Copy([BitConverter]::GetBytes([int16]-8192), 0, $data, 8, 2)
    [Array]::Copy([BitConverter]::GetBytes([int16]8191), 0, $data, 10, 2)
    [Array]::Copy([BitConverter]::GetBytes([int16]-4096), 0, $data, 12, 2)
    $data[14] = 5; $data[15] = $Flags; $data[24] = $Status; $data[25] = $RangeG; $data[26] = 100
    return ,$data
}

$fake = New-FakePort {
    param($p, $r)
    Assert-True ($r.Command -eq 0x30 -and $r.Payload.Length -eq 0) 'Read-only GET_IMU request'
    Add-Response $p $r (New-ImuPayload ([uint32]::MaxValue))
}
$sample = Get-DeviceImu (New-SerialClient $fake)
Assert-True ($sample.Sequence -eq [uint32]::MaxValue -and $sample.TimestampMs -eq 1000) 'Unsigned counters'
Assert-True ($sample.RawX -eq -8192 -and $sample.RawY -eq 8191 -and $sample.RawZ -eq -4096) 'Signed raw axes'
Assert-True ($sample.RangeG -eq 8 -and $sample.Xg -eq -8 -and $sample.Yg -eq (8191 / 1024.0) -and $sample.Zg -eq -4) 'Exact 8g scaling'
Assert-True ($sample.NewData -eq 5 -and $sample.Valid -and $sample.Ready -and $sample.Status -eq 'OK') 'Sample flags'
$fake = New-FakePort { param($p, $r) Add-Response $p $r (New-ImuPayload 1 0 3 2) }
$legacy = Get-DeviceImu (New-SerialClient $fake)
Assert-True ($legacy.RangeG -eq 2 -and $legacy.Xg -eq -2 -and $legacy.Yg -eq (8191 / 4096.0) -and $legacy.Zg -eq -1) 'Legacy 2g firmware remains compatible'
foreach ($invalid in @('length', 'flags', 'range', 'status', 'raw', 'rate', 'freshness')) {
    $script:invalidImu = $invalid
    $fake = New-FakePort {
        param($p, $r)
        $data = New-ImuPayload
        switch ($script:invalidImu) {
            'length' { $data = [byte[]]::new(27) }
            'flags' { $data[15] = 4 }
            'range' { $data[25] = 4 }
            'status' { $data[24] = 10 }
            'raw' { $data[9] = 0x7F }
            'rate' { $data[26] = 50 }
            'freshness' { $data[14] = 8 }
        }
        Add-Response $p $r $data
    }
    Assert-Throws { Get-DeviceImu (New-SerialClient $fake) } 'Invalid GET_IMU'
}
Write-Host 'IMU decoding tests passed.'

. "$PSScriptRoot/../imu.ps1"
$fake = New-FakePort {
    param($p, $r)
    if ($p.Requests.Count -eq 1) { Add-Response $p $r (New-ImuPayload 0 4 2); return }
    $sequence = @(([uint32]::MaxValue - 1), ([uint32]::MaxValue - 1), [uint32]::MaxValue, 0, 2)[$p.Requests.Count - 2]
    Add-Response $p $r (New-ImuPayload $sequence)
}
$previousCulture = [Threading.Thread]::CurrentThread.CurrentCulture
try {
    [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('fr-FR')
    $lines = @(Read-ImuStream (New-SerialClient $fake) -Count 4 -TimeoutMs 1000)
} finally {
    [Threading.Thread]::CurrentThread.CurrentCulture = $previousCulture
}
Assert-True ($lines.Count -eq 5 -and $fake.Requests.Count -eq 6) 'Continuous stream skips duplicate sequences and startup state'
Assert-True ($lines[1] -match ',-8\.000000,7\.999023,-4\.000000,5,0,0,0$') 'CSV uses invariant culture and 8g values'
Assert-True ($lines[3].StartsWith('0,') -and $lines[3].EndsWith(',0')) 'Sequence rollover is continuous'
Assert-True ($lines[4].StartsWith('2,') -and $lines[4].EndsWith(',1')) 'Host sample gaps are reported'

$fake = New-FakePort { param($p, $r) Add-Response $p $r (New-ImuPayload 1 1) }
Assert-Throws { Read-ImuStream (New-SerialClient $fake) -Count 1 } 'BUS_ERROR'
$fake = New-FakePort { param($p, $r) Add-Response $p $r (New-ImuPayload 0 4 2) }
Assert-Throws { Read-ImuStream (New-SerialClient $fake) -Count 1 -TimeoutMs 30 } 'No new IMU'
$fake = New-FakePort { param($p, $r) Add-Response $p $r (New-ImuPayload 1) }
Assert-Throws { Read-ImuStream (New-SerialClient $fake) -Count 2 -TimeoutMs 30 } 'No new IMU'
$fake = New-FakePort {
    param($p, $r)
    $sequence = 1
    if ($p.Requests.Count -gt 1) { $sequence = 0 }
    Add-Response $p $r (New-ImuPayload $sequence)
}
Assert-Throws { Read-ImuStream (New-SerialClient $fake) -Count 2 } 'reset|backwards'
$fake = New-FakePort {
    param($p, $r)
    if ($p.Requests.Count -eq 2) { Add-Response $p $r (New-ImuPayload) }
}
$null = Get-DeviceImu (New-SerialClient $fake) -TimeoutMs 500
Assert-True ($fake.Requests.Count -eq 2) 'Read-only IMU requests retry while silent'

$engine = (Get-Process -Id $PID).Path
foreach ($helpArgument in @('-Help', '-h')) {
    $helpText = & $engine -NoProfile -File "$PSScriptRoot/../imu.ps1" $helpArgument -Port COM_DO_NOT_OPEN -Count 2
    Assert-True ($LASTEXITCODE -eq 0 -and ($helpText -join "`n").Contains('Ctrl+C')) 'Stream help exits before serial I/O'
}
Write-Host 'IMU streaming tests passed.'

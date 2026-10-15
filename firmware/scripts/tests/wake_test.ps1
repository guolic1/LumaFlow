$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/serial_test.ps1"

function New-WakePayload {
    $data = [byte[]]::new(32)
    $values = @([uint32]::MaxValue, [uint32]2, [uint32]3, [uint32]4, [uint32]1500)
    for ($i = 0; $i -lt 5; $i++) {
        [Array]::Copy([BitConverter]::GetBytes([uint32]$values[$i]), 0, $data, $i * 4, 4)
    }
    [Array]::Copy([BitConverter]::GetBytes([int16]-8192), 0, $data, 20, 2)
    [Array]::Copy([BitConverter]::GetBytes([int16]8191), 0, $data, 22, 2)
    $data[26] = 7; $data[27] = 0x0C; $data[30] = 8; $data[31] = 25
    return ,$data
}
$fake = New-FakePort {
    param($p, $r)
    Assert-True ($r.Command -eq 0x32 -and $r.Payload.Length -eq 0) 'Read-only GET_WAKE request'
    if ($p.Requests.Count -eq 2) { Add-Response $p $r (New-WakePayload) }
}
$result = Get-DeviceWake (New-SerialClient $fake) -TimeoutMs 500
Assert-True ($result.SleepEntries -eq [uint32]::MaxValue -and $result.MotionEvents -eq 2) 'Counters'
Assert-True ($result.EmptyInterrupts -eq 3 -and $result.WakeReadErrors -eq 4) 'Wake causes'
Assert-True ($result.LastEventAfterSleepMs -eq 1500 -and $result.LastMotionStatus -eq '0x0C') 'Retained event'
Assert-True ($result.LastXg -eq -8 -and $result.LastYg -eq (8191 / 1024.0)) 'Native 8g scaling'
Assert-True ($result.LastNewData -eq 7 -and $result.LastReadStatus -eq 'OK' -and $result.LastSampleStatus -eq 'OK') 'Validity'
Assert-True ($result.SleepRateHz -eq 25 -and $fake.Requests.Count -eq 2) 'Read-only retry'
foreach ($invalid in @('length', 'new_data', 'read_status', 'sample_status', 'range', 'rate', 'axis')) {
    $script:invalidWake = $invalid
    $fake = New-FakePort {
        param($p, $r)
        $data = New-WakePayload
        switch ($script:invalidWake) {
            'length' { $data = [byte[]]::new(31) }
            'new_data' { $data[26] = 8 }
            'read_status' { $data[28] = 255 }
            'sample_status' { $data[29] = 255 }
            'range' { $data[30] = 0 }
            'rate' { $data[31] = 0 }
            'axis' { $data[21] = 0x80 }
        }
        Add-Response $p $r $data
    }
    Assert-Throws { Get-DeviceWake (New-SerialClient $fake) } 'Invalid GET_WAKE'
}
$fake = New-FakePort {
    param($p, $r)
    $data = New-WakePayload
    $data[28] = 1; $data[29] = 4
    Add-Response $p $r $data
}
$result = Get-DeviceWake (New-SerialClient $fake)
Assert-True ($result.LastReadStatus -eq 'BUS_ERROR' -and $result.LastSampleStatus -eq 'NOT_READY') 'Diagnostic errors remain visible'
Write-Host 'Wake diagnostics decoding tests passed.'

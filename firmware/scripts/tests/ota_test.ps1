$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/serial_test.ps1"

Assert-True ((Get-Crc32 ([Text.Encoding]::ASCII.GetBytes('123456789'))) -eq [uint32]0xCBF43926L) 'CRC32 known vector'
$imageData = [byte[]]::new(249)
[Array]::Copy([BitConverter]::GetBytes([uint32]0x20002000), 0, $imageData, 0, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]0x08002009), 0, $imageData, 4, 4)
for ($i = 8; $i -lt $imageData.Length; $i++) { $imageData[$i] = $i -band 255 }

function New-UpdatePort([string]$Fault = '') {
    $device = New-FakePort {
        param($p, $r)
        switch ($r.Command) {
            2 {
                if ($p.Mode -eq 2) { Add-Response $p $r @(1, 2, 0, 1, 0, 0, 128, 0, 1, 0, 0, 0) }
                elseif ($p.Fault -eq 'delayed-reset' -and $p.Probes -lt 3) { $p.Probes++; return }
                elseif ($p.Probes -eq 0) { $p.Probes++; return }
                else { Add-Response $p $r @(1, 1, 0, 2, 0, 0, 128, 0, 6, 0, 0, 0) }
            }
            0x10 {
                $p.ResetRequests++
                if ($p.Fault -eq 'lost-reset-request' -and $p.ResetRequests -eq 1) { return }
                if ($p.Fault -ne 'stuck-reset') { $p.Mode = 1 }
                if ($p.Fault -eq 'reset-race') { Add-Response $p $r @() 1; return }
                if ($p.Fault -eq 'lost-reset-ack') { return }
                Add-Response $p $r
            }
            0x23 {
                $state = 0
                if ($p.Fault -eq 'receiving') { $state = 1 }
                if ($p.Completed) { $state = 2 }
                $data = [byte[]]@($state, 0, 0, 0) + [BitConverter]::GetBytes([uint32]$p.Received.Count) + [BitConverter]::GetBytes([uint32]$p.Size)
                Add-Response $p $r $data
            }
            0x20 {
                Assert-True ($r.Payload.Length -eq 12) 'BEGIN fields'
                $p.Size = [BitConverter]::ToUInt32($r.Payload, 0)
                $p.Crc = [BitConverter]::ToUInt32($r.Payload, 4)
                $p.Version = [BitConverter]::ToUInt32($r.Payload, 8)
                $maxChunk = 120
                if ($p.Fault -eq 'chunk-limit') { $maxChunk = 7 }
                if ($p.Fault -eq 'small-chunk') { $maxChunk = 118 }
                if ($p.Fault -eq 'bad-begin') { Add-Response $p $r @(120); return }
                Add-Response $p $r ([byte[]]@($maxChunk, 0, 0, 0, 0, 0, 0, 0))
            }
            0x21 {
                Assert-True ([BitConverter]::ToUInt32($r.Payload, 0) -eq $p.Received.Count) 'Sequential offsets'
                $count = $r.Payload.Length - 4
                Assert-True ($count -le 120 -and $count -gt 0) 'Chunk length'
                Assert-True ($count % 8 -eq 0 -or $p.Received.Count + $count -eq $p.Size) 'Chunk alignment'
                $p.Received.AddRange([byte[]]$r.Payload[4..($r.Payload.Length - 1)])
                if ($p.Fault -eq 'lost-ack') { return }
                if ($p.Fault -eq 'flash') { Add-Response $p $r @() 7; return }
                $next = $p.Received.Count
                if ($p.Fault -eq 'wrong-offset') { $next++ }
                Add-Response $p $r ([BitConverter]::GetBytes([uint32]$next))
            }
            0x22 {
                Assert-True ($p.Received.Count -eq $p.Size) 'END after all bytes'
                Assert-True ((Get-Crc32 $p.Received.ToArray()) -eq $p.Crc) 'Image CRC matches BEGIN'
                if ($p.Fault -eq 'verify') { Add-Response $p $r @() 8; return }
                if ($p.Fault -eq 'bad-end') { Add-Response $p $r @(1); return }
                if ($p.Fault -eq 'uncommitted') { Add-Response $p $r; return }
                $p.Completed = $true
                Add-Response $p $r
            }
            0x11 {
                Assert-True $p.Completed 'Boot only after commit'
                $p.BootRequests++
                if ($p.Fault -eq 'boot-rejected') { Add-Response $p $r @() 3; return }
                if ($p.Fault -eq 'lost-boot-request' -and $p.BootRequests -eq 1) { return }
                $p.Booted = $true
                $p.Mode = 2
                if ($p.Fault -eq 'lost-boot-ack') { return }
                Add-Response $p $r
            }
            default { throw 'Unexpected command' }
        }
    }
    $device | Add-Member NoteProperty Fault $Fault
    $device | Add-Member NoteProperty Mode 2
    $device | Add-Member NoteProperty Probes 0
    $device | Add-Member NoteProperty Size 0
    $device | Add-Member NoteProperty Crc 0
    $device | Add-Member NoteProperty Version 0
    $device | Add-Member NoteProperty Received ([Collections.Generic.List[byte]]::new())
    $device | Add-Member NoteProperty Completed $false
    $device | Add-Member NoteProperty Booted $false
    $device | Add-Member NoteProperty ResetRequests 0
    $device | Add-Member NoteProperty BootRequests 0
    return $device
}

$device = New-UpdatePort
Invoke-ApplicationUpdate (New-SerialClient $device) $imageData 123 -TimeoutMs 2000
Assert-Bytes $device.Received.ToArray() $imageData 'Exact image transfer including odd final chunk'
Assert-True ($device.Completed -and $device.Booted -and $device.Version -eq 123) 'Committed image and requested boot'
Assert-True ($device.Probes -eq 1) 'Handshake tolerates reset dropping a probe'
$device = New-UpdatePort
$device.Mode = 1
$device.Probes = 1
Invoke-ApplicationUpdate (New-SerialClient $device) $imageData 0 -NoBoot
Assert-True ($device.Completed -and -not $device.Booted) 'NoBoot leaves committed image in Bootloader'

foreach ($fault in @('lost-reset-request', 'lost-reset-ack', 'delayed-reset', 'reset-race', 'lost-boot-request', 'lost-boot-ack')) {
    $device = New-UpdatePort $fault
    Invoke-ApplicationUpdate (New-SerialClient $device) $imageData 1 -TimeoutMs 1500
    Assert-True ($device.Completed -and $device.Booted) "Recover from $fault"
    $expectedResets = 1
    if ($fault -eq 'lost-reset-request') { $expectedResets = 2 }
    Assert-True ($device.ResetRequests -eq $expectedResets) 'Only repeat reset if the device is still in Application'
    $expectedBoots = 1
    if ($fault -eq 'lost-boot-request') { $expectedBoots = 2 }
    Assert-True ($device.BootRequests -eq $expectedBoots) 'Only repeat boot if the device is still in Bootloader'
}
$device = New-UpdatePort 'stuck-reset'
$timer = [Diagnostics.Stopwatch]::StartNew()
Assert-Throws { Enter-DeviceBootloader (New-SerialClient $device) 300 } 'handshake timed out'
Assert-True ($device.ResetRequests -eq 1) 'Acknowledged reset must not be repeated while waiting'
Assert-True ($timer.ElapsedMilliseconds -ge 300 -and $timer.ElapsedMilliseconds -lt 1000) 'Reset transition has one overall deadline'

$device = New-UpdatePort 'boot-rejected'
Assert-Throws { Invoke-ApplicationUpdate (New-SerialClient $device) $imageData 0 -TimeoutMs 1500 } 'BAD_STATE'
Assert-True ($device.BootRequests -eq 1 -and -not $device.Booted) 'A rejected boot must fail without retry'
$device = New-UpdatePort
$info = Set-DeviceImage (New-SerialClient $device) 2 1000
Assert-True ($info.ImageType -eq 2 -and $device.BootRequests -eq 0) 'Already in Application needs no boot command'

foreach ($fault in @('lost-ack', 'wrong-offset', 'flash', 'verify', 'receiving', 'chunk-limit', 'bad-begin', 'bad-end', 'uncommitted')) {
    $device = New-UpdatePort $fault
    Assert-Throws { Invoke-ApplicationUpdate (New-SerialClient $device) $imageData 0 -TimeoutMs 200 } 'timed out|offset|FLASH_ERROR|VERIFY_ERROR|reset|chunk|END_UPDATE|complete'
    Assert-True (-not $device.Booted) "Do not boot after $fault"
    if ($fault -eq 'lost-ack') {
        Assert-True (@($device.Requests | Where-Object Command -eq 0x21).Count -eq 1) 'Do not replay a non-idempotent WRITE'
    }
    if ($fault -eq 'receiving') {
        Assert-True (@($device.Requests | Where-Object Command -eq 0x20).Count -eq 0) 'Do not erase an active update'
    }
}

$largeImage = [byte[]]::new(57312)
[Array]::Copy($imageData, 0, $largeImage, 0, $imageData.Length)
$device = New-UpdatePort 'small-chunk'
Invoke-ApplicationUpdate (New-SerialClient $device) $largeImage ([uint32]0xFFFFFFFFL) -NoBoot
Assert-Bytes $device.Received.ToArray() $largeImage 'Maximum image and negotiated aligned chunks'
Assert-True ($device.Version -eq [uint32]0xFFFFFFFFL) 'Full uint32 firmware version'
Assert-True ($device.Requests[6].Payload.Length -eq 116) '118-byte chunk limit rounds down to 112'

$badVector = [byte[]]$imageData.Clone()
$badVector[4] = 8
foreach ($invalid in @([byte[]]::new(7), [byte[]]::new(57313), $badVector)) {
    $device = New-UpdatePort
    Assert-Throws { Invoke-ApplicationUpdate (New-SerialClient $device) $invalid 0 } 'size|vector'
    Assert-True ($device.Requests.Count -eq 0) 'Reject invalid image before touching device'
}

$tempFile = Join-Path ([IO.Path]::GetTempPath()) (([Guid]::NewGuid().ToString()) + '.bin')
try {
    [IO.File]::WriteAllBytes($tempFile, $imageData)
    Assert-Bytes (Read-ApplicationImage $tempFile) $imageData 'Read raw binary exactly'
    [IO.File]::WriteAllBytes($tempFile, $badVector)
    Assert-Throws { Read-ApplicationImage $tempFile } 'vector'
    [IO.File]::WriteAllBytes($tempFile, ([byte[]]::new(57313)))
    Assert-Throws { Read-ApplicationImage $tempFile } 'size'
    Assert-Throws { Read-ApplicationImage ($tempFile + '.hex') } 'bin'
} finally {
    if (Test-Path -LiteralPath $tempFile) { Remove-Item -LiteralPath $tempFile }
}
Write-Host 'OTA tests passed.'

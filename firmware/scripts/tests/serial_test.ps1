$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/../lumaflow.ps1"

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}

function Assert-Bytes([byte[]]$Actual, [byte[]]$Expected, [string]$Message) {
    Assert-True ([BitConverter]::ToString($Actual) -eq [BitConverter]::ToString($Expected)) $Message
}

function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    try { & $Action } catch {
        Assert-True ($_.Exception.Message -match $Pattern) "Unexpected error: $_"
        return
    }
    throw "FAIL: Expected error matching $Pattern"
}

function New-FakePort([scriptblock]$Handler) {
    $fake = [pscustomobject]@{
        ReadTimeout = 1000
        WriteTimeout = 1000
        Incoming = [Collections.Generic.Queue[byte]]::new()
        Requests = [Collections.Generic.List[object]]::new()
        Handler = $Handler
    }
    $fake | Add-Member ScriptMethod Write {
        param([byte[]]$Data, [int]$Offset, [int]$Count)
        if ($Count -eq 1 -and $Data[$Offset] -eq 0) { return }
        Assert-True ($Data[$Offset + $Count - 1] -eq 0) 'Request delimiter'
        $frame = ConvertFrom-WireFrame $Data[$Offset..($Offset + $Count - 2)]
        Assert-True ($frame.Flags -eq 0 -and $frame.Status -eq 0) 'Request flags/status'
        $this.Requests.Add($frame)
        & $this.Handler $this $frame
    }
    $fake | Add-Member ScriptMethod ReadByte {
        if ($this.Incoming.Count -eq 0) {
            Start-Sleep -Milliseconds $this.ReadTimeout
            throw [TimeoutException]::new('fake timeout')
        }
        return [int]$this.Incoming.Dequeue()
    }
    return $fake
}

function Add-Response($Port, $Request, [byte[]]$Payload = @(), [byte]$Status = 0) {
    $wire = New-WireFrame $Request.Sequence $Request.Command $Payload 1 $Status
    foreach ($value in $wire) { $Port.Incoming.Enqueue($value) }
}

Assert-True ((Get-Crc16 ([Text.Encoding]::ASCII.GetBytes('123456789'))) -eq 0x29B1) 'CRC16 known vector'
$fakePort = New-FakePort { param($p, $r) Add-Response $p $r $r.Payload }
$client = New-SerialClient $fakePort
Assert-Bytes (Invoke-SerialCommand $client 1 ([byte[]]@(0, 17, 0, 255))) @(0, 17, 0, 255) 'Binary PING'
$client.Sequence = 255
$null = Invoke-SerialCommand $client 1
$null = Invoke-SerialCommand $client 1
Assert-True ($fakePort.Requests[1].Sequence -eq 255 -and $fakePort.Requests[2].Sequence -eq 0) 'Sequence wrap'

$fakePort = New-FakePort {
    param($p, $r)
    foreach ($value in ([byte[]]@(3, 1, 0))) { $p.Incoming.Enqueue($value) }
    $bad = New-WireFrame $r.Sequence $r.Command @(42) 1 0
    $bad[$bad.Length - 2] = $bad[$bad.Length - 2] -bxor 1
    foreach ($value in $bad) { $p.Incoming.Enqueue($value) }
    $stale = [pscustomobject]@{ Sequence = ($r.Sequence + 1) % 256; Command = $r.Command }
    Add-Response $p $stale @(99)
    Add-Response $p $r @(7)
}
Assert-Bytes (Invoke-SerialCommand (New-SerialClient $fakePort) 1) @(7) 'Ignore corrupt and stale frames'
$fakePort = New-FakePort { param($p, $r) Add-Response $p $r @() 7 }
Assert-Throws { Invoke-SerialCommand (New-SerialClient $fakePort) 1 } 'FLASH_ERROR'
$fakePort = New-FakePort { param($p, $r) }
Assert-Throws { Invoke-SerialCommand (New-SerialClient $fakePort) 1 -TimeoutMs 20 } 'timeout|timed out'
Assert-True ($fakePort.Requests.Count -eq 1) 'No retry past a short command deadline'
Assert-Throws { New-WireFrame 0 1 ([byte[]]::new(129)) } '128'
Write-Host 'Serial protocol tests passed.'

$fakePort = New-FakePort {
    param($p, $r)
    if ($p.Requests.Count -eq 3) { Add-Response $p $r @(0, 17, 0, 255) }
}
Assert-Bytes (Invoke-SerialCommand (New-SerialClient $fakePort) 1 @(0, 17, 0, 255) 1000) @(0, 17, 0, 255) 'Retry PING until the device responds'
Assert-True ($fakePort.Requests.Count -eq 3) 'Retry after two dropped requests'
Assert-True ($fakePort.Requests[0].Sequence -eq $fakePort.Requests[2].Sequence) 'Retry keeps the request sequence'
Assert-Bytes $fakePort.Requests[2].Payload $fakePort.Requests[0].Payload 'Retry keeps the payload'

$fakePort = New-FakePort { param($p, $r) }
$timer = [Diagnostics.Stopwatch]::StartNew()
Assert-Throws { Invoke-SerialCommand (New-SerialClient $fakePort) 2 -TimeoutMs 250 } 'timed out'
Assert-True ($fakePort.Requests.Count -ge 2) 'Repeat GET_INFO while the device is silent'
Assert-True ($timer.ElapsedMilliseconds -ge 250 -and $timer.ElapsedMilliseconds -lt 1000) 'One overall deadline across retries'

foreach ($commandId in @(0x10, 0x11, 0x20, 0x21, 0x22)) {
    $fakePort = New-FakePort { param($p, $r) }
    Assert-Throws { Invoke-SerialCommand (New-SerialClient $fakePort) $commandId -TimeoutMs 120 } 'timed out'
    Assert-True ($fakePort.Requests.Count -eq 1) 'Do not blindly replay a command with side effects'
}
$fakePort = New-FakePort { param($p, $r) Add-Response $p $r @() 6 }
Assert-Throws { Invoke-SerialCommand (New-SerialClient $fakePort) 1 } 'BAD_ARGUMENT'
Assert-True ($fakePort.Requests.Count -eq 1) 'A device error is a response, not a reason to retry'

$fakePort = New-FakePort {
    param($p, $r)
    if ($p.Requests.Count -eq 2) { Add-Response $p $r @(2, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0) }
}
$updateStatus = Get-DeviceUpdateStatus (New-SerialClient $fakePort) 1000
Assert-True ($updateStatus.State -eq 'complete' -and $fakePort.Requests.Count -eq 2) 'Status queries retry while silent'
Write-Host 'Serial retry tests passed.'

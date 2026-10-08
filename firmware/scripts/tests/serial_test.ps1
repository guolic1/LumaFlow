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
        if ($this.Incoming.Count -eq 0) { throw [TimeoutException]::new('fake timeout') }
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
Assert-True ($fakePort.Requests.Count -eq 1) 'No implicit command retry'
Assert-Throws { New-WireFrame 0 1 ([byte[]]::new(129)) } '128'
Write-Host 'Serial protocol tests passed.'

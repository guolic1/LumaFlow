$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/serial_test.ps1"

foreach ($duty in @(0, 1, 255, 256, 500, 999, 1000)) {
    $fakePort = New-FakePort { param($p, $r) Add-Response $p $r $r.Payload }
    $result = Set-DevicePwm (New-SerialClient $fakePort) $duty
    Assert-True ($result.Duty -eq $duty -and $result.FrequencyHz -eq 1000) 'PWM result'
    Assert-True ($result.DutyPercent -eq ($duty / 10.0)) 'PWM percentage'
    Assert-True ($fakePort.Requests[0].Command -eq 0x30) 'SET_LED_PWM command'
    Assert-Bytes $fakePort.Requests[0].Payload @(($duty -band 255), ($duty -shr 8)) 'Little-endian duty'
}

$fakePort = New-FakePort { param($p, $r) }
Assert-Throws { Set-DevicePwm (New-SerialClient $fakePort) -1 } '0|1000|range'
Assert-Throws { Set-DevicePwm (New-SerialClient $fakePort) 1001 } '0|1000|range'
Assert-True ($fakePort.Requests.Count -eq 0) 'Reject invalid duty before sending'

foreach ($reply in @(@(), @(0), @(0, 0, 0), @(1, 0), @(233, 3))) {
    $script:pwmReply = [byte[]]$reply
    $fakePort = New-FakePort { param($p, $r) Add-Response $p $r $script:pwmReply }
    Assert-Throws { Set-DevicePwm (New-SerialClient $fakePort) 500 } 'Invalid SET_LED_PWM response'
}

$fakePort = New-FakePort { param($p, $r) Add-Response $p $r @() 6 }
Assert-Throws { Set-DevicePwm (New-SerialClient $fakePort) 500 } 'BAD_ARGUMENT'
$fakePort = New-FakePort { param($p, $r) Add-Response $p $r @() 1 }
Assert-Throws { Set-DevicePwm (New-SerialClient $fakePort) 500 } 'BAD_COMMAND'

$fakePort = New-FakePort {
    param($p, $r)
    if ($p.Requests.Count -eq 3) { Add-Response $p $r $r.Payload }
}
$null = Set-DevicePwm (New-SerialClient $fakePort) 1 -TimeoutMs 1000
Assert-True ($fakePort.Requests.Count -eq 3) 'Retry idempotent PWM setting after a lost ACK'
Assert-Bytes $fakePort.Requests[2].Payload $fakePort.Requests[0].Payload 'Retry preserves duty'
$fakePort = New-FakePort { param($p, $r) }
Assert-Throws { Set-DevicePwm (New-SerialClient $fakePort) 1 -TimeoutMs 250 } 'timed out'
Assert-True ($fakePort.Requests.Count -ge 2) 'Retry PWM until the shared deadline'

$engine = (Get-Process -Id $PID).Path
$scriptPath = Join-Path $PSScriptRoot '../lumaflow.ps1'
foreach ($arguments in @(
    @('-Command', 'pwm', '-Port', 'COM_DO_NOT_OPEN'),
    @('-Command', 'pwm', '-Port', 'COM_DO_NOT_OPEN', '-Duty', '1001'),
    @('-Command', 'info', '-Port', 'COM_DO_NOT_OPEN', '-Duty', '10')
)) {
    $ErrorActionPreference = 'Continue'
    $output = & $engine -NoProfile -File $scriptPath @arguments 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Assert-True ($code -ne 0) 'Invalid PWM arguments fail before opening a port'
    Assert-True (($output -join "`n") -match 'Duty|1000') 'Report the invalid PWM argument'
}
Write-Host 'PWM command tests passed.'

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/serial_test.ps1"

function New-OpeningPort([Exception]$OpenError, [int]$Failures = 2) {
    $fake = [pscustomobject]@{
        PortName = 'COM_FAKE'; Attempts = 0; IsOpen = $false
        OpenError = $OpenError; Failures = $Failures
    }
    $fake | Add-Member ScriptMethod Open {
        $this.Attempts++
        if ($this.Attempts -le $this.Failures) { throw $this.OpenError }
        $this.IsOpen = $true
    }
    return $fake
}

foreach ($failure in @([IO.IOException]::new('Port does not exist'),
    [UnauthorizedAccessException]::new('Port is busy'))) {
    $fake = New-OpeningPort $failure
    Open-DeviceSerialPort $fake
    Assert-True ($fake.IsOpen -and $fake.Attempts -eq 3) 'Keep opening until the unavailable port becomes available'
}
$fake = New-OpeningPort ([ArgumentException]::new('Invalid baud rate'))
Assert-Throws { Open-DeviceSerialPort $fake } 'Invalid baud rate'
Assert-True ($fake.Attempts -eq 1) 'Invalid configuration must fail immediately'
Write-Host 'Serial connection tests passed.'

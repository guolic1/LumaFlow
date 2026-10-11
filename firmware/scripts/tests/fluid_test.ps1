$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/serial_test.ps1"

function New-FluidPayload {
    $data = [byte[]]::new(32)
    $values = @([uint32]::MaxValue, [uint32]1200, [uint32]2400, [uint32]3000, [uint32]7, [uint32]190)
    for ($i = 0; $i -lt 6; $i++) {
        [Array]::Copy([BitConverter]::GetBytes([uint32]$values[$i]), 0, $data, $i * 4, 4)
    }
    $data[24] = 128; $data[26] = 13; $data[27] = 13
    [Array]::Copy([BitConverter]::GetBytes([uint16]3650), 0, $data, 28, 2)
    $data[30] = 50
    return ,$data
}
$fake = New-FakePort {
    param($p, $r)
    Assert-True ($r.Command -eq 0x31 -and $r.Payload.Length -eq 0) 'Read-only GET_FLUID request'
    if ($p.Requests.Count -eq 2) { Add-Response $p $r (New-FluidPayload) }
}
$result = Get-DeviceFluid (New-SerialClient $fake) -TimeoutMs 500
Assert-True ($result.CompletedSteps -eq [uint32]::MaxValue -and $result.LastWorkUs -eq 1200) 'Unsigned stats and work duration'
Assert-True ($result.MaxWorkUs -eq 2400 -and $result.LastStepUs -eq 3000 -and $result.MaxSliceUs -eq 190) 'Timing metrics'
Assert-True ($result.MissedSteps -eq 7 -and $result.StateBytes -eq 3650 -and $result.TargetHz -eq 50) 'Budget and missed steps'
Assert-True ($result.ParticleCount -eq 128 -and $result.GridWidth -eq 13 -and $result.GridHeight -eq 13) 'Solver dimensions'
Assert-True ($result.WorkBudgetPercent -eq 6 -and $fake.Requests.Count -eq 2) 'Read-only retry and 20ms budget conversion'
foreach ($invalid in @('length', 'particles', 'width', 'height', 'rate', 'memory', 'timing')) {
    $script:invalidFluid = $invalid
    $fake = New-FakePort {
        param($p, $r)
        $data = New-FluidPayload
        switch ($script:invalidFluid) {
            'length' { $data = [byte[]]::new(31) }
            'particles' { $data[24] = 0 }
            'width' { $data[26] = 0 }
            'height' { $data[27] = 0 }
            'rate' { $data[30] = 0 }
            'memory' { $data[29] = 255 }
            'timing' { $data[8] = 0; $data[9] = 0 }
        }
        Add-Response $p $r $data
    }
    Assert-Throws { Get-DeviceFluid (New-SerialClient $fake) } 'Invalid GET_FLUID'
}
Write-Host 'Fluid performance decoding tests passed.'

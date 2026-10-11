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
$script:fluidParticles = 64
$fake = New-FakePort {
    param($p, $r)
    Assert-True ($r.Command -eq 0x31 -and $r.Payload.Length -eq 0) 'Read-only GET_FLUID request'
    if ($p.Requests.Count -eq 2) {
        $payload = New-FluidPayload
        $payload[24] = $script:fluidParticles
        Add-Response $p $r $payload
    }
}
$result = Get-DeviceFluid (New-SerialClient $fake) -TimeoutMs 500
Assert-True ($result.CompletedSteps -eq [uint32]::MaxValue -and $result.LastWorkUs -eq 1200) 'Unsigned stats and work duration'
Assert-True ($result.MaxWorkUs -eq 2400 -and $result.LastStepUs -eq 3000 -and $result.MaxSliceUs -eq 190) 'Timing metrics'
Assert-True ($result.MissedSteps -eq 7 -and $result.StateBytes -eq 3650 -and $result.TargetHz -eq 50) 'Budget and missed steps'
Assert-True ($result.ParticleCount -eq 64 -and $result.GridWidth -eq 13 -and $result.GridHeight -eq 13) 'Solver dimensions'
Assert-True ($result.WorkBudgetPercent -eq 6 -and $fake.Requests.Count -eq 2) 'Read-only retry and 20ms budget conversion'
$fake = New-FakePort { param($p, $r) Add-Response $p $r (New-FluidPayload) }
$legacy = Get-DeviceFluid (New-SerialClient $fake)
Assert-True ($legacy.ParticleCount -eq 128) 'Legacy 128-particle firmware remains readable'
$fake = New-FakePort {
    param($p, $r)
    $data = New-FluidPayload
    $data[24] = 64; $data[30] = 30
    Add-Response $p $r $data
}
$result = Get-DeviceFluid (New-SerialClient $fake)
Assert-True ($result.TargetHz -eq 30 -and $result.WorkBudgetPercent -eq 3.6) '30Hz firmware budget conversion'
$fake = New-FakePort {
    param($p, $r)
    $data = New-FluidPayload
    $data[24] = 64; $data[30] = 25
    Add-Response $p $r $data
}
$result = Get-DeviceFluid (New-SerialClient $fake)
Assert-True ($result.TargetHz -eq 25 -and $result.WorkBudgetPercent -eq 3) '25Hz firmware budget conversion'
foreach ($invalid in @('length', 'particles', 'particle_limit', 'width', 'height', 'rate', 'memory', 'timing')) {
    $script:invalidFluid = $invalid
    $fake = New-FakePort {
        param($p, $r)
        $data = New-FluidPayload
        switch ($script:invalidFluid) {
            'length' { $data = [byte[]]::new(31) }
            'particles' { $data[24] = 0 }
            'particle_limit' { $data[24] = 129 }
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

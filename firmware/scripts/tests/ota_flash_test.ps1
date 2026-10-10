$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$engine = (Get-Process -Id $PID).Path
$scriptPath = Join-Path $PSScriptRoot '../ota.ps1'

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}

function Invoke-OtaEntry([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    $output = & $engine -NoProfile -File $scriptPath @Arguments 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    return [pscustomobject]@{ Code = $code; Text = $output -join "`n" }
}

foreach ($helpArgument in @('-Help', '-h')) {
    $result = Invoke-OtaEntry @($helpArgument, '-Image', 'does-not-exist.bin')
    Assert-True ($result.Code -eq 0) "OTA entry help succeeds: $($result.Text)"
    foreach ($expected in @('Usage:', '-Port', '-Configuration', 'Release', '-Image',
        '-BaudRate', '1000000', '-TimeoutMs', '5000', '-FirmwareVersion', '-NoBoot', '-Help')) {
        Assert-True ($result.Text.Contains($expected)) "Help includes $expected"
    }
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('lumaflow-ota-' + [Guid]::NewGuid().ToString('N'))
$firmwareRoot = Join-Path $fixtureRoot 'firmware with spaces'
$fixtureScripts = Join-Path $firmwareRoot 'scripts'
$callerRoot = Join-Path $fixtureRoot 'caller with spaces'
$logPath = Join-Path $fixtureRoot 'calls.jsonl'
$environmentNames = @('LUMAFLOW_OTA_TEST_LOG', 'LUMAFLOW_OTA_TEST_CONFIGURE_EXIT',
    'LUMAFLOW_OTA_TEST_BUILD_EXIT', 'LUMAFLOW_OTA_TEST_WORKER_EXIT', 'LUMAFLOW_OTA_TEST_NO_BIN')
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Read-Calls {
    if (Test-Path -LiteralPath $logPath) {
        Get-Content -LiteralPath $logPath | ForEach-Object { $_ | ConvertFrom-Json }
    }
}

function Reset-Fixture {
    if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath }
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $env:LUMAFLOW_OTA_TEST_LOG = $logPath
}

try {
    $null = New-Item -ItemType Directory -Path $fixtureScripts, $callerRoot, (Join-Path $firmwareRoot '.vscode')
    $invalidImage = Join-Path $fixtureRoot 'invalid.bin'
    [IO.File]::WriteAllBytes($invalidImage, [byte[]]@(0))
    $result = Invoke-OtaEntry @('-Port', 'COM_DO_NOT_OPEN', '-Image', $invalidImage)
    Assert-True ($result.Code -ne 0 -and $result.Text -match 'image size') 'Real OTA worker rejects malformed BIN before serial I/O'

    Copy-Item -LiteralPath $scriptPath -Destination (Join-Path $fixtureScripts 'ota.ps1')
    $scriptPath = Join-Path $fixtureScripts 'ota.ps1'
    $fakeCmake = Join-Path $fixtureRoot 'fake cmake.ps1'
    Set-Content -LiteralPath $fakeCmake -Encoding UTF8 -Value @'
$record = @{ Kind = 'cmake'; Arguments = @($args); Directory = (Get-Location).Path }
$record | ConvertTo-Json -Compress | Add-Content -LiteralPath $env:LUMAFLOW_OTA_TEST_LOG
if ($args[0] -eq '--preset') { exit [int]$env:LUMAFLOW_OTA_TEST_CONFIGURE_EXIT }
if ([int]$env:LUMAFLOW_OTA_TEST_BUILD_EXIT -ne 0) { exit [int]$env:LUMAFLOW_OTA_TEST_BUILD_EXIT }
if ($env:LUMAFLOW_OTA_TEST_NO_BIN -ne '1') {
    $configuration = $args[2].Substring('Application-'.Length)
    $directory = Join-Path (Get-Location).Path "build/Application/$configuration"
    $null = New-Item -ItemType Directory -Force -Path $directory
    Set-Content -LiteralPath (Join-Path $directory 'LumaFlow.bin') -Value 'fixture image'
}
exit 0
'@
    @{ 'cmake.cmakePath' = $fakeCmake } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $firmwareRoot '.vscode/settings.json') -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $fixtureScripts 'lumaflow.ps1') -Encoding UTF8 -Value @'
[CmdletBinding()]
param([string]$Command, [string]$Port, [int]$BaudRate, [int]$TimeoutMs,
    [string]$Image, [uint32]$FirmwareVersion, [switch]$NoBoot)
if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { throw 'Worker received a missing image.' }
@{ Kind = 'ota'; Command = $Command; Port = $Port; BaudRate = $BaudRate; TimeoutMs = $TimeoutMs
    Image = $Image; FirmwareVersion = $FirmwareVersion; NoBoot = $NoBoot.IsPresent
    Verbose = [bool]$PSBoundParameters['Verbose'] } |
    ConvertTo-Json -Compress | Add-Content -LiteralPath $env:LUMAFLOW_OTA_TEST_LOG
exit [int]$env:LUMAFLOW_OTA_TEST_WORKER_EXIT
'@

    Push-Location -LiteralPath $callerRoot
    try {
        Reset-Fixture
        $result = Invoke-OtaEntry @()
        Assert-True ($result.Code -ne 0 -and $result.Text -match 'Port') 'Require port before building'
        Assert-True (@(Read-Calls).Count -eq 0) 'Missing port causes no build or OTA'

        Reset-Fixture
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE')
        Assert-True ($result.Code -eq 0) "Default build and OTA succeed: $($result.Text)"
        $calls = @(Read-Calls)
        Assert-True ($calls.Count -eq 3) 'Configure, build, then OTA'
        Assert-True (($calls[0].Arguments -join '|') -eq '--preset|Application') 'Configure Application only'
        Assert-True (($calls[1].Arguments -join '|') -eq '--build|--preset|Application-Release|--parallel') 'Build Release by default'
        Assert-True ($calls[0].Directory -eq $firmwareRoot -and $calls[1].Directory -eq $firmwareRoot) 'Build relative to the script firmware root'
        $ota = $calls[2]
        Assert-True ($ota.Command -eq 'ota' -and $ota.Port -eq 'COM_FAKE') 'Delegate to existing OTA command'
        Assert-True ($ota.BaudRate -eq 1000000 -and $ota.TimeoutMs -eq 5000) 'Preserve serial defaults'
        Assert-True ($ota.Image -eq (Join-Path $firmwareRoot 'build/Application/Release/LumaFlow.bin')) 'Use generated Release BIN'
        Assert-True ($ota.FirmwareVersion -eq 0 -and -not $ota.NoBoot) 'Default metadata and boot behavior'

        Reset-Fixture
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE', '-Configuration', 'Debug', '-BaudRate', '115200',
            '-TimeoutMs', '1234', '-FirmwareVersion', '4294967295', '-NoBoot', '-Verbose')
        Assert-True ($result.Code -eq 0) "Debug OTA succeeds: $($result.Text)"
        $calls = @(Read-Calls)
        Assert-True ($calls[1].Arguments[2] -eq 'Application-Debug') 'Select Debug build'
        $ota = $calls[2]
        Assert-True ($ota.Image -eq (Join-Path $firmwareRoot 'build/Application/Debug/LumaFlow.bin')) 'Use Debug BIN'
        Assert-True ($ota.BaudRate -eq 115200 -and $ota.TimeoutMs -eq 1234) 'Forward serial options'
        Assert-True ($ota.FirmwareVersion -eq 4294967295 -and $ota.NoBoot -and $ota.Verbose) 'Forward metadata, NoBoot and Verbose'

        Reset-Fixture
        $env:LUMAFLOW_OTA_TEST_CONFIGURE_EXIT = '21'
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE')
        Assert-True ($result.Code -ne 0 -and @(Read-Calls).Count -eq 1) 'Configure failure stops before build or OTA'
        Reset-Fixture
        $env:LUMAFLOW_OTA_TEST_BUILD_EXIT = '37'
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE')
        Assert-True ($result.Code -ne 0 -and @(Read-Calls).Count -eq 2) 'Build failure never flashes a stale BIN'

        Reset-Fixture
        Remove-Item -LiteralPath (Join-Path $firmwareRoot 'build/Application/Release/LumaFlow.bin')
        $env:LUMAFLOW_OTA_TEST_NO_BIN = '1'
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE')
        Assert-True ($result.Code -ne 0 -and @(Read-Calls).Count -eq 2) 'Missing build output stops before OTA'

        Reset-Fixture
        $customImage = Join-Path $callerRoot 'custom app[1].bin'
        Set-Content -LiteralPath $customImage -Value 'custom fixture image'
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE', '-Image', 'custom app[1].bin')
        Assert-True ($result.Code -eq 0) "Existing BIN succeeds: $($result.Text)"
        $calls = @(Read-Calls)
        Assert-True ($calls.Count -eq 1 -and $calls[0].Kind -eq 'ota') 'Explicit image skips CMake'
        Assert-True ($calls[0].Image -eq $customImage) 'Resolve relative image from caller and preserve literal brackets'

        Reset-Fixture
        $env:LUMAFLOW_OTA_TEST_WORKER_EXIT = '9'
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE', '-Image', $customImage)
        Assert-True ($result.Code -eq 9) 'Forward OTA failure exit code'

        Reset-Fixture
        $result = Invoke-OtaEntry @('-Port', 'COM_FAKE', '-Image', 'missing.bin')
        Assert-True ($result.Code -ne 0 -and @(Read-Calls).Count -eq 0) 'Missing explicit image fails before OTA'
    } finally { Pop-Location }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    # Delete only the unique test directory created above, after checking its parent.
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    if ([IO.Path]::GetDirectoryName($resolvedFixture).TrimEnd('\') -ne
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')) {
        throw "Refusing to remove unexpected fixture path: $resolvedFixture"
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
Write-Host 'OTA flash entry tests passed.'

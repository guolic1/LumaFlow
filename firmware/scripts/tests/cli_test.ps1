$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$engine = (Get-Process -Id $PID).Path
$scriptPath = Join-Path $PSScriptRoot '../lumaflow.ps1'

function Assert-Help([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    $output = & $engine -NoProfile -File $scriptPath @Arguments 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "Help failed with exit code ${code}: $output" }
    $text = $output -join "`n"
    foreach ($expected in @('Usage:', '-Help', '-Command', '-Port', '-BaudRate', '1000000',
        '-TimeoutMs', '-Text', '-Image', '-FirmwareVersion', '-NoBoot', '-Verbose',
        'ports', 'info', 'ping', 'enter-bootloader', 'boot-application', 'status', 'ota',
        'Examples:', '.bin', 'Default: 5000', '100 ms', 'Ctrl+C', 'pwm', '-Duty', '1000')) {
        if (-not $text.Contains($expected)) { throw "Help is missing: $expected" }
    }
}

Assert-Help @('-Help')
Assert-Help @('-h')
# Even with an invalid port/image and OTA-only options, help must exit before I/O.
Assert-Help @('-Help', '-Command', 'info', '-Port', 'COM_DO_NOT_OPEN',
    '-Image', 'does-not-exist.bin', '-NoBoot')
Assert-Help @('-Help', '-Command', 'ota', '-Port', 'COM_DO_NOT_OPEN',
    '-Image', 'does-not-exist.bin')
Write-Host 'CLI help tests passed.'

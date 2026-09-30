$ErrorActionPreference = 'Stop'

try {
    $settingsPath = Join-Path $PSScriptRoot 'settings.json'
    if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
        throw 'Copy .vscode/settings.example.json to .vscode/settings.json and fill in the local tool paths.'
    }

    $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
    $configPath = $settings.'lumaflow.openocdConfig'
    if ([string]::IsNullOrWhiteSpace($configPath)) {
        throw 'Set lumaflow.openocdConfig in .vscode/settings.json to the absolute path of your OpenOCD .cfg file. Build is available without a probe configuration.'
    }
    if (-not [System.IO.Path]::IsPathRooted($configPath) -or
        -not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw "OpenOCD configuration must be an existing absolute file path: $configPath"
    }

    $openocdPath = $settings.'lumaflow.openocdPath'
    if ([string]::IsNullOrWhiteSpace($openocdPath) -or
        -not (Test-Path -LiteralPath $openocdPath -PathType Leaf)) {
        throw 'Set lumaflow.openocdPath in .vscode/settings.json to the full path of openocd.exe.'
    }

    $toolchainPath = $settings.'lumaflow.armToolchainPath'
    if ([string]::IsNullOrWhiteSpace($toolchainPath) -or
        -not (Test-Path -LiteralPath (Join-Path $toolchainPath 'arm-none-eabi-gdb.exe') -PathType Leaf)) {
        throw 'Set lumaflow.armToolchainPath in .vscode/settings.json to the bin directory containing arm-none-eabi-gdb.exe.'
    }

    Write-Output "Debug configuration paths are valid: $configPath"
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

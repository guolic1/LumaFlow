<#
.SYNOPSIS
Build and flash the LumaFlow Application through the existing serial OTA tool.
.EXAMPLE
powershell -NoProfile -File scripts/ota.ps1 -Port COM3
.EXAMPLE
powershell -NoProfile -File scripts/ota.ps1 -Port COM3 -Image application.bin
#>
[CmdletBinding()]
param(
    [string]$Port,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Image,
    [ValidateRange(1, 10000000)][int]$BaudRate = 1000000,
    [ValidateRange(1, 60000)][int]$TimeoutMs = 5000,
    [uint32]$FirmwareVersion = 0,
    [switch]$NoBoot,
    [Alias('h')][switch]$Help
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

try {
    if ($Help) {
        Write-Output @'
LumaFlow Application OTA flash

Usage:
  powershell -NoProfile -File scripts/ota.ps1 -Port COM3 [options]
  powershell -NoProfile -File scripts/ota.ps1 -Help

Options:
  -Port <name>               Required serial port, e.g. COM3.
  -Configuration <name>      Debug or Release; default: Release.
  -Image <path>              Flash an existing Application .bin, skipping build.
                            Relative image paths use the caller's directory.
  -BaudRate <number>         Default: 1000000 (8N1).
  -TimeoutMs <milliseconds>  Default: 5000 per command/transition.
  -FirmwareVersion <uint32>  Metadata version; default: 0.
  -NoBoot                   Remain in Bootloader after the update.
  -Verbose                  Print OTA protocol diagnostics.
  -Help, -h                 Show help without building or opening a port.

Without -Image, configure Application and build Application-Release (or Debug)
from this script's firmware directory, then flash its generated LumaFlow.bin.
CMake uses .vscode/settings.json cmake.cmakePath when set, otherwise PATH.
The installed Bootloader is required; OTA erases only the old Application.
Serial port opening waits indefinitely (Ctrl+C cancels); commands use -TimeoutMs.
You can use pwsh instead of powershell for PowerShell 7.
'@
        exit 0
    }
    if ([string]::IsNullOrWhiteSpace($Port)) { throw '-Port is required (for example COM3).' }
    $firmwareRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    if ($PSBoundParameters.ContainsKey('Image')) {
        if ([string]::IsNullOrWhiteSpace($Image) -or -not (Test-Path -LiteralPath $Image -PathType Leaf)) {
            throw 'Specify an existing Application .bin with -Image.'
        }
        $imagePath = (Resolve-Path -LiteralPath $Image).ProviderPath
    } else {
        $cmakeExecutable = 'cmake'
        $settingsPath = Join-Path $firmwareRoot '.vscode/settings.json'
        if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
            $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
            $cmakeProperty = $settings.PSObject.Properties['cmake.cmakePath']
            if ($null -ne $cmakeProperty -and -not [string]::IsNullOrWhiteSpace($cmakeProperty.Value)) {
                $cmakeExecutable = [string]$cmakeProperty.Value
            }
        }
        Push-Location -LiteralPath $firmwareRoot
        try {
            & $cmakeExecutable --preset Application
            if ($LASTEXITCODE -ne 0) { throw "Application configure failed (exit $LASTEXITCODE)." }
            & $cmakeExecutable --build --preset "Application-$Configuration" --parallel
            if ($LASTEXITCODE -ne 0) { throw "Application build failed (exit $LASTEXITCODE)." }
        } finally { Pop-Location }
        $imagePath = Join-Path $firmwareRoot "build/Application/$Configuration/LumaFlow.bin"
        if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
            throw "Build did not generate the Application BIN: $imagePath"
        }
    }

    $otaArguments = @{
        Command = 'ota'; Port = $Port; Image = $imagePath
        BaudRate = $BaudRate; TimeoutMs = $TimeoutMs; FirmwareVersion = $FirmwareVersion
        NoBoot = $NoBoot.IsPresent; Verbose = ($VerbosePreference -ne 'SilentlyContinue')
    }
    # The shared tool owns image validation, serial lifetime and the OTA protocol.
    $LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot 'lumaflow.ps1') @otaArguments
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

<#
.SYNOPSIS
Continuously read LumaFlow IMU samples as CSV (100Hz device sampling).
.EXAMPLE
powershell -NoProfile -File scripts/imu.ps1 -Port COM3
.EXAMPLE
powershell -NoProfile -File scripts/imu.ps1 -Port COM3 -Count 1000
#>
[CmdletBinding()]
param(
    [string]$Port,
    [ValidateRange(1, 10000000)][int]$BaudRate = 1000000,
    [ValidateRange(1, 60000)][int]$TimeoutMs = 5000,
    [ValidateRange(0, 2147483647)][int]$Count = 0,
    [Alias('h')][switch]$Help
)

# Pass overlapping parameters explicitly: dot-sourcing also binds the library's params.
. "$PSScriptRoot/lumaflow.ps1" -Port $Port -BaudRate $BaudRate -TimeoutMs $TimeoutMs -Help:$Help

function Read-ImuStream($Client, [int]$Count = 0, [int]$TimeoutMs = 5000) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $lastUpdate = [Diagnostics.Stopwatch]::StartNew()
    $nextPoll = 0L
    $lastSequence = $null
    $emitted = 0
    Write-Output 'sequence,timestamp_ms,x_g,y_g,z_g,new_data,read_errors,missed_ticks,host_skipped'
    while ($Count -eq 0 -or $emitted -lt $Count) {
        $wait = $nextPoll - $clock.ElapsedMilliseconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
        $remaining = $TimeoutMs - [int]$lastUpdate.ElapsedMilliseconds
        if ($remaining -le 0) {
            throw [TimeoutException]::new("No new IMU read for $TimeoutMs ms.")
        }
        try { $sample = Get-DeviceImu $Client $remaining }
        catch {
            if ($_.Exception.GetBaseException() -is [TimeoutException]) {
                throw [TimeoutException]::new("No new IMU read within $TimeoutMs ms: $($_.Exception.Message)")
            }
            throw
        }
        if ($sample.StatusCode -notin @(0, 4)) {
            throw "IMU $($sample.Status); read_errors=$($sample.ReadErrors), missed_ticks=$($sample.MissedTicks)."
        }
        if ($sample.StatusCode -eq 0 -and $sample.Valid -and $sample.Ready -and
            ($null -eq $lastSequence -or $sample.Sequence -ne $lastSequence)) {
            $hostSkipped = 0L
            if ($null -ne $lastSequence) {
                $delta = ([long]$sample.Sequence - [long]$lastSequence + 4294967296L) % 4294967296L
                if ($delta -ge 2147483648L) { throw 'IMU sequence moved backwards; the device may have reset.' }
                $hostSkipped = $delta - 1
            }
            Write-Output ([string]::Format([Globalization.CultureInfo]::InvariantCulture,
                '{0},{1},{2:F6},{3:F6},{4:F6},{5},{6},{7},{8}',
                $sample.Sequence, $sample.TimestampMs, $sample.Xg, $sample.Yg, $sample.Zg,
                $sample.NewData, $sample.ReadErrors, $sample.MissedTicks, $hostSkipped))
            $lastSequence = $sample.Sequence
            $lastUpdate.Restart()
            $emitted++
        } elseif ($lastUpdate.ElapsedMilliseconds -ge $TimeoutMs) {
            throw [TimeoutException]::new("No new IMU read for $TimeoutMs ms (status $($sample.Status)).")
        }
        $nextPoll = [Math]::Max($nextPoll + 10, $clock.ElapsedMilliseconds)
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    Set-StrictMode -Version Latest
    $serialPort = $null
    try {
        if ($Help) {
            Write-Output @'
LumaFlow continuous IMU reader

Usage:
  powershell -NoProfile -File scripts/imu.ps1 -Port COM3 [options]
  powershell -NoProfile -File scripts/imu.ps1 -Help

Options:
  -Port <name>               Required serial port, e.g. COM3.
  -BaudRate <number>         Default: 1000000 (8N1, no flow control).
  -TimeoutMs <milliseconds>  Default: 5000 per request / no-new-read deadline.
  -Count <number>            Samples to print; default: 0 (run until Ctrl+C).
  -Verbose                  Print protocol diagnostics.
  -Help, -h                 Show help without opening a port.

The Application must support GET_IMU. The device samples at 100Hz; this tool
polls every 10ms and skips repeated sequences. It reads the latest sample,
so slow hosts can miss samples; host_skipped reports gaps between printed rows.
new_data bits 0/1/2 indicate updated X/Y/Z; a read is not always a new conversion.
CSV acceleration is in g. Press Ctrl+C to stop and release the serial port.
Use pwsh instead of powershell for PowerShell 7. Save CSV, for example:
  .\scripts\imu.ps1 -Port COM3 -Count 1000 | Set-Content -Encoding utf8 imu.csv
'@
            exit 0
        }
        if ([string]::IsNullOrWhiteSpace($Port)) { throw '-Port is required (for example COM3).' }
        $serialPort = [IO.Ports.SerialPort]::new($Port, $BaudRate, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
        $serialPort.Handshake = [IO.Ports.Handshake]::None
        $serialPort.DtrEnable = $false
        $serialPort.RtsEnable = $false
        $serialPort.ReadTimeout = $TimeoutMs
        $serialPort.WriteTimeout = $TimeoutMs
        Open-DeviceSerialPort $serialPort
        $serialPort.DiscardInBuffer()
        $serialPort.Write([byte[]]@(0), 0, 1)
        $client = New-SerialClient $serialPort
        $info = Get-DeviceInfo $client $TimeoutMs
        if ($info.ImageType -ne 2 -or ($info.Capabilities -band 8) -eq 0) {
            throw 'The running firmware is not an Application with GET_IMU support.'
        }
        Read-ImuStream $client $Count $TimeoutMs
    } catch {
        [Console]::Error.WriteLine($_.Exception.Message)
        exit 1
    } finally {
        if ($null -ne $serialPort) { $serialPort.Dispose() }
    }
}

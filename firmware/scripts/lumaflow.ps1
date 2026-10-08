<#
.SYNOPSIS
Communicate with LumaFlow firmware over USART1 (2000000 8N1).
.EXAMPLE
pwsh -File scripts/lumaflow.ps1 -Command info -Port COM3
.EXAMPLE
pwsh -File scripts/lumaflow.ps1 -Command ping -Port COM3 -Text hello
.EXAMPLE
pwsh -File scripts/lumaflow.ps1 -Command ota -Port COM3 -Image application.bin -FirmwareVersion 1
#>
[CmdletBinding()]
param(
    [ValidateSet('ports', 'info', 'ping', 'enter-bootloader', 'boot-application', 'status', 'ota')]
    [string]$Command = 'info',
    [string]$Port,
    [ValidateRange(1, 10000000)][int]$BaudRate = 2000000,
    [ValidateRange(1, 60000)][int]$TimeoutMs = 2000,
    [string]$Text = 'LumaFlow',
    [string]$Image,
    [uint32]$FirmwareVersion = 0,
    [switch]$NoBoot
)

function Get-Crc16([byte[]]$Data, [int]$Length = $Data.Length) {
    $crc = 0xFFFF
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = $crc -bxor ([int]$Data[$i] -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if ($crc -band 0x8000) { $crc = ($crc -shl 1) -bxor 0x1021 }
            else { $crc = $crc -shl 1 }
            $crc = $crc -band 0xFFFF
        }
    }
    return $crc
}

function New-WireFrame(
    [byte]$Sequence, [byte]$CommandId, [byte[]]$Payload = @(),
    [byte]$Flags = 0, [byte]$Status = 0
) {
    if ($Payload.Length -gt 128) { throw 'Payload must be at most 128 bytes.' }
    $raw = [byte[]]::new(9 + $Payload.Length)
    $raw[0] = 1
    $raw[1] = $Flags
    $raw[2] = $Sequence
    $raw[3] = $CommandId
    $raw[4] = $Status
    $raw[5] = $Payload.Length
    [Array]::Copy($Payload, 0, $raw, 7, $Payload.Length)
    $crc = Get-Crc16 $raw ($raw.Length - 2)
    $raw[$raw.Length - 2] = $crc -band 255
    $raw[$raw.Length - 1] = $crc -shr 8
    $encoded = [Collections.Generic.List[byte]]::new()
    $encoded.Add(0)
    $codeIndex = 0
    $code = 1
    foreach ($value in $raw) {
        if ($value -eq 0) {
            $encoded[$codeIndex] = $code
            $codeIndex = $encoded.Count
            $encoded.Add(0)
            $code = 1
        } else {
            $encoded.Add($value)
            $code++
            # A frame is smaller than 254 bytes, so no full COBS run is possible.
        }
    }
    $encoded[$codeIndex] = $code
    $encoded.Add(0)
    return ,$encoded.ToArray()
}

function ConvertFrom-WireFrame([byte[]]$Encoded) {
    $raw = [Collections.Generic.List[byte]]::new()
    $index = 0
    if ($Encoded.Length -eq 0 -or $Encoded.Length -gt 139) {
        throw [FormatException]::new('Invalid encoded frame length.')
    }
    while ($index -lt $Encoded.Length) {
        $code = [int]$Encoded[$index++]
        if ($code -eq 0 -or $index + $code - 1 -gt $Encoded.Length) {
            throw [FormatException]::new('Invalid COBS frame.')
        }
        for ($offset = 1; $offset -lt $code; $offset++) {
            $value = $Encoded[$index++]
            if ($value -eq 0) { throw [FormatException]::new('Zero inside COBS frame.') }
            $raw.Add($value)
        }
        if ($code -ne 255 -and $index -lt $Encoded.Length) { $raw.Add(0) }
    }
    if ($raw.Count -lt 9 -or $raw[0] -ne 1) {
        throw [FormatException]::new('Invalid frame header/version.')
    }
    $length = [int]$raw[5] -bor ([int]$raw[6] -shl 8)
    if ($length -gt 128 -or $raw.Count -ne $length + 9) {
        throw [FormatException]::new('Invalid payload length.')
    }
    $bytes = $raw.ToArray()
    $crc = [int]$bytes[$bytes.Length - 2] -bor ([int]$bytes[$bytes.Length - 1] -shl 8)
    if ((Get-Crc16 $bytes ($bytes.Length - 2)) -ne $crc) {
        throw [FormatException]::new('CRC16 mismatch.')
    }
    $payload = [byte[]]::new($length)
    [Array]::Copy($bytes, 7, $payload, 0, $length)
    return [pscustomobject]@{
        Flags = $bytes[1]; Sequence = $bytes[2]; Command = $bytes[3]
        Status = $bytes[4]; Payload = $payload
    }
}

function New-SerialClient($SerialPort) {
    return [pscustomobject]@{
        Port = $SerialPort; Sequence = 0
        Encoded = [Collections.Generic.List[byte]]::new(); Discard = $false
    }
}

function Invoke-SerialCommand(
    $Client, [byte]$CommandId, [byte[]]$Payload = @(), [int]$TimeoutMs = 2000
) {
    $sequence = $Client.Sequence
    $Client.Sequence = ($sequence + 1) % 256
    $wire = New-WireFrame $sequence $CommandId $Payload
    $Client.Port.Write($wire, 0, $wire.Length)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
        $Client.Port.ReadTimeout = [Math]::Max(1, $TimeoutMs - [int]$timer.ElapsedMilliseconds)
        try { $value = $Client.Port.ReadByte() }
        catch {
            if ($_.Exception.GetBaseException() -is [TimeoutException]) { break }
            throw
        }
        if ($value -lt 0) { throw [IO.IOException]::new('Serial port closed while reading.') }
        if ($value -ne 0) {
            if (-not $Client.Discard) {
                if ($Client.Encoded.Count -lt 139) { $Client.Encoded.Add([byte]$value) }
                else { $Client.Encoded.Clear(); $Client.Discard = $true }
            }
            continue
        }
        $encoded = $Client.Encoded.ToArray()
        $discard = $Client.Discard
        $Client.Encoded.Clear()
        $Client.Discard = $false
        if ($discard -or $encoded.Length -eq 0) { continue }
        try { $frame = ConvertFrom-WireFrame $encoded }
        catch [FormatException] { Write-Verbose $_.Exception.Message; continue }
        if ($frame.Flags -ne 1 -or $frame.Sequence -ne $sequence -or $frame.Command -ne $CommandId) {
            Write-Verbose 'Ignoring an unrelated frame.'
            continue
        }
        if ($frame.Status -ne 0) {
            $names = @('OK', 'BAD_COMMAND', 'BAD_LENGTH', 'BAD_STATE', 'UNSUPPORTED_VERSION',
                'INTERNAL_ERROR', 'BAD_ARGUMENT', 'FLASH_ERROR', 'VERIFY_ERROR')
            $name = 'UNKNOWN'
            if ($frame.Status -lt $names.Length) { $name = $names[$frame.Status] }
            throw ('Command 0x{0:X2}: {1} (status {2}).' -f $CommandId, $name, $frame.Status)
        }
        return ,$frame.Payload
    }
    throw [TimeoutException]::new(('Command 0x{0:X2} timed out after {1} ms; device outcome is unknown.' -f $CommandId, $TimeoutMs))
}

function Get-DeviceInfo($Client, [int]$TimeoutMs = 2000) {
    $data = Invoke-SerialCommand $Client 2 -TimeoutMs $TimeoutMs
    if ($data.Length -ne 12 -or $data[0] -ne 1 -or $data[1] -notin @(1, 2)) {
        throw 'Invalid GET_INFO response.'
    }
    return [pscustomobject]@{
        ImageType = [int]$data[1]
        Image = @('Unknown', 'Bootloader', 'Application')[$data[1]]
        Version = '{0}.{1}.{2}' -f $data[2], $data[3], $data[4]
        MaxPayload = [BitConverter]::ToUInt16($data, 6)
        Capabilities = [BitConverter]::ToUInt32($data, 8)
    }
}

function Enter-DeviceBootloader($Client, [int]$TimeoutMs = 2000) {
    $info = Get-DeviceInfo $Client $TimeoutMs
    if ($info.ImageType -eq 1) { return $info }
    if (($info.Capabilities -band 1) -eq 0) { throw 'Device cannot enter Bootloader.' }
    $reply = Invoke-SerialCommand $Client 0x10 -TimeoutMs $TimeoutMs
    if ($reply.Length -ne 0) { throw 'Invalid ENTER_BOOTLOADER response.' }
    # Start immediately, retry only GET_INFO, and delimit any request cut by reset.
    # Keeping each attempt under 200 ms catches the bootloader command window.
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
        $Client.Port.Write([byte[]]@(0), 0, 1)
        try { $info = Get-DeviceInfo $Client 50 }
        catch [TimeoutException] { continue }
        if ($info.ImageType -eq 1) { return $info }
    }
    throw [TimeoutException]::new('Bootloader handshake timed out; reset the board and retry.')
}

function Get-DeviceUpdateStatus($Client, [int]$TimeoutMs = 2000) {
    $data = Invoke-SerialCommand $Client 0x23 -TimeoutMs $TimeoutMs
    if ($data.Length -ne 12 -or $data[0] -gt 3 -or $data[1] -gt 4) {
        throw 'Invalid GET_UPDATE_STATUS response.'
    }
    return [pscustomobject]@{
        State = @('idle', 'receiving', 'complete', 'failed')[$data[0]]
        LastResult = @('OK', 'BAD_STATE', 'BAD_ARGUMENT', 'FLASH_ERROR', 'VERIFY_ERROR')[$data[1]]
        NextOffset = [BitConverter]::ToUInt32($data, 4)
        ImageSize = [BitConverter]::ToUInt32($data, 8)
    }
}

function Get-Crc32([byte[]]$Data) {
    $crc = [uint32]0xFFFFFFFFL
    foreach ($value in $Data) {
        $crc = $crc -bxor [uint32]$value
        for ($bit = 0; $bit -lt 8; $bit++) {
            if ($crc -band 1) { $crc = ($crc -shr 1) -bxor [uint32]0xEDB88320L }
            else { $crc = $crc -shr 1 }
        }
    }
    return [uint32]($crc -bxor [uint32]0xFFFFFFFFL)
}

function Assert-ApplicationImage([byte[]]$Data) {
    if ($Data.Length -lt 8 -or $Data.Length -gt 57312) {
        throw 'Application image size must be 8..57312 bytes.'
    }
    $stack = [BitConverter]::ToUInt32($Data, 0)
    $reset = [BitConverter]::ToUInt32($Data, 4)
    $address = $reset -band 0xFFFFFFFEL
    if ($stack -lt 0x20000000 -or $stack -gt 0x20002000 -or ($stack -band 7) -ne 0 -or
        ($reset -band 1) -eq 0 -or $address -lt 0x08002008 -or $address -ge 0x08002000 + $Data.Length) {
        throw 'Invalid Application vector: expected aligned SRAM stack and Thumb reset entry inside the image at 0x08002000.'
    }
}

function Read-ApplicationImage([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::GetExtension($Path) -ine '.bin') {
        throw 'OTA requires -Image pointing to an Application .bin file (not ELF, HEX or Bootloader).'
    }
    $resolved = (Get-Item -LiteralPath $Path -ErrorAction Stop).FullName
    $stream = [IO.File]::OpenRead($resolved)
    try {
        if ($stream.Length -lt 8 -or $stream.Length -gt 57312) {
            throw 'Application image size must be 8..57312 bytes.'
        }
        $data = [byte[]]::new([int]$stream.Length)
        $offset = 0
        while ($offset -lt $data.Length) {
            $read = $stream.Read($data, $offset, $data.Length - $offset)
            if ($read -eq 0) { throw 'Unexpected end of Application file.' }
            $offset += $read
        }
        Assert-ApplicationImage $data
        return ,$data
    } finally { $stream.Dispose() }
}

function Invoke-ApplicationUpdate(
    $Client, [byte[]]$Data, [uint32]$FirmwareVersion,
    [int]$TimeoutMs = 2000, [switch]$NoBoot
) {
    Assert-ApplicationImage $Data
    $crc = Get-Crc32 $Data
    $info = Enter-DeviceBootloader $Client $TimeoutMs
    if (($info.Capabilities -band 4) -eq 0 -or $info.MaxPayload -lt 12) {
        throw 'Bootloader does not support the Application update protocol.'
    }
    $status = Get-DeviceUpdateStatus $Client $TimeoutMs
    if ($status.State -eq 'receiving') {
        throw 'An update is already receiving; reset the board before starting a new OTA.'
    }
    Write-Host ('Updating {0} bytes, CRC32 0x{1:X8}, firmware version {2}.' -f $Data.Length, $crc, $FirmwareVersion)
    $begin = [BitConverter]::GetBytes([uint32]$Data.Length) + [BitConverter]::GetBytes($crc) + [BitConverter]::GetBytes($FirmwareVersion)
    # Erasing 28 flash pages and verifying the full image need a longer deadline.
    $flashTimeout = [Math]::Max(10000, $TimeoutMs)
    $reply = Invoke-SerialCommand $Client 0x20 $begin $flashTimeout
    if ($reply.Length -ne 8 -or [BitConverter]::ToUInt32($reply, 4) -ne 0) {
        throw 'Invalid BEGIN_UPDATE response/initial offset.'
    }
    $maxChunk = [BitConverter]::ToUInt16($reply, 0)
    $chunkSize = [Math]::Min(120, [Math]::Min($maxChunk, $info.MaxPayload - 4))
    $chunkSize -= $chunkSize % 8
    if ($chunkSize -lt 8) { throw 'Invalid Bootloader chunk limit.' }
    $offset = 0
    try {
        while ($offset -lt $Data.Length) {
            $count = [Math]::Min($chunkSize, $Data.Length - $offset)
            $chunk = [byte[]]::new(4 + $count)
            [Array]::Copy([BitConverter]::GetBytes([uint32]$offset), 0, $chunk, 0, 4)
            [Array]::Copy($Data, $offset, $chunk, 4, $count)
            $reply = Invoke-SerialCommand $Client 0x21 $chunk $TimeoutMs
            if ($reply.Length -ne 4 -or [BitConverter]::ToUInt32($reply, 0) -ne $offset + $count) {
                throw "Invalid WRITE_CHUNK next offset at $offset."
            }
            $offset += $count
            Write-Progress -Activity 'LumaFlow OTA' -Status "$offset / $($Data.Length) bytes" -PercentComplete (100 * $offset / $Data.Length)
        }
        $reply = Invoke-SerialCommand $Client 0x22 -TimeoutMs $flashTimeout
        if ($reply.Length -ne 0) { throw 'Invalid END_UPDATE response.' }
        $status = Get-DeviceUpdateStatus $Client $TimeoutMs
        if ($status.State -ne 'complete' -or $status.LastResult -ne 'OK' -or
            $status.NextOffset -ne $Data.Length -or $status.ImageSize -ne $Data.Length) {
            throw 'Device did not confirm a complete Application update.'
        }
        Write-Host 'Application image verified and committed.'
        if (-not $NoBoot) {
            $reply = Invoke-SerialCommand $Client 0x11 -TimeoutMs $TimeoutMs
            if ($reply.Length -ne 0) { throw 'Invalid BOOT_APPLICATION response.' }
            Write-Host 'Application boot requested.'
        }
    } finally { Write-Progress -Activity 'LumaFlow OTA' -Completed }
}

# Dot sourcing exposes the protocol functions to host tests without opening a port.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    Set-StrictMode -Version Latest
    $serialPort = $null
    try {
        if ($Command -ne 'ota' -and ($NoBoot -or $PSBoundParameters.ContainsKey('Image') -or
            $PSBoundParameters.ContainsKey('FirmwareVersion'))) {
            throw '-Image, -FirmwareVersion and -NoBoot apply only to ota.'
        }
        if ($Command -eq 'ports') {
            [IO.Ports.SerialPort]::GetPortNames() | Sort-Object
            exit 0
        }
        if ([string]::IsNullOrWhiteSpace($Port)) { throw '-Port is required (for example COM3).' }
        if ($Command -eq 'ota') { $imageData = Read-ApplicationImage $Image }
        $payload = [Text.Encoding]::UTF8.GetBytes($Text)
        if ($Command -eq 'ping' -and $payload.Length -gt 32) { throw 'PING text must be at most 32 UTF-8 bytes.' }
        $serialPort = [IO.Ports.SerialPort]::new($Port, $BaudRate, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
        $serialPort.Handshake = [IO.Ports.Handshake]::None
        $serialPort.DtrEnable = $false
        $serialPort.RtsEnable = $false
        $serialPort.ReadTimeout = $TimeoutMs
        $serialPort.WriteTimeout = $TimeoutMs
        $serialPort.Open()
        $serialPort.DiscardInBuffer()
        $serialPort.Write([byte[]]@(0), 0, 1)
        $client = New-SerialClient $serialPort
        switch ($Command) {
            'info' { Get-DeviceInfo $client $TimeoutMs }
            'ping' {
                $reply = Invoke-SerialCommand $client 1 $payload $TimeoutMs
                if ([BitConverter]::ToString($reply) -ne [BitConverter]::ToString($payload)) { throw 'PING echo mismatch.' }
                [Text.Encoding]::UTF8.GetString($reply)
            }
            'enter-bootloader' { Enter-DeviceBootloader $client $TimeoutMs }
            'boot-application' {
                $reply = Invoke-SerialCommand $client 0x11 -TimeoutMs $TimeoutMs
                if ($reply.Length -ne 0) { throw 'Invalid BOOT_APPLICATION response.' }
                Write-Output 'Application boot requested.'
            }
            'status' { Get-DeviceUpdateStatus $client $TimeoutMs }
            'ota' { Invoke-ApplicationUpdate $client $imageData $FirmwareVersion $TimeoutMs -NoBoot:$NoBoot }
        }
    } catch {
        [Console]::Error.WriteLine($_.Exception.Message)
        exit 1
    } finally {
        if ($null -ne $serialPort) { $serialPort.Dispose() }
    }
}

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [string]$TestRoot
)

$ErrorActionPreference = 'Stop'
$PackagePath = [IO.Path]::GetFullPath($PackagePath)
$exe = Join-Path $PackagePath 'sigrok-cli.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Package CLI is missing: $exe" }

$temporaryRoot = if ($TestRoot) {
    [IO.Path]::GetFullPath($TestRoot)
} else {
    Join-Path ([IO.Path]::GetTempPath()) ("probes-sigrok-output-" + [guid]::NewGuid())
}
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
$oldPath = $env:PATH
$oldPythonHome = $env:PYTHONHOME
$oldDecoderDir = $env:SIGROKDECODE_DIR
$oldDecoderPath = $env:SIGROKDECODE_PATH

function Invoke-Sigrok([string[]]$Arguments) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$psi.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
    }
}

function Read-Result([string]$Stdout) {
    $line = @($Stdout -split '\r?\n' | Where-Object { $_.Trim().StartsWith('{') })
    if ($line.Count -ne 1) { throw "Expected one JSON result line, got $($line.Count): $Stdout" }
    return $line[0] | ConvertFrom-Json
}

function Invoke-SigrokStream([string[]]$Arguments) {
    $outFile = [IO.Path]::GetTempFileName()
    $errFile = [IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru `
            -ArgumentList $Arguments -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        $bytes = [IO.File]::ReadAllBytes($outFile)
        $types = New-Object System.Collections.Generic.List[int]
        $frames = New-Object System.Collections.Generic.List[object]
        $offset = 0
        while ($offset + 16 -le $bytes.Length) {
            if (-not ($bytes[$offset] -eq 0x50 -and $bytes[$offset + 1] -eq 0x52 -and
                    $bytes[$offset + 2] -eq 0x42 -and $bytes[$offset + 3] -eq 0x53)) {
                throw "Stream frame magic mismatch at offset $offset."
            }
            $length = [BitConverter]::ToUInt32($bytes, $offset + 12)
            $type = [int]$bytes[$offset + 5]
            $types.Add($type) | Out-Null
            $payload = [Text.Encoding]::UTF8.GetString($bytes, $offset + 16, [int]$length)
            $frames.Add([pscustomobject]@{ Type = $type; Payload = $payload }) | Out-Null
            $offset += 16 + [int]$length
        }
        if ($offset -ne $bytes.Length) { throw 'Stream produced a trailing partial frame.' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Types = $types; Frames = $frames }
    } finally {
        Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
}

function Read-Exact([IO.Stream]$Stream, [int]$Count) {
    $buffer = New-Object byte[] $Count
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw 'Stream closed before a complete frame was read.' }
        $offset += $read
    }
    return ,$buffer
}

function Invoke-SigrokGracefulStop([string[]]$Arguments) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    foreach ($argument in $Arguments) { [void]$psi.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    $frames = New-Object System.Collections.Generic.List[object]
    $stopped = $false
    while ($true) {
        $header = Read-Exact $process.StandardOutput.BaseStream 16
        $length = [BitConverter]::ToUInt32($header, 12)
        $type = [int]$header[5]
        $payload = Read-Exact $process.StandardOutput.BaseStream ([int]$length)
        $text = [Text.Encoding]::UTF8.GetString($payload)
        $frames.Add([pscustomobject]@{ Type = $type; Payload = $text }) | Out-Null
        if ($type -eq 2 -and -not $stopped) {
            $process.StandardInput.WriteLine('{"cmd":"stop"}')
            $process.StandardInput.Flush()
            $stopped = $true
        }
        if ($type -eq 8) { break }
    }
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Frames = $frames; Stderr = $stderr }
}

try {
    $env:PATH = "$PackagePath;$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:PYTHONHOME, Env:SIGROKDECODE_DIR, Env:SIGROKDECODE_PATH -ErrorAction SilentlyContinue

    foreach ($arguments in @(
        @('--version'),
        @('--list-supported'),
        @('-d', 'demo', '--scan'),
        @('-d', 'demo', '--show')
    )) {
        $info = Invoke-Sigrok $arguments
        if ($info.ExitCode -ne 0) { throw "Information command failed: $($arguments -join ' '): $($info.Stderr)" }
    }
    if (@(Get-ChildItem -LiteralPath $temporaryRoot -Directory).Count -ne 0) {
        throw 'Information commands created an artifact directory.'
    }

    $default = Invoke-Sigrok @('-d', 'demo', '--samples', '4')
    if ($default.ExitCode -ne 0) { throw "Default capture failed: $($default.Stderr)" }
    $defaultResult = Read-Result $default.Stdout
    if ($defaultResult.status -ne 'ok' -or -not $defaultResult.startedAt -or -not $defaultResult.finishedAt) {
        throw 'Default capture summary is missing status or timestamps.'
    }
    $capture = @($defaultResult.artifacts | Where-Object { $_.kind -eq 'capture' })
    if ($capture.Count -ne 1 -or -not (Test-Path -LiteralPath $capture[0].path)) {
        throw 'Default capture did not publish capture.sr.'
    }
    $defaultDir = $defaultResult.operationDir
    if (-not (Test-Path -LiteralPath (Join-Path $defaultDir 'result.json'))) {
        throw 'Default capture did not publish result.json.'
    }
    $reopenedDir = Join-Path $temporaryRoot 'reopened-operation'
    $reopened = Invoke-Sigrok @('-i', $capture[0].path, '--output-dir', $reopenedDir)
    if ($reopened.ExitCode -ne 0) { throw "Published capture could not be reopened: $($reopened.Stderr)" }
    if (-not (Test-Path -LiteralPath (Join-Path $reopenedDir 'result.json'))) {
        throw 'Reopened capture did not publish a result summary.'
    }

    $explicitDir = Join-Path $temporaryRoot 'explicit-operation'
    $explicit = Invoke-Sigrok @('-d', 'demo', '--samples', '4', '--output-dir', $explicitDir)
    if ($explicit.ExitCode -ne 0) { throw "Explicit capture failed: $($explicit.Stderr)" }
    foreach ($name in @('capture.sr', 'result.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $explicitDir $name))) {
            throw "Explicit operation is missing $name."
        }
    }
    Set-Content -LiteralPath (Join-Path $temporaryRoot 'occupied') -Value 'keep' -Encoding ascii
    $occupied = Invoke-Sigrok @('-d', 'demo', '--samples', '4', '--output-dir', $temporaryRoot)
    if ($occupied.ExitCode -ne 0) { throw "Existing output directory was rejected: $($occupied.Stderr)" }
    if ((Get-Content -LiteralPath (Join-Path $temporaryRoot 'occupied') -Raw) -ne ("keep" + [Environment]::NewLine)) {
        throw 'Existing output directory contents were modified.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $temporaryRoot 'capture.sr')) -or
        -not (Test-Path -LiteralPath (Join-Path $temporaryRoot 'result.json'))) {
        throw 'Existing output directory did not receive generated artifacts.'
    }

    $mutuallyExclusive = Invoke-Sigrok @('-d', 'demo', '--samples', '4', '--output-mode', 'stdout', '--output-dir', (Join-Path $temporaryRoot 'invalid'))
    if ($mutuallyExclusive.ExitCode -eq 0 -or $mutuallyExclusive.Stderr -notmatch 'output-dir') {
        throw 'stdout mode accepted a conflicting output directory.'
    }

    $direct = Invoke-Sigrok @('-d', 'demo', '--samples', '4', '--output-mode', 'stdout')
    if ($direct.ExitCode -ne 0 -or $direct.Stdout -notmatch '(?m)^D[0-9]+:') {
        throw "Direct capture did not return the data stream: $($direct.Stderr)"
    }

    $stream = Invoke-SigrokStream @('-d', 'demo', '--samples', '4', '--stream')
    if ($stream.ExitCode -ne 0 -or -not ($stream.Types -contains 2) -or -not ($stream.Types -contains 8)) {
        throw "Stream mode did not produce LOGIC + END frames: $($stream.Types -join ',')"
    }
    $unsupportedStreamFormat = Invoke-Sigrok @('-d', 'demo', '--samples', '4', '--stream', '--output-format', 'csv')
    if ($unsupportedStreamFormat.ExitCode -eq 0) {
        throw 'Stream mode silently accepted a file output format it does not support.'
    }
    $plainStreamEnd = @($stream.Frames | Where-Object Type -eq 8 | Select-Object -Last 1)
    if ($plainStreamEnd.Count -ne 1) { throw 'Stream mode did not produce exactly one end frame.' }
    $plainStreamResult = $plainStreamEnd[0].Payload | ConvertFrom-Json
    if (-not $plainStreamResult.capturePath -or -not (Test-Path -LiteralPath $plainStreamResult.capturePath)) {
        throw 'Live stream mode did not publish its native capture.'
    }
    $streamCaptureDir = Join-Path $temporaryRoot 'stream-capture'
    $streamCapture = Invoke-SigrokStream @(
        '-d', 'demo', '--samples', '4', '--stream', '--output-dir', $streamCaptureDir
    )
    if ($streamCapture.ExitCode -ne 0 -or -not ($streamCapture.Types -contains 8)) {
        throw "Stream capture failed: $($streamCapture.Types -join ',')"
    }
    $endFrame = @($streamCapture.Frames | Where-Object Type -eq 8 | Select-Object -Last 1)
    if ($endFrame.Count -ne 1) { throw 'Stream capture did not produce exactly one end frame.' }
    $endPayload = $endFrame[0].Payload | ConvertFrom-Json
    if (-not $endPayload.capturePath -or -not (Test-Path -LiteralPath $endPayload.capturePath)) {
        throw 'Successful stream end did not publish a readable native capture path.'
    }
    if ([int64]$endPayload.totalSamples -ne 4) {
        throw "Stream sample count was $($endPayload.totalSamples), expected 4."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $streamCaptureDir 'result.json'))) {
        throw 'Stream capture did not publish its result manifest.'
    }
    $nativeReadbackDir = Join-Path $temporaryRoot 'native-stream-readback'
    $nativeReadback = Invoke-Sigrok @('-i', $endPayload.capturePath, '--output-dir', $nativeReadbackDir)
    if ($nativeReadback.ExitCode -ne 0) {
        throw "Native Sigrok could not reopen stream capture: $($nativeReadback.Stderr)"
    }
    $nativeReadbackResult = Get-Content -LiteralPath (Join-Path $nativeReadbackDir 'result.json') -Raw | ConvertFrom-Json
    if ($nativeReadbackResult.status -ne 'ok') {
        throw 'Native stream capture replay did not complete successfully.'
    }
    $replayedStream = Invoke-SigrokStream @('-i', $endPayload.capturePath, '--stream')
    if ($replayedStream.ExitCode -ne 0 -or -not ($replayedStream.Types -contains 2) -or
        -not ($replayedStream.Types -contains 8)) {
        throw "Native capture could not be replayed: $($replayedStream.Types -join ',')"
    }
    $replayEnd = @($replayedStream.Frames | Where-Object Type -eq 8 | Select-Object -Last 1)
    if ($replayEnd.Count -ne 1 -or
        [int64](($replayEnd[0].Payload | ConvertFrom-Json).totalSamples) -ne [int64]$endPayload.totalSamples) {
        throw 'Native replay sample count differs from the live stream.'
    }
    $stoppedDir = Join-Path $temporaryRoot 'gracefully-stopped'
    $stoppedStream = Invoke-SigrokGracefulStop @(
        '-d', 'demo', '--stream', '--output-dir', $stoppedDir
    )
    if ($stoppedStream.ExitCode -ne 0) {
        throw "Graceful stream stop did not finalize a native capture: $($stoppedStream.Stderr)"
    }
    $stoppedEnd = @($stoppedStream.Frames | Where-Object Type -eq 8 | Select-Object -Last 1)
    if ($stoppedEnd.Count -ne 1) { throw 'Graceful stop did not produce exactly one end frame.' }
    $stoppedPayload = $stoppedEnd[0].Payload | ConvertFrom-Json
    if (-not $stoppedPayload.capturePath -or -not (Test-Path -LiteralPath $stoppedPayload.capturePath)) {
        throw 'Graceful stop did not publish a finalized .sr artifact.'
    }
    if ([int64]$stoppedPayload.totalSamples -le 0) {
        throw 'Graceful stop did not retain the samples already received.'
    }
    $stoppedReadback = Invoke-Sigrok @('-i', $stoppedPayload.capturePath)
    if ($stoppedReadback.ExitCode -ne 0) {
        throw "Gracefully stopped capture is not readable: $($stoppedReadback.Stderr)"
    }
    $repeatedStream = Invoke-SigrokStream @(
        '-d', 'demo', '--samples', '8', '--stream', '--output-dir', $streamCaptureDir
    )
    if ($repeatedStream.ExitCode -ne 0) {
        throw 'A later capture could not safely replace files in an existing output directory.'
    }
    $repeatedResult = Get-Content -LiteralPath (Join-Path $streamCaptureDir 'result.json') -Raw | ConvertFrom-Json
    if ($repeatedResult.status -ne 'ok' -or
        (Get-Item -LiteralPath (Join-Path $streamCaptureDir 'capture.sr')).Length -le 0) {
        throw 'Repeated capture did not atomically publish a matching result and capture.'
    }
    $missingDevice = Invoke-SigrokStream @(
        '-d', 'no-such-driver', '--samples', '4', '--stream', '--output-dir',
        (Join-Path $temporaryRoot 'failed-stream')
    )
    if ($missingDevice.ExitCode -eq 0 -or
        (Test-Path -LiteralPath (Join-Path $temporaryRoot 'failed-stream/capture.sr'))) {
        throw 'A stream capture that failed before the data header was reported as successful.'
    }
    $capabilities = Invoke-Sigrok @('--capabilities', '--json')
    if ($capabilities.ExitCode -ne 0 -or $capabilities.Stdout -notmatch '"streamProtocol"\s*:\s*1') {
        throw "Capabilities JSON did not advertise streamProtocol 1: $($capabilities.Stdout)"
    }

    Write-Output "Output mode checks passed: $PackagePath"
} finally {
    $env:PATH = $oldPath
    foreach ($entry in @(
        @{ Name = 'PYTHONHOME'; Value = $oldPythonHome },
        @{ Name = 'SIGROKDECODE_DIR'; Value = $oldDecoderDir },
        @{ Name = 'SIGROKDECODE_PATH'; Value = $oldDecoderPath }
    )) {
        if ($null -eq $entry.Value) { Remove-Item "Env:$($entry.Name)" -ErrorAction SilentlyContinue }
        else { Set-Item "Env:$($entry.Name)" $entry.Value }
    }
    if (-not $TestRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

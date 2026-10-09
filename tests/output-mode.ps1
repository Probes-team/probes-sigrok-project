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
        $offset = 0
        while ($offset + 16 -le $bytes.Length) {
            if (-not ($bytes[$offset] -eq 0x50 -and $bytes[$offset + 1] -eq 0x52 -and
                    $bytes[$offset + 2] -eq 0x42 -and $bytes[$offset + 3] -eq 0x53)) {
                throw "Stream frame magic mismatch at offset $offset."
            }
            $length = [BitConverter]::ToUInt32($bytes, $offset + 12)
            $types.Add([int]$bytes[$offset + 5]) | Out-Null
            $offset += 16 + [int]$length
        }
        if ($offset -ne $bytes.Length) { throw 'Stream produced a trailing partial frame.' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Types = $types }
    } finally {
        Remove-Item -LiteralPath $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
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

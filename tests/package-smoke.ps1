[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string[]]$ExpectedDrivers,
    [Parameter(Mandatory)][string[]]$ExpectedDecoders
)

$ErrorActionPreference = 'Stop'
$PackagePath = [IO.Path]::GetFullPath($PackagePath)
$exe = Join-Path $PackagePath 'sigrok-cli.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Package CLI is missing: $exe"
}
$wchDll = Join-Path $PackagePath 'CH375DLL64.DLL'
if (-not (Test-Path -LiteralPath $wchDll)) {
    throw "Packaged WCH runtime DLL is missing: $wchDll"
}
$driverRoot = Join-Path $PackagePath 'driver-install/wch-ch375'
$driverHelper = Join-Path $PackagePath 'wch-driver-installer.exe'
if (-not (Test-Path -LiteralPath $driverHelper -PathType Leaf)) {
    throw 'Packaged WCH driver installer helper is missing.'
}
$driverManifestPath = Join-Path $driverRoot 'driver-manifest.json'
if (-not (Test-Path -LiteralPath $driverManifestPath)) {
    throw "Packaged WCH driver manifest is missing: $driverManifestPath"
}
$driverManifest = Get-Content -LiteralPath $driverManifestPath -Raw | ConvertFrom-Json
if ($driverManifest.id -ne 'wch-ch375-x64' -or $driverManifest.architecture -ne 'amd64') {
    throw 'Packaged WCH driver manifest has unexpected identity or architecture.'
}
foreach ($driverFile in @($driverManifest.files)) {
    $driverPath = Join-Path (Join-Path $driverRoot 'x64') ($driverFile.path.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $driverPath -PathType Leaf)) {
        throw "Packaged WCH driver file is missing: $($driverFile.path)"
    }
    $item = Get-Item -LiteralPath $driverPath
    $hash = (Get-FileHash -LiteralPath $driverPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($item.Length -ne [int64]$driverFile.size -or $hash -ne $driverFile.sha256.ToLowerInvariant()) {
        throw "Packaged WCH driver file hash/size mismatch: $($driverFile.path)"
    }
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("probes-sigrok-smoke-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
$previousLocation = Get-Location
$oldPath = $env:PATH
$oldPythonHome = $env:PYTHONHOME
$oldDecoderDir = $env:SIGROKDECODE_DIR
$oldDecoderPath = $env:SIGROKDECODE_PATH
$oldFirmwareDir = $env:SIGROK_FIRMWARE_DIR
$oldFirmwarePath = $env:SIGROK_FIRMWARE_PATH
try {
    Set-Location $temporaryRoot
    $env:PATH = "$PackagePath;$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:PYTHONHOME, Env:SIGROKDECODE_DIR, Env:SIGROKDECODE_PATH, Env:SIGROK_FIRMWARE_DIR, Env:SIGROK_FIRMWARE_PATH -ErrorAction SilentlyContinue

    $version = @(& $exe --version 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "--version failed: $($version -join [Environment]::NewLine)" }
    $supported = @(& $exe --list-supported 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "--list-supported failed: $($supported -join [Environment]::NewLine)" }
    $scan = @(& $exe -d demo --scan 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "demo --scan failed: $($scan -join [Environment]::NewLine)" }
    $show = @(& $exe -d demo --show 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "demo --show failed: $($show -join [Environment]::NewLine)" }
    $help = @(& $exe --help 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "--help failed: $($help -join [Environment]::NewLine)" }
    $helpText = $help -join [Environment]::NewLine
    if ($helpText -notmatch '--output-mode' -or $helpText -notmatch '--output-dir' -or
        $helpText -match '--output-file') {
        throw 'The packaged CLI output contract is missing or still exposes --output-file.'
    }

    $capture = @(& $exe -d demo --samples 4 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Default demo capture failed: $($capture -join [Environment]::NewLine)" }
    $captureJson = @($capture | Where-Object { ([string]$_).Trim().StartsWith('{') })
    if ($captureJson.Count -ne 1) { throw 'Default demo capture did not return one JSON summary line.' }
    $captureResult = $captureJson[0] | ConvertFrom-Json
    if ($captureResult.status -ne 'ok') { throw 'Default demo capture summary is not successful.' }
    $direct = @(& $exe -d demo --samples 4 --output-mode stdout 2>&1)
    if ($LASTEXITCODE -ne 0 -or ($direct -join "`n") -notmatch '(?m)^D[0-9]+:') {
        throw 'Explicit stdout mode did not return the demo data stream.'
    }

    $supportedText = $supported -join "`n"
    foreach ($driver in $ExpectedDrivers) {
        if ($supportedText -notmatch "(?m)^\s{2}$([regex]::Escape($driver))\s") {
            throw "Built driver '$driver' is absent from --list-supported."
        }
    }
    foreach ($decoder in ($ExpectedDecoders | Where-Object { $_ -ne 'common' })) {
        if ($supportedText -notmatch "(?m)^\s{2}$([regex]::Escape($decoder))\s") {
            throw "Packaged decoder '$decoder' is absent from --list-supported."
        }
    }
} finally {
    Set-Location $previousLocation
    $env:PATH = $oldPath
    foreach ($entry in @(
        @{ Name = 'PYTHONHOME'; Value = $oldPythonHome },
        @{ Name = 'SIGROKDECODE_DIR'; Value = $oldDecoderDir },
        @{ Name = 'SIGROKDECODE_PATH'; Value = $oldDecoderPath },
        @{ Name = 'SIGROK_FIRMWARE_DIR'; Value = $oldFirmwareDir },
        @{ Name = 'SIGROK_FIRMWARE_PATH'; Value = $oldFirmwarePath }
    )) {
        if ($null -eq $entry.Value) {
            Remove-Item "Env:$($entry.Name)" -ErrorAction SilentlyContinue
        } else {
            Set-Item "Env:$($entry.Name)" $entry.Value
        }
    }
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

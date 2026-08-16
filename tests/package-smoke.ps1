[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string[]]$ExpectedDrivers,
    [Parameter(Mandatory)][string[]]$ExpectedDecoders
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PackagePath 'sigrok-cli.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Package CLI is missing: $exe"
}
$wchDll = Join-Path $PackagePath 'CH375DLL64.DLL'
if (-not (Test-Path -LiteralPath $wchDll)) {
    throw "Packaged WCH runtime DLL is missing: $wchDll"
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

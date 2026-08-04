[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Script
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolchain = Import-PowerShellDataFile (Join-Path $projectRoot 'config/toolchain.psd1')
$bash = Join-Path $toolchain.MsysRoot 'usr/bin/bash.exe'

if (-not (Test-Path -LiteralPath $bash)) {
    throw "MSYS2 bash was not found at $bash"
}

$shellScript = "export MSYSTEM=$($toolchain.Environment)`nexport PATH=/mingw64/bin:/usr/bin`nexport ACLOCAL_PATH=/mingw64/share/aclocal:/usr/share/aclocal`n$Script"
& $bash -lc $shellScript
if ($LASTEXITCODE -ne 0) {
    throw "MINGW64 command failed with exit code $LASTEXITCODE."
}

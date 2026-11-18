[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Script
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolchain = Import-PowerShellDataFile (Join-Path $projectRoot 'config/toolchain.psd1')
$msysRoot = if ([string]::IsNullOrWhiteSpace($env:PROBES_SIGROK_MSYS_ROOT)) {
    $toolchain.MsysRoot
} else {
    [IO.Path]::GetFullPath($env:PROBES_SIGROK_MSYS_ROOT)
}
$bash = Join-Path $msysRoot 'usr/bin/bash.exe'

if (-not (Test-Path -LiteralPath $bash)) {
    throw "MSYS2 bash was not found at $bash"
}

$shellScript = "export MSYSTEM=$($toolchain.Environment)`nexport PATH=/ucrt64/bin:/usr/bin`nexport ACLOCAL_PATH=/ucrt64/share/aclocal:/usr/share/aclocal`n$Script"
& $bash -lc $shellScript
if ($LASTEXITCODE -ne 0) {
    throw "UCRT64 command failed with exit code $LASTEXITCODE."
}

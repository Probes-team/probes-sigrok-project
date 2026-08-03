[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $projectRoot 'config/release.psd1'
$config = Import-PowerShellDataFile -Path $configPath
$components = @('libsigrok', 'libsigrokdecode', 'sigrok-cli')

foreach ($component in $components) {
    $configurePath = Join-Path $projectRoot "src/$component/configure.ac"
    if (-not (Test-Path -LiteralPath $configurePath)) {
        throw "Missing imported source component: $component"
    }
}

throw "Build pipeline is not implemented yet. Baseline source and release configuration are ready for $($config.Target) ($Configuration)."

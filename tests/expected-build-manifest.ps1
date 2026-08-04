[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildManifestPath
)

$ErrorActionPreference = 'Stop'
$build = Get-Content -LiteralPath $BuildManifestPath -Raw | ConvertFrom-Json
[pscustomobject]@{
    Drivers = @($build.runtimeDrivers | Sort-Object)
    Decoders = @($build.decoders | Sort-Object)
}

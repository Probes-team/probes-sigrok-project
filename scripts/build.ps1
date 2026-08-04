[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/release-config.ps1')

$projectRoot = Get-ProjectRoot -ScriptRoot $PSScriptRoot
$config = Get-ReleaseConfig -ProjectRoot $projectRoot
$sourceRoots = @('libsigrok', 'libsigrokdecode', 'sigrok-cli')

foreach ($sourceRoot in $sourceRoots) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "src/$sourceRoot/configure.ac"))) {
        throw "Missing imported source component: $sourceRoot"
    }
}

$fullPath = [IO.Path]::GetFullPath($projectRoot)
$drive = $fullPath.Substring(0, 1).ToLowerInvariant()
$msysProjectRoot = '/' + $drive + $fullPath.Substring(2).Replace('\', '/')
$buildScript = "$msysProjectRoot/scripts/build-mingw64.sh"
$projectCommit = (git -C $projectRoot rev-parse HEAD).Trim()

& (Join-Path $PSScriptRoot 'invoke-mingw64.ps1') -Script "bash '$buildScript' '$msysProjectRoot' '$Configuration' '$($config.Target)' '$projectCommit'"

$buildManifest = Join-Path $projectRoot '.work/build-manifest.json'
if (-not (Test-Path -LiteralPath $buildManifest)) {
    throw "Build did not produce $buildManifest"
}

Write-Output "Build completed for $($config.Target) ($Configuration)."

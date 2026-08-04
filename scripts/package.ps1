[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/release-config.ps1')
. (Join-Path $PSScriptRoot 'lib/dll-closure.ps1')
. (Join-Path $PSScriptRoot 'lib/python-runtime.ps1')
. (Join-Path $PSScriptRoot 'lib/source-disclosure.ps1')
. (Join-Path $PSScriptRoot 'lib/package-licenses.ps1')

$projectRoot = Get-ProjectRoot -ScriptRoot $PSScriptRoot
$toolchain = Get-ToolchainConfig -ProjectRoot $projectRoot
$stageRoot = Join-Path $projectRoot '.work/stage/probes-sigrok'
$buildManifestPath = Join-Path $projectRoot '.work/build-manifest.json'
$firmwareRoot = Join-Path $projectRoot 'src/sigrok-firmware'
foreach ($required in @($stageRoot, $buildManifestPath, $firmwareRoot)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required package input is missing: $required"
    }
}

$distRoot = Join-Path $projectRoot "dist/probes-sigrok-$Version"
$packageRoot = Join-Path $distRoot 'probes-sigrok'
if (Test-Path -LiteralPath $distRoot) {
    Remove-Item -LiteralPath $distRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

$stageBin = Join-Path $stageRoot 'bin'
Copy-Item -LiteralPath (Join-Path $stageBin 'sigrok-cli.exe') -Destination $packageRoot
Get-ChildItem -LiteralPath $stageBin -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $packageRoot
}

$decoderRoot = Join-Path $stageRoot 'share/libsigrokdecode/decoders'
if (-not (Test-Path -LiteralPath $decoderRoot)) {
    throw "Installed decoder tree is missing: $decoderRoot"
}
Copy-DirectoryContents -Source $decoderRoot -Destination (Join-Path $packageRoot 'decoders')
Copy-DirectoryContents -Source $firmwareRoot -Destination (Join-Path $packageRoot 'firmware')
Copy-EmbeddedPythonRuntime -PackageRoot $packageRoot -Toolchain $toolchain

$mingwBin = Join-Path $toolchain.MsysRoot 'mingw64/bin'
$objdump = Join-Path $mingwBin 'objdump.exe'
$dllClosure = Resolve-DllClosure -PackageRoot $packageRoot -StageBin $stageBin -MsysBin $mingwBin -ObjdumpPath $objdump

New-SourceDisclosure -ProjectRoot $projectRoot -PackageRoot $packageRoot
Copy-PackageLicenses -ProjectRoot $projectRoot -PackageRoot $packageRoot -Toolchain $toolchain
Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses/THIRD_PARTY_NOTICES.md') -Destination $packageRoot

$projectCommit = (git -C $projectRoot rev-parse HEAD).Trim()
@(
    "Package: probes-sigrok",
    "Version: $Version",
    "Target: $($toolchain.Target)",
    "Project commit: $projectCommit"
) | Set-Content -LiteralPath (Join-Path $packageRoot 'VERSION.txt') -Encoding ascii

$files = Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Where-Object { $_.Name -ne 'package-manifest.json' } | ForEach-Object {
    [pscustomobject]@{
        path = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        size = $_.Length
    }
}
$manifest = [ordered]@{
    packageName = 'probes-sigrok'
    version = $Version
    target = $toolchain.Target
    projectCommit = $projectCommit
    build = (Get-Content -LiteralPath $buildManifestPath -Raw | ConvertFrom-Json)
    dllClosure = @($dllClosure)
    peDependencies = @(Get-PeDependencyManifest -PackageRoot $packageRoot -ObjdumpPath $objdump)
    files = @($files | Sort-Object path)
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Encoding utf8
Compress-Archive -LiteralPath $packageRoot -DestinationPath (Join-Path $distRoot 'probes-sigrok.zip') -Force

Write-Output "Package assembled at $packageRoot"

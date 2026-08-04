[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/dll-closure.ps1')

$PackagePath = [IO.Path]::GetFullPath($PackagePath)
$manifestPath = Join-Path $PackagePath 'package-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Package manifest is missing: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($required in @('sigrok-cli.exe', 'decoders', 'firmware', 'python', 'source-disclosure', 'LICENSES', 'THIRD_PARTY_NOTICES.md', 'VERSION.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackagePath $required))) {
        throw "Required package entry is missing: $required"
    }
}

foreach ($file in $manifest.files) {
    $path = Join-Path $PackagePath $file.path
    if (-not (Test-Path -LiteralPath $path)) { throw "Manifest file is missing: $($file.path)" }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $file.sha256) { throw "Manifest hash mismatch: $($file.path)" }
}

$actualDecoders = @(Get-ChildItem -LiteralPath (Join-Path $PackagePath 'decoders') -Directory | ForEach-Object Name | Sort-Object)
$expectedDecoders = @($manifest.build.decoders | Sort-Object)
if (Compare-Object $expectedDecoders $actualDecoders) {
    throw 'The packaged decoder directory set differs from the build manifest.'
}

$forbidden = Get-ChildItem -LiteralPath $PackagePath -Recurse -File | Where-Object {
    $_.FullName -notlike "*\source-disclosure\*" -and $_.Extension -in @('.a', '.h', '.lib', '.o', '.pc')
}
if ($forbidden) { throw "Runtime package contains development artifacts: $($forbidden.FullName -join ', ')" }

$mingwBin = 'C:\msys64\mingw64\bin'
$objdump = Join-Path $mingwBin 'objdump.exe'
foreach ($binary in Get-ChildItem -LiteralPath $PackagePath -Recurse -File | Where-Object { $_.Extension -in @('.exe', '.dll', '.pyd') }) {
    foreach ($dependency in Get-PeImports -FilePath $binary.FullName -ObjdumpPath $objdump) {
        if (-not (Test-WindowsSystemDll -Name $dependency) -and -not (Test-Path -LiteralPath (Join-Path $PackagePath $dependency))) {
            throw "Unpackaged DLL dependency '$dependency' required by '$($binary.Name)'."
        }
    }
}

$buildManifestPath = Join-Path (Split-Path -Parent $PSScriptRoot) '.work/build-manifest.json'
$expected = & (Join-Path $PSScriptRoot '../tests/expected-build-manifest.ps1') -BuildManifestPath $buildManifestPath
& (Join-Path $PSScriptRoot '../tests/package-smoke.ps1') -PackagePath $PackagePath -ExpectedDrivers $expected.Drivers -ExpectedDecoders $expected.Decoders
& (Join-Path $PSScriptRoot '../tests/runtime-paths.ps1') -PackagePath $PackagePath

Write-Output "Package verification passed: $PackagePath"

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [string]$ObjdumpPath
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

$actualFirmware = @(Get-ChildItem -LiteralPath (Join-Path $PackagePath 'firmware') -Recurse -File |
    ForEach-Object { $_.FullName.Substring((Join-Path $PackagePath 'firmware').Length + 1).Replace('\', '/') } |
    Sort-Object)
$expectedFirmware = @($manifest.build.firmware | Sort-Object)
if (Compare-Object $expectedFirmware $actualFirmware) {
    throw 'The packaged firmware file set differs from the build manifest.'
}

foreach ($suite in @($manifest.build.testSuites)) {
    if ($suite.total -lt 1 -or $suite.fail -ne 0 -or $suite.error -ne 0) {
        throw "Build test suite '$($suite.component)' is not a passing, non-empty test run."
    }
}

$forbidden = Get-ChildItem -LiteralPath $PackagePath -Recurse -File | Where-Object {
    $_.FullName -notlike "*\source-disclosure\*" -and
    $_.FullName -notlike "*\LICENSES\*" -and
    $_.Extension -in @('.a', '.h', '.lib', '.o', '.pc')
}
if ($forbidden) { throw "Runtime package contains development artifacts: $($forbidden.FullName -join ', ')" }

$objdump = if ($ObjdumpPath) { $ObjdumpPath } else { Join-Path 'C:\msys64\mingw64\bin' 'objdump.exe' }
if (Test-Path -LiteralPath $objdump) {
    foreach ($binary in Get-ChildItem -LiteralPath $PackagePath -Recurse -File | Where-Object { $_.Extension -in @('.exe', '.dll', '.pyd') }) {
        foreach ($dependency in Get-PeImports -FilePath $binary.FullName -ObjdumpPath $objdump) {
            if (-not (Test-WindowsSystemDll -Name $dependency) -and -not (Test-Path -LiteralPath (Join-Path $PackagePath $dependency))) {
                throw "Unpackaged DLL dependency '$dependency' required by '$($binary.Name)'."
            }
        }
    }
} elseif (-not $manifest.peDependencies) {
    throw 'Package does not contain a sealed PE dependency manifest and objdump is unavailable.'
} else {
    foreach ($binary in @($manifest.peDependencies)) {
        foreach ($dependency in @($binary.imports)) {
            if (-not (Test-WindowsSystemDll -Name $dependency) -and -not (Test-Path -LiteralPath (Join-Path $PackagePath $dependency))) {
                throw "Sealed dependency '$dependency' required by '$($binary.path)' is missing."
            }
        }
    }
}

$buildManifestPath = Join-Path (Split-Path -Parent $PSScriptRoot) '.work/build-manifest.json'
$expected = & (Join-Path $PSScriptRoot '../tests/expected-build-manifest.ps1') -BuildManifestPath $buildManifestPath
& (Join-Path $PSScriptRoot '../tests/package-smoke.ps1') -PackagePath $PackagePath -ExpectedDrivers $expected.Drivers -ExpectedDecoders $expected.Decoders
& (Join-Path $PSScriptRoot '../tests/runtime-paths.ps1') -PackagePath $PackagePath
& (Join-Path $PSScriptRoot '../tests/run-custom-fixtures.ps1') -PackagePath $PackagePath -ExpectedDrivers $expected.Drivers -ExpectedDecoders $expected.Decoders

$expectedPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($file in $manifest.files) { [void]$expectedPaths.Add($file.path) }
$unexpected = Get-ChildItem -LiteralPath $PackagePath -Recurse -File |
    Where-Object { $_.Name -ne 'package-manifest.json' } |
    ForEach-Object { $_.FullName.Substring($PackagePath.Length + 1).Replace('\', '/') } |
    Where-Object { -not $expectedPaths.Contains($_) }
if ($unexpected) {
    throw "Package verification created or found unmanifested files: $($unexpected -join ', ')"
}

Write-Output "Package verification passed: $PackagePath"

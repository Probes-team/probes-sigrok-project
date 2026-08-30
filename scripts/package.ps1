[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    # Keep the default stable for extension synchronization: release/current.
    # An explicit value is supported only when a separately named archive is required.
    [string]$Version = 'current'
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
$wchRoot = Join-Path $projectRoot 'third_party/wch-ch375'
$wchDllSource = Join-Path $wchRoot 'CH375DLL64.DLL'
$wchProvenance = Join-Path $wchRoot 'PROVENANCE.md'
$wchDriverSource = Join-Path $wchRoot 'driver-source/x64'
$wchDriverManifest = Join-Path $wchRoot 'driver-manifest.json'
$wchInstallerHelper = Join-Path $projectRoot '.work/wch-driver-installer/wch-driver-installer.exe'
foreach ($required in @($stageRoot, $buildManifestPath, $firmwareRoot, $wchDllSource, $wchProvenance, $wchDriverSource, $wchDriverManifest, $wchInstallerHelper)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required package input is missing: $required"
    }
}
$wchHash = (Get-FileHash -LiteralPath $wchDllSource -Algorithm SHA256).Hash.ToUpperInvariant()
$provenanceText = Get-Content -LiteralPath $wchProvenance -Raw
$provenanceHash = [regex]::Match(
    $provenanceText,
    '(?im)^- SHA-256:\s*`?([A-F0-9]{64})`?\s*$'
).Groups[1].Value.ToUpperInvariant()
if ($provenanceHash -ne $wchHash) {
    throw "CH375DLL64.DLL does not match third_party/wch-ch375/PROVENANCE.md: $wchHash"
}
$driverManifest = Get-Content -LiteralPath $wchDriverManifest -Raw | ConvertFrom-Json
if ($driverManifest.id -ne 'wch-ch375-x64' -or $driverManifest.architecture -ne 'amd64' -or
    $driverManifest.hardwareIds -notcontains 'USB\VID_1A86&PID_5537' -or
    $driverManifest.acceptableServices -notcontains 'CH375_A64') {
    throw 'WCH driver manifest does not describe the expected x64 CH375 device.'
}
foreach ($driverFile in @($driverManifest.files)) {
    if ([IO.Path]::IsPathRooted($driverFile.path) -or $driverFile.path -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "WCH driver manifest contains an unsafe path: $($driverFile.path)"
    }
    $source = Join-Path $wchDriverSource ($driverFile.path.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "WCH driver payload is missing: $source"
    }
    $item = Get-Item -LiteralPath $source
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($item.Length -ne [int64]$driverFile.size -or $hash -ne $driverFile.sha256.ToLowerInvariant()) {
        throw "WCH driver payload hash/size mismatch: $($driverFile.path)"
    }
    if ($driverFile.peArchitecture -notin @('data', 'x86', 'amd64')) {
        throw "WCH driver payload has an unsupported PE architecture: $($driverFile.path)"
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
Copy-Item -LiteralPath $wchDllSource -Destination (Join-Path $packageRoot 'CH375DLL64.DLL')
Copy-Item -LiteralPath $wchInstallerHelper -Destination (Join-Path $packageRoot 'wch-driver-installer.exe')
$driverPackageRoot = Join-Path $packageRoot 'driver-install/wch-ch375'
$driverPackageX64 = Join-Path $driverPackageRoot 'x64'
New-Item -ItemType Directory -Force -Path $driverPackageX64 | Out-Null
foreach ($driverFile in @($driverManifest.files)) {
    $source = Join-Path $wchDriverSource ($driverFile.path.Replace('/', '\'))
    $destination = Join-Path $driverPackageX64 ($driverFile.path.Replace('/', '\'))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Copy-Item -LiteralPath $wchDriverManifest -Destination (Join-Path $driverPackageRoot 'driver-manifest.json') -Force

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
    drivers = @($driverManifest)
    dllClosure = @($dllClosure)
    peDependencies = @(Get-PeDependencyManifest -PackageRoot $packageRoot -ObjdumpPath $objdump)
    files = @($files | Sort-Object path)
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Encoding utf8
$archivePath = Join-Path $distRoot 'probes-sigrok.zip'
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -Force

if ($Version -eq 'current') {
    $releaseRoot = Join-Path $projectRoot 'release/current'
    $releaseArchivePath = Join-Path $releaseRoot 'probes-sigrok.zip'
    $releaseHashPath = Join-Path $releaseRoot 'probes-sigrok.zip.sha256'
    New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
    Copy-Item -LiteralPath $archivePath -Destination $releaseArchivePath -Force
    $releaseHash = (Get-FileHash -LiteralPath $releaseArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath $releaseHashPath -Value $releaseHash -Encoding ascii
    Write-Output "Published current artifact at $releaseArchivePath"
}

Write-Output "Package assembled at $packageRoot"

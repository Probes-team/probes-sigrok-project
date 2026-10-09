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
$driverRoot = Join-Path $PackagePath 'driver-install/wch-ch375'
$driverHelper = Join-Path $PackagePath 'wch-driver-installer.exe'
if (-not (Test-Path -LiteralPath $driverHelper -PathType Leaf)) {
    throw 'Packaged WCH driver installer helper is missing.'
}
$driverManifestPath = Join-Path $driverRoot 'driver-manifest.json'
if (-not (Test-Path -LiteralPath $driverManifestPath)) {
    throw 'Packaged WCH driver manifest is missing.'
}
$driverManifest = Get-Content -LiteralPath $driverManifestPath -Raw | ConvertFrom-Json
if ($driverManifest.id -ne 'wch-ch375-x64' -or $driverManifest.architecture -ne 'amd64' -or
    $driverManifest.hardwareIds -notcontains 'USB\VID_1A86&PID_5537' -or
    $driverManifest.acceptableServices -notcontains 'CH375_A64') {
    throw 'Packaged WCH driver manifest is invalid.'
}
$driverPayloadPaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($driverFile in @($driverManifest.files)) {
    $relative = ('driver-install/wch-ch375/x64/' + $driverFile.path).Replace('/', '\')
    $path = Join-Path $PackagePath $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Packaged WCH driver file is missing: $relative" }
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($item.Length -ne [int64]$driverFile.size -or $hash -ne $driverFile.sha256.ToLowerInvariant()) {
        throw "Packaged WCH driver file hash/size mismatch: $relative"
    }
    [void]$driverPayloadPaths.Add($relative.Replace('\', '/'))
    $manifestEntry = @($manifest.files | Where-Object { $_.path -eq $relative.Replace('\', '/') })
    if ($manifestEntry.Count -ne 1) { throw "WCH driver file is not covered by package manifest: $relative" }
}
$catalog = Join-Path $PackagePath 'driver-install/wch-ch375/x64/CH375WDM.CAT'
if (Get-Command Get-AuthenticodeSignature -ErrorAction SilentlyContinue) {
    $signature = Get-AuthenticodeSignature -LiteralPath $catalog
    if ($signature.Status -ne 'Valid') { throw "WCH catalog signature is not valid: $($signature.Status)" }
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
	foreach ($driverFile in @($driverManifest.files | Where-Object { $_.peArchitecture -ne 'data' })) {
		$driverPeFile = Join-Path $PackagePath ('driver-install/wch-ch375/x64/' + $driverFile.path.Replace('/', '\\'))
		$format = @(& $objdump -f $driverPeFile 2>&1) -join "`n"
		$expectedFormat = if ($driverFile.peArchitecture -eq 'amd64') { 'pei-x86-64' } elseif ($driverFile.peArchitecture -eq 'x86') { 'pei-i386' } else { throw "Unsupported WCH PE architecture: $($driverFile.peArchitecture)" }
		if ($LASTEXITCODE -ne 0 -or $format -notmatch "file format $expectedFormat") {
			throw "WCH driver payload PE architecture mismatch: $($driverFile.path)"
		}
	}
	$helperFormat = @(& $objdump -f $driverHelper 2>&1) -join "`n"
	if ($LASTEXITCODE -ne 0 -or $helperFormat -notmatch 'file format pei-x86-64') {
		throw 'WCH driver installer helper is not an x64 PE file.'
	}
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

$sigrokCli = Join-Path $PackagePath 'sigrok-cli.exe'

function Get-SigrokStreamFrames {
    param([Parameter(Mandatory)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $frames = New-Object System.Collections.Generic.List[object]
    $i = 0
    while ($i + 16 -le $bytes.Length) {
        if (-not ($bytes[$i] -eq 0x50 -and $bytes[$i + 1] -eq 0x52 -and
                $bytes[$i + 2] -eq 0x42 -and $bytes[$i + 3] -eq 0x53)) {
            throw "sigrok-cli --stream magic mismatch at offset $i."
        }
        $type = [int]$bytes[$i + 5]
        $length = [BitConverter]::ToUInt32($bytes, $i + 12)
        $frames.Add([pscustomobject]@{ Type = $type; Length = $length }) | Out-Null
        $i += 16 + [int]$length
    }
    if ($i -ne $bytes.Length) { throw 'sigrok-cli --stream produced a trailing partial frame.' }
    return $frames
}

# Capabilities JSON must advertise the streaming + schema versions the panel gates on.
$capabilitiesRaw = (& $sigrokCli --capabilities --json) | Out-String
$capabilities = $capabilitiesRaw | ConvertFrom-Json
if ([int]$capabilities.streamProtocol -lt 1 -or [int]$capabilities.jsonSchema -lt 1) {
    throw "sigrok-cli capabilities JSON is missing streaming support: $capabilitiesRaw"
}

# Live-ish capture: the demo driver must emit an analyzable binary frame sequence.
$streamOut = [IO.Path]::GetTempFileName()
$streamErr = [IO.Path]::GetTempFileName()
try {
    $proc = Start-Process -FilePath $sigrokCli -NoNewWindow -Wait -PassThru `
        -ArgumentList @('-d', 'demo', '--samples', '1024', '--stream') `
        -RedirectStandardOutput $streamOut -RedirectStandardError $streamErr
    if ($proc.ExitCode -ne 0) { throw "sigrok-cli --stream exited with $($proc.ExitCode)." }
    $frames = Get-SigrokStreamFrames -Path $streamOut
    if (-not ($frames | Where-Object { $_.Type -eq 2 })) {
        throw 'sigrok-cli -d demo --stream produced no LOGIC frame.'
    }
    if (-not ($frames | Where-Object { $_.Type -eq 8 })) {
        throw 'sigrok-cli -d demo --stream produced no END frame.'
    }
} finally {
    Remove-Item -LiteralPath $streamOut, $streamErr -Force -ErrorAction SilentlyContinue
}

# Session-file replay must reuse the same stream pipeline.
$artifactDir = Join-Path ([IO.Path]::GetTempPath()) ("probes-sigrok-artifact-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$replayOut = [IO.Path]::GetTempFileName()
$replayErr = [IO.Path]::GetTempFileName()
try {
    $capture = Start-Process -FilePath $sigrokCli -NoNewWindow -Wait -PassThru `
        -ArgumentList @('-d', 'demo', '--samples', '2048', '--output-dir', $artifactDir) `
        -RedirectStandardOutput $replayOut -RedirectStandardError $replayErr
    if ($capture.ExitCode -ne 0) { throw "sigrok-cli artifact capture exited with $($capture.ExitCode)." }
    $sessionFile = Join-Path $artifactDir 'capture.sr'
    if (-not (Test-Path -LiteralPath $sessionFile)) { throw 'Artifact capture did not produce capture.sr.' }

    $replay = Start-Process -FilePath $sigrokCli -NoNewWindow -Wait -PassThru `
        -ArgumentList @('-i', $sessionFile, '--stream') `
        -RedirectStandardOutput $replayOut -RedirectStandardError $replayErr
    if ($replay.ExitCode -ne 0) { throw "sigrok-cli --input-file --stream exited with $($replay.ExitCode)." }
    $replayFrames = Get-SigrokStreamFrames -Path $replayOut
    if (-not ($replayFrames | Where-Object { $_.Type -eq 2 })) {
        throw 'sigrok-cli --input-file --stream produced no LOGIC frame.'
    }
} finally {
    Remove-Item -LiteralPath $replayOut, $replayErr -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $artifactDir -Recurse -Force -ErrorAction SilentlyContinue
}

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

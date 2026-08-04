function Copy-PackageLicenses {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$PackageRoot,
        [Parameter(Mandatory)][hashtable]$Toolchain
    )

    $licenseRoot = Join-Path $PackageRoot 'LICENSES'
    New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null

    $sourceRoot = Join-Path $ProjectRoot 'src'
    Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object {
        $_.Name -match '^(COPYING|LICENSE(?:\.|$)|NOTICE(?:\.|$))'
    } | ForEach-Object {
        $relative = $_.FullName.Substring($sourceRoot.Length + 1).Replace('\', '/')
        $destination = Join-Path $licenseRoot ('source/' + $relative)
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
    }

    $msysLicenses = Join-Path $Toolchain.MsysRoot 'mingw64/share/licenses'
    if (Test-Path -LiteralPath $msysLicenses) {
        Get-ChildItem -LiteralPath $msysLicenses -Recurse -File | ForEach-Object {
            $relative = $_.FullName.Substring($msysLicenses.Length + 1)
            $destination = Join-Path $licenseRoot ('mingw64/' + $relative)
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        }
    }
}

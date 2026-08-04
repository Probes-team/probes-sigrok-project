function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        if ($_.Name -eq '.git') {
            throw "Nested Git metadata is not allowed in source disclosure: $Source"
        }
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function New-SourceDisclosure {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$PackageRoot
    )

    $disclosureRoot = Join-Path $PackageRoot 'source-disclosure'
    New-Item -ItemType Directory -Force -Path $disclosureRoot | Out-Null
    foreach ($component in @('libsigrok', 'libsigrokdecode', 'sigrok-cli', 'sigrok-firmware')) {
        $source = Join-Path $ProjectRoot "src/$component"
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Missing source disclosure component: $component"
        }
        Copy-DirectoryContents -Source $source -Destination (Join-Path $disclosureRoot $component)
    }
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'upstream-manifest.json') -Destination $disclosureRoot
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'licenses') -Destination $disclosureRoot -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'licenses/THIRD_PARTY_NOTICES.md') -Destination $disclosureRoot
}

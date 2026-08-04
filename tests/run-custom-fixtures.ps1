[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][string[]]$ExpectedDrivers,
    [Parameter(Mandatory)][string[]]$ExpectedDecoders
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$registryPath = Join-Path $projectRoot 'config/custom-components.json'
$registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
if ($registry.schemaVersion -ne 1) {
    throw "Unsupported custom component registry schema: $($registry.schemaVersion)"
}

$exe = Join-Path $PackagePath 'sigrok-cli.exe'
$fixtureRoot = Join-Path $PSScriptRoot 'fixtures'
$oldPath = $env:PATH
$oldPythonHome = $env:PYTHONHOME
$oldDecoderDir = $env:SIGROKDECODE_DIR
$oldDecoderPath = $env:SIGROKDECODE_PATH
try {
    $env:PATH = "$PackagePath;$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:PYTHONHOME, Env:SIGROKDECODE_DIR, Env:SIGROKDECODE_PATH -ErrorAction SilentlyContinue

    foreach ($driver in @($registry.drivers | Where-Object { $null -ne $_ })) {
        if ($driver.id -notin $ExpectedDrivers) {
            throw "Custom driver '$($driver.id)' is absent from the built driver set."
        }
        if (-not $driver.arguments -or -not $driver.expect) {
            throw "Custom driver '$($driver.id)' must declare arguments and expected output patterns."
        }
        $output = @(& $exe @($driver.arguments) 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "Driver fixture '$($driver.id)' failed: $($output -join [Environment]::NewLine)"
        }
        $text = $output -join "`n"
        foreach ($pattern in @($driver.expect)) {
            if ($text -notmatch $pattern) {
                throw "Driver fixture '$($driver.id)' did not match '$pattern'."
            }
        }
    }

    foreach ($decoder in @($registry.decoders | Where-Object { $null -ne $_ })) {
        if ($decoder.id -notin $ExpectedDecoders) {
            throw "Custom decoder '$($decoder.id)' is absent from the packaged decoder set."
        }
        if (-not $decoder.fixture -or -not $decoder.arguments -or -not $decoder.expect) {
            throw "Custom decoder '$($decoder.id)' must declare a fixture, arguments, and expected output patterns."
        }
        $fixture = Join-Path $fixtureRoot $decoder.fixture
        if (-not (Test-Path -LiteralPath $fixture)) {
            throw "Decoder fixture '$($decoder.fixture)' for '$($decoder.id)' is missing."
        }
        $arguments = @($decoder.arguments | ForEach-Object { ([string]$_).Replace('{fixture}', $fixture) })
        $output = @(& $exe @arguments 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "Decoder fixture '$($decoder.id)' failed: $($output -join [Environment]::NewLine)"
        }
        $text = $output -join "`n"
        foreach ($pattern in @($decoder.expect)) {
            if ($text -notmatch $pattern) {
                throw "Decoder fixture '$($decoder.id)' did not match '$pattern'."
            }
        }
    }
} finally {
    $env:PATH = $oldPath
    foreach ($entry in @(
        @{ Name = 'PYTHONHOME'; Value = $oldPythonHome },
        @{ Name = 'SIGROKDECODE_DIR'; Value = $oldDecoderDir },
        @{ Name = 'SIGROKDECODE_PATH'; Value = $oldDecoderPath }
    )) {
        if ($null -eq $entry.Value) { Remove-Item "Env:$($entry.Name)" -ErrorAction SilentlyContinue }
        else { Set-Item "Env:$($entry.Name)" $entry.Value }
    }
}

[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackagePath)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PackagePath 'sigrok-cli.exe'
$oldPath = $env:PATH
$oldPythonHome = $env:PYTHONHOME
$oldDecoderDir = $env:SIGROKDECODE_DIR
$oldDecoderPath = $env:SIGROKDECODE_PATH
try {
    $env:PATH = "$PackagePath;$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:PYTHONHOME, Env:SIGROKDECODE_DIR, Env:SIGROKDECODE_PATH -ErrorAction SilentlyContinue
    $output = @(& $exe --list-supported 2>&1)
    if ($LASTEXITCODE -ne 0 -or ($output -join "`n") -notmatch 'Supported protocol decoders:') {
        throw 'The package did not find its decoder directory without environment variables.'
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

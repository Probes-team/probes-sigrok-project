[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackagePath)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PackagePath 'sigrok-cli.exe'
$wchDll = Join-Path $PackagePath 'CH375DLL64.DLL'
if (-not (Test-Path -LiteralPath $wchDll)) {
    throw "Packaged WCH runtime DLL is missing: $wchDll"
}
$oldPath = $env:PATH
$oldPythonHome = $env:PYTHONHOME
$oldDecoderDir = $env:SIGROKDECODE_DIR
$oldDecoderPath = $env:SIGROKDECODE_PATH
$localDecoderRoot = Join-Path $env:LOCALAPPDATA 'Probes\sigrok\decoders'
$fixtureDecoderId = 'probes_external_path_fixture'
$fixtureDecoderRoot = Join-Path $localDecoderRoot ($fixtureDecoderId + '_' + [guid]::NewGuid().ToString('N'))
try {
    $env:PATH = "$PackagePath;$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:PYTHONHOME, Env:SIGROKDECODE_DIR, Env:SIGROKDECODE_PATH -ErrorAction SilentlyContinue
    $output = @(& $exe --list-supported 2>&1)
    if ($LASTEXITCODE -ne 0 -or ($output -join "`n") -notmatch 'Supported protocol decoders:') {
        throw 'The package did not find its decoder directory without environment variables.'
    }

    New-Item -ItemType Directory -Force -Path $fixtureDecoderRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureDecoderRoot '__init__.py') -Value 'from .pd import Decoder' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $fixtureDecoderRoot 'pd.py') -Value @'
import sigrokdecode as srd

class Decoder(srd.Decoder):
    api_version = 3
    id = "probes_external_path_fixture"
    name = "External path fixture"
    longname = "External path fixture"
    desc = "Runtime search-path test decoder."
    license = "gplv2+"
    inputs = ["logic"]
    outputs = ["probes_external_path_fixture"]
    tags = ["Test"]
    annotations = (("sample", "Sample"),)

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def reset(self):
        pass

    def decode(self):
        return
'@ -Encoding utf8
    $output = @(& $exe --list-supported 2>&1)
    if ($LASTEXITCODE -ne 0 -or ($output -join "`n") -notmatch [regex]::Escape($fixtureDecoderId)) {
        throw "The package did not find its persistent external decoder root: $localDecoderRoot"
    }
} finally {
    Remove-Item -LiteralPath $fixtureDecoderRoot -Recurse -Force -ErrorAction SilentlyContinue
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

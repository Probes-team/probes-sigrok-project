[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$requiredCommands = @(
    'git',
    'tar',
    'bash',
    'autoreconf',
    'make',
    'pkg-config',
    'python'
)

$missing = @(
    $requiredCommands | Where-Object {
        $null -eq (Get-Command $_ -ErrorAction SilentlyContinue)
    }
)

if ($missing.Count -gt 0) {
    throw "Missing required build commands: $($missing -join ', ')"
}

$configPath = Join-Path $PSScriptRoot '../config/release.psd1'
$config = Import-PowerShellDataFile -Path $configPath
$compiler = "$($config.Target)-gcc"

if ($null -eq (Get-Command $compiler -ErrorAction SilentlyContinue)) {
    throw "Missing target compiler: $compiler"
}

Write-Output "Build prerequisites found for target $($config.Target)."

function Get-ProjectRoot {
    param([Parameter(Mandatory)][string]$ScriptRoot)

    return Split-Path -Parent $ScriptRoot
}

function Get-ReleaseConfig {
    param([Parameter(Mandatory)][string]$ProjectRoot)

    return Import-PowerShellDataFile (Join-Path $ProjectRoot 'config/release.psd1')
}

function Get-ToolchainConfig {
    param([Parameter(Mandatory)][string]$ProjectRoot)

    return Import-PowerShellDataFile (Join-Path $ProjectRoot 'config/toolchain.psd1')
}

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

    $config = Import-PowerShellDataFile (Join-Path $ProjectRoot 'config/toolchain.psd1')
    if (-not [string]::IsNullOrWhiteSpace($env:PROBES_SIGROK_MSYS_ROOT)) {
        $config.MsysRoot = [IO.Path]::GetFullPath($env:PROBES_SIGROK_MSYS_ROOT)
    }
    return $config
}

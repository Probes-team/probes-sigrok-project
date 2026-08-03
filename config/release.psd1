@{
    PackageName = 'probes-sigrok'
    Version = '0.1.0-dev'
    Target = 'x86_64-w64-mingw32'
    IncludeDecoders = $true
    IncludeFirmware = $true
    PythonMode = 'embedded'
    EnabledDrivers = @()
    DecoderAllowlist = @()
    FirmwareAllowlist = @()
}

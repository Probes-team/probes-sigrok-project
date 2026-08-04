[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolchain = Import-PowerShellDataFile (Join-Path $projectRoot 'config/toolchain.psd1')
$invoke = Join-Path $PSScriptRoot 'invoke-mingw64.ps1'

$requiredPackages = @(
    'autoconf',
    'automake',
    'libtool',
    'mingw-w64-x86_64-gcc',
    'mingw-w64-x86_64-pkgconf',
    'mingw-w64-x86_64-glib2',
    'mingw-w64-x86_64-zlib',
    'mingw-w64-x86_64-libzip',
    'mingw-w64-x86_64-python',
    'mingw-w64-x86_64-libusb',
    'mingw-w64-x86_64-libserialport',
    'mingw-w64-x86_64-libftdi',
    'mingw-w64-x86_64-hidapi',
    'mingw-w64-x86_64-nettle'
)

$checks = @(
    'command -v autoreconf',
    'command -v automake',
    'command -v libtoolize',
    'command -v gcc',
    'command -v make',
    'command -v pkg-config',
    'command -v python3.14',
    'pkg-config --exists glib-2.0 gio-2.0 zlib libzip libusb-1.0 libserialport libftdi1 hidapi python-3.14-embed',
    'test "$(pkg-config --modversion python-3.14-embed)" = "3.14"'
)

foreach ($package in $requiredPackages) {
    & $invoke -Script "pacman -Q $package"
}
foreach ($check in $checks) {
    & $invoke -Script $check
}

Write-Output "MINGW64 prerequisites found for $($toolchain.Target) with Python 3.14."
Write-Output 'Platform-unavailable upstream dependencies: bluez, librevisa, libgpib, libieee1284.'

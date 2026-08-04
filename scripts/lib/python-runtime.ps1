function Copy-EmbeddedPythonRuntime {
    param(
        [Parameter(Mandatory)][string]$PackageRoot,
        [Parameter(Mandatory)][hashtable]$Toolchain
    )

    $mingwRoot = Join-Path $Toolchain.MsysRoot 'mingw64'
    $pythonDll = Join-Path $mingwRoot 'bin/libpython3.14.dll'
    $stdlib = Join-Path $mingwRoot 'lib/python3.14'
    if (-not (Test-Path -LiteralPath $pythonDll) -or -not (Test-Path -LiteralPath $stdlib)) {
        throw 'The fixed MINGW64 Python 3.14 runtime is incomplete.'
    }

    Copy-Item -LiteralPath $pythonDll -Destination (Join-Path $PackageRoot 'libpython3.14.dll')
    $pythonDestination = Join-Path $PackageRoot 'python/lib/python3.14'
    New-Item -ItemType Directory -Force -Path $pythonDestination | Out-Null
    Get-ChildItem -LiteralPath $stdlib -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $pythonDestination -Recurse -Force
    }
    $pythonObject = Join-Path $pythonDestination 'config-3.14/python.o'
    if (Test-Path -LiteralPath $pythonObject) {
        Remove-Item -LiteralPath $pythonObject -Force
    }
}

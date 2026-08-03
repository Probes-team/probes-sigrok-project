[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackagePath
)

$ErrorActionPreference = 'Stop'

throw "Package verification is not implemented yet. It must validate the assembled package at $PackagePath without using build-machine runtime dependencies."

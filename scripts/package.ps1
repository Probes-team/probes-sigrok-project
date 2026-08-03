[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

throw "Package pipeline is not implemented yet. It must package only .work/stage and create source-disclosure from the tagged src tree for version $Version."

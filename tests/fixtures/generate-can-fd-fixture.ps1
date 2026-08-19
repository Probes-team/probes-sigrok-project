[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SigrokCli
)

$ErrorActionPreference = 'Stop'
$fixtureRoot = $PSScriptRoot
$vcdPath = Join-Path $fixtureRoot 'can-fd-basic.vcd'
$srPath = Join-Path $fixtureRoot 'can-fd-basic.sr'

function Get-Bits([int]$Value, [int]$Width) {
    $bits = [System.Collections.Generic.List[int]]::new()
    for ($index = $Width - 1; $index -ge 0; $index--) {
        $bits.Add(($Value -shr $index) -band 1)
    }
    return $bits
}

function Add-StuffBits([int[]]$Bits) {
    $result = [System.Collections.Generic.List[int]]::new()
    $previous = -1
    $runLength = 0
    foreach ($bit in $Bits) {
        $result.Add($bit)
        if ($bit -eq $previous) { $runLength++ } else { $previous = $bit; $runLength = 1 }
        if ($runLength -eq 5) {
            $previous = 1 - $bit
            $result.Add($previous)
            $runLength = 1
        }
    }
    return ,$result
}

# Standard CAN-FD data frame: ID 0x123, BRS enabled, DLC 1, payload 0xA5.
$logical = [System.Collections.Generic.List[int]]::new()
$logical.Add(0)
foreach ($bit in (Get-Bits 0x123 11)) { $logical.Add($bit) }
$logical.AddRange([int[]]@(0, 0, 1, 0, 1, 0)) # RRS, IDE, FDF, RB0, BRS, ESI
foreach ($bit in (Get-Bits 1 4)) { $logical.Add($bit) }
foreach ($bit in (Get-Bits 0xA5 8)) { $logical.Add($bit) }
$wire = Add-StuffBits $logical

# The decoder's CRC check is not implemented yet. The remaining bits only
# provide enough well-formed timing for the fixture to flush field annotations.
$wire.AddRange([int[]]@(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1))

$vcd = [System.Collections.Generic.List[string]]::new()
foreach ($line in @(
    '$date', '  2026-08-19', '$end', '$version',
    '  Synthetic standard CAN-FD frame: ID 0x123, BRS, DLC 1, data 0xA5.',
    '$end', '$timescale 100ns $end', '$scope module logic $end',
    '$var wire 1 ! can_rx $end', '$upscope $end', '$enddefinitions $end',
    '$dumpvars', '1!', '$end', '#1', '1!'
)) { $vcd.Add($line) }

$time = 10
$level = 1
foreach ($bit in $wire) {
    if ($bit -ne $level) { $vcd.Add("#$time"); $vcd.Add("$bit!"); $level = $bit }
    $time += 10
}
$time += 10
if ($level -ne 1) { $vcd.Add("#$time"); $vcd.Add('1!') }

Set-Content -LiteralPath $vcdPath -Value $vcd -Encoding ascii
& $SigrokCli -I vcd -i $vcdPath -O srzip -o $srPath
if ($LASTEXITCODE -ne 0) { throw 'Failed to convert CAN-FD VCD fixture to srzip.' }

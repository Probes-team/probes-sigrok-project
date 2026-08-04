function Get-PeImports {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string]$ObjdumpPath
    )

    return @(& $ObjdumpPath -p $FilePath |
        ForEach-Object {
            if ($_ -match '^\s*DLL Name:\s+(.+)$') {
                $Matches[1].Trim()
            }
        } | Sort-Object -Unique)
}

function Test-WindowsSystemDll {
    param([Parameter(Mandatory)][string]$Name)

    return $Name -match '^(api-ms-win-|ext-ms-win-)' -or $Name -in @(
        'advapi32.dll', 'bcrypt.dll', 'combase.dll', 'comctl32.dll', 'comdlg32.dll',
        'crypt32.dll', 'gdi32.dll', 'imm32.dll', 'iphlpapi.dll', 'kernel32.dll',
        'kernelbase.dll', 'msvcrt.dll', 'netapi32.dll', 'ntdll.dll', 'ole32.dll',
        'oleaut32.dll', 'propsys.dll', 'rpcrt4.dll', 'secur32.dll', 'setupapi.dll',
        'shell32.dll', 'ucrtbase.dll', 'user32.dll', 'userenv.dll', 'version.dll',
        'winmm.dll', 'ws2_32.dll'
    )
}

function Resolve-DllClosure {
    param(
        [Parameter(Mandatory)][string]$PackageRoot,
        [Parameter(Mandatory)][string]$StageBin,
        [Parameter(Mandatory)][string]$MsysBin,
        [Parameter(Mandatory)][string]$ObjdumpPath
    )

    $queue = [System.Collections.Generic.Queue[string]]::new()
    $visited = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $copied = [System.Collections.Generic.List[string]]::new()
    Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.exe', '.dll', '.pyd') } |
        ForEach-Object { $queue.Enqueue($_.FullName) }

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if (-not $visited.Add($current)) {
            continue
        }
        foreach ($dependency in Get-PeImports -FilePath $current -ObjdumpPath $ObjdumpPath) {
            if (Test-WindowsSystemDll -Name $dependency) {
                continue
            }
            $packaged = Join-Path $PackageRoot $dependency
            if (-not (Test-Path -LiteralPath $packaged)) {
                $stageCandidate = Join-Path $StageBin $dependency
                $msysCandidate = Join-Path $MsysBin $dependency
                if (Test-Path -LiteralPath $stageCandidate) {
                    Copy-Item -LiteralPath $stageCandidate -Destination $packaged
                    $copied.Add($dependency)
                } elseif (Test-Path -LiteralPath $msysCandidate) {
                    Copy-Item -LiteralPath $msysCandidate -Destination $packaged
                    $copied.Add($dependency)
                } else {
                    throw "Unresolved non-system DLL dependency '$dependency' required by '$current'."
                }
            }
            $queue.Enqueue($packaged)
        }
    }

    return $copied | Sort-Object -Unique
}

function Get-PeDependencyManifest {
    param(
        [Parameter(Mandatory)][string]$PackageRoot,
        [Parameter(Mandatory)][string]$ObjdumpPath
    )

    return @(Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.exe', '.dll', '.pyd') } |
        ForEach-Object {
            [ordered]@{
                path = $_.FullName.Substring($PackageRoot.Length + 1).Replace('\', '/')
                imports = @(Get-PeImports -FilePath $_.FullName -ObjdumpPath $ObjdumpPath)
            }
        } | Sort-Object path)
}

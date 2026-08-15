param(
    [string] $PluginDll = 'release/Release/program-standby-source/bin/64bit/program-standby-source.dll',
    [string] $ObsBin = 'C:/Program Files/obs-studio/bin/64bit',
    [string] $DumpbinPath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))

if (-not [IO.Path]::IsPathRooted($PluginDll)) { $PluginDll = Join-Path $repoRoot $PluginDll }
if (-not (Test-Path -LiteralPath $PluginDll -PathType Leaf)) { throw "Plugin DLL not found: $PluginDll" }
if (-not (Test-Path -LiteralPath $ObsBin -PathType Container)) { throw "OBS binary directory not found: $ObsBin" }

if (-not $DumpbinPath) {
    $command = Get-Command 'dumpbin.exe' -ErrorAction SilentlyContinue
    if ($command) {
        $DumpbinPath = $command.Source
    } else {
        $vsRoot = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/2022'
        $DumpbinPath = Get-ChildItem $vsRoot -Recurse -Filter 'dumpbin.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*Hostx64*x64*' } |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $DumpbinPath) { throw 'dumpbin.exe was not found.' }

$dump = & $DumpbinPath /DEPENDENTS $PluginDll
if ($LASTEXITCODE -ne 0) { throw "dumpbin.exe failed with exit code $LASTEXITCODE." }

$dependencies = $dump |
    Where-Object { $_ -match '^\s+([A-Za-z0-9._-]+\.dll)\s*$' } |
    ForEach-Object { $Matches[1] } |
    Sort-Object -Unique

$searchDirs = @($ObsBin, (Join-Path $env:SystemRoot 'System32'), (Join-Path $env:SystemRoot 'SysWOW64'))
$missing = foreach ($dependency in $dependencies) {
    if ($dependency -like 'api-ms-win-*' -or $dependency -like 'ext-ms-*') { continue }
    if (-not ($searchDirs | Where-Object { Test-Path -LiteralPath (Join-Path $_ $dependency) -PathType Leaf })) {
        $dependency
    }
}
if ($missing) {
    throw "Plugin dependencies unavailable to this OBS installation: $($missing -join ', ')"
}

Write-Host 'OBS runtime compatibility validation passed.'

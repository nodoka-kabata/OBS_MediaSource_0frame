param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',
    [string] $BuildDir = 'build_x64',
    [string] $StagingDir = '',
    [string] $OutputDir = 'dist',
    [string] $CmakePath = '',
    [string] $IsccPath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath {
    param([string] $Path)
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $repoRoot $Path
}

function Invoke-Native {
    param([string] $FilePath, [string[]] $Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

$BuildDir = Resolve-RepoPath $BuildDir
if (-not $StagingDir) { $StagingDir = "release/$Configuration" }
$StagingDir = Resolve-RepoPath $StagingDir
$OutputDir = Resolve-RepoPath $OutputDir

$buildspec = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'buildspec.json') | ConvertFrom-Json
$version = $buildspec.version
$pluginName = $buildspec.name

if (-not $CmakePath) {
    $cmakeCache = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cmakeCache -PathType Leaf) {
        $cacheEntry = Select-String -LiteralPath $cmakeCache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$'
        if ($cacheEntry) { $CmakePath = $cacheEntry.Matches[0].Groups[1].Value }
    }
    if (-not $CmakePath) {
        $command = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
        if ($command) { $CmakePath = $command.Source }
    }
}
if (-not $CmakePath -or -not (Test-Path -LiteralPath $CmakePath -PathType Leaf)) {
    throw 'cmake.exe was not found. Configure the project first or pass -CmakePath.'
}

Invoke-Native $CmakePath @('--build', $BuildDir, '--config', $Configuration)
Invoke-Native $CmakePath @('--install', $BuildDir, '--config', $Configuration, '--prefix', $StagingDir)

$pluginRoot = Join-Path $StagingDir $pluginName
$requiredFiles = @(
    (Join-Path $pluginRoot "bin/64bit/$pluginName.dll"),
    (Join-Path $pluginRoot 'data/locale/en-US.ini'),
    (Join-Path $pluginRoot 'data/locale/ja-JP.ini')
)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required installer input is missing: $file"
    }
}

if (-not $IsccPath) {
    $candidates = @(
        (Join-Path $repoRoot '.deps/inno-setup/ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6/ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $IsccPath) {
        $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
        if ($command) { $IsccPath = $command.Source }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'ISCC.exe was not found. Install Inno Setup 6 or pass -IsccPath.'
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$outputName = "$($buildspec.displayName)-$version-Windows-x64-Setup"
$installerScript = Join-Path $PSScriptRoot 'program-standby-source.iss'
Invoke-Native $IsccPath @("/DSourceDir=$StagingDir", "/DAppVersion=$version", "/O$OutputDir", "/F$outputName", $installerScript)

$installerPath = Join-Path $OutputDir "$outputName.exe"
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Installer output was not created: $installerPath"
}
Write-Host "Installer created: $installerPath"

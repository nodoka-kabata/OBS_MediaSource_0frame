$ErrorActionPreference = 'Stop'

$installerDir = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$scriptPath = Join-Path $installerDir 'program-standby-source.iss'
$buildPath = Join-Path $installerDir 'build-installer.ps1'

function Assert-Matches {
    param([string] $Content, [string] $Pattern, [string] $Message)

    if ($Content -notmatch $Pattern) {
        throw $Message
    }
}

if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw 'The Inno Setup script is missing.'
}
if (-not (Test-Path -LiteralPath $buildPath -PathType Leaf)) {
    throw 'The installer build script is missing.'
}

$installer = Get-Content -Raw -LiteralPath $scriptPath
$builder = Get-Content -Raw -LiteralPath $buildPath

Assert-Matches $installer 'PrivilegesRequired=lowest' 'Installer must not require administrator privileges.'
Assert-Matches $installer 'DefaultDirName=\{commonappdata\}\\obs-studio\\plugins\\program-standby-source' 'Installer must target the OBS Windows plugin directory.'
Assert-Matches $installer 'UsePreviousAppDir=no' 'Installer must not reuse the obsolete per-user install path.'
Assert-Matches $installer '\[InstallDelete\][\s\S]*\{userappdata\}\\obs-studio\\plugins\\program-standby-source' 'Installer must remove the obsolete per-user plugin directory.'
Assert-Matches $installer 'program-standby-source\\bin\\64bit\\program-standby-source\.dll' 'Installer must include the x64 plugin DLL.'
Assert-Matches $installer 'program-standby-source\\data\\locale\\\*\.ini' 'Installer must include locale files.'
Assert-Matches $installer 'ArchitecturesAllowed=x64compatible' 'Installer must reject unsupported architectures.'
Assert-Matches $builder 'CMAKE_COMMAND:INTERNAL' 'Build script must discover CMake from the configured build tree.'
Assert-Matches $builder "@\('--preset'" 'Build script must reconfigure dependencies before building.'
Assert-Matches $builder "'--fresh'" 'Build script must discard stale dependency caches.'
Assert-Matches $builder "@\('--build'" 'Build script must build the plugin.'
Assert-Matches $builder "@\('--install'" 'Build script must stage the plugin.'
Assert-Matches $builder 'ISCC\.exe' 'Build script must invoke the Inno Setup compiler.'
Assert-Matches $builder 'validate-obs-compatibility\.ps1' 'Build script must validate the plugin against the installed OBS runtime.'

Write-Host 'Installer validation passed.'

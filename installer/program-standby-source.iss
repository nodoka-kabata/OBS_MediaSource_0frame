#ifndef SourceDir
  #error SourceDir must point to the staged CMake install directory.
#endif
#ifndef AppVersion
  #error AppVersion must be supplied by build-installer.ps1.
#endif

#define AppName "Program Standby Source"

[Setup]
AppId={{9DAEAFD0-4699-4E55-8BF5-D6E14A57496F}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=rirorochannel
DefaultDirName={userappdata}\obs-studio\plugins\program-standby-source
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Files]
Source: "{#SourceDir}\program-standby-source\bin\64bit\program-standby-source.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion restartreplace
Source: "{#SourceDir}\program-standby-source\data\locale\*.ini"; DestDir: "{app}\data\locale"; Flags: ignoreversion

; FlashGram Windows installer (Inno Setup 6).
; Built by flashgram_release.ps1 from the release-staging folder, which
; contains only runtime files: FlashGram.exe and the d3d compiler module.

#define MyAppName "FlashGram"
#define MyAppPublisher "FlashGram"
#define MyAppURL "https://github.com/CriminalFlower/FlashGram"
#define MyAppExeName "FlashGram.exe"
#define MyAppId "8B1F6C2E-4A7D-4E3B-9F25-6D0C3A91E7B4"
#define CurrentYear GetDateTimeString('yyyy','','')

#ifndef MyAppVersion
  #error MyAppVersion must be passed with /DMyAppVersion=x.y.z
#endif
#ifndef MyAppFileVersion
  #define MyAppFileVersion MyAppVersion + ".0"
#endif
#ifndef StagingPath
  #error StagingPath must be passed with /DStagingPath=...
#endif
#ifndef OutputPath
  #error OutputPath must be passed with /DOutputPath=...
#endif

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppCopyright={#MyAppPublisher} {#CurrentYear}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=no
AllowNoIcons=yes
OutputDir={#OutputPath}
OutputBaseFilename=FlashGram-Setup-{#MyAppVersion}
SetupIconFile={#SourcePath}..\Resources\art\icon256.ico
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMADictionarySize=65536
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=force
WizardStyle=modern
VersionInfoVersion={#MyAppFileVersion}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppFileVersion}
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#StagingPath}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StagingPath}\modules\x64\d3d\d3dcompiler_47.dll"; DestDir: "{app}\modules\x64\d3d"; Flags: ignoreversion
Source: "{#StagingPath}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; AppUserModelID: "FlashGram.FlashGramDesktop"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\log.txt"
Type: filesandordirs; Name: "{app}\DebugLogs"
Type: filesandordirs; Name: "{app}\tdumps"
Type: filesandordirs; Name: "{app}\modules"
Type: dirifempty; Name: "{app}"

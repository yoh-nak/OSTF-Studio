[Setup]
AppId={{24B8E27B-6719-48E3-8F10-122734FA07F7}
AppName=OSTF Studio VST3 Plug-ins
AppPublisher=OSTF Studio
AppVersion={#AppVersion}
VersionInfoVersion={#BinaryVersion}
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={commonpf64}\OSTF Studio VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateUninstallRegKey=yes
CloseApplications=no
RestartApplications=no
SetupLogging=yes
OutputDir={#OutputDir}
OutputBaseFilename=OSTF-Studio-VST3-Setup
SetupIconFile=..\..\assets\AppIcon.ico

[Files]
Source: "{#GmBundle}\*"; DestDir: "{commoncf64}\VST3\OSTF Studio\OSTF GM Sound Module.vst3"; Flags: replacesameversion recursesubdirs createallsubdirs
Source: "{#EffectsDir}\*"; DestDir: "{commoncf64}\VST3\OSTF Studio"; Flags: replacesameversion recursesubdirs createallsubdirs

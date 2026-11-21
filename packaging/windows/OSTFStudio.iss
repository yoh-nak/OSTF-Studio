[Setup]
AppId={{6296B3E9-263E-4F3E-B29E-D5DF48519D34}
AppName=OSTF Studio
AppVersion={#AppVersion}
VersionInfoVersion={#BinaryVersion}
AppPublisher=OSTF
DefaultDirName={localappdata}\Programs\OSTF Studio
UsePreviousAppDir=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\OSTF Studio.exe
CloseApplications=yes
RestartApplications=no
OutputDir={#OutputDir}
OutputBaseFilename=OSTF_Studio_Windows_x64_Setup
WizardStyle=modern

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\OSTF Studio"; Filename: "{app}\OSTF Studio.exe"

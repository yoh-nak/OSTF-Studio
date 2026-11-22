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
ChangesAssociations=yes
OutputDir={#OutputDir}
OutputBaseFilename=OSTF_Studio_Windows_x64_Setup
SetupIconFile=..\..\assets\AppIcon.ico
WizardStyle=modern

[Files]
Source: "{#EffectsDir}\*"; DestDir: "{localappdata}\Programs\Common\VST3\OSTF Studio Effects"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Licenses}\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\assets\OstfScore.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\assets\OstfpPackage.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GmBundle}\*"; DestDir: "{localappdata}\Programs\Common\VST3\OSTF GM Sound Module.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArrangerBundle}\*"; DestDir: "{localappdata}\Programs\Common\VST3\OSTF Arranger.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#GmLicense}"; DestDir: "{app}"; DestName: "TinySoundFont-License.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\OSTF Studio"; Filename: "{app}\OSTF Studio.exe"

[Registry]
Root: HKCU; Subkey: "Software\Classes\.ostf"; ValueType: string; ValueName: ""; ValueData: "OSTFStudio.Score"; Check: CanSetDefault('.ostf')
Root: HKCU; Subkey: "Software\Classes\.ostfp"; ValueType: string; ValueName: ""; ValueData: "OSTFStudio.Project"; Check: CanSetDefault('.ostfp')
Root: HKCU; Subkey: "Software\Classes\.ostf\OpenWithProgids"; ValueType: string; ValueName: "OSTFStudio.Score"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.ostfp\OpenWithProgids"; ValueType: string; ValueName: "OSTFStudio.Project"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Score"; ValueType: string; ValueName: ""; ValueData: "OSTF Score"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Score\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\OstfScore.ico"",0"
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Score\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OSTF Studio.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Project"; ValueType: string; ValueName: ""; ValueData: "OSTF Studio Project"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\OstfpPackage.ico"",0"
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OSTF Studio.exe"" ""%1"""

[Code]
function CanSetDefault(const Extension: String): Boolean;
var
  Existing: String;
begin
  Result := not RegValueExists(HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + Extension + '\UserChoice', 'ProgId')
    and (not RegQueryStringValue(HKCR, Extension, '', Existing) or (Existing = ''));
end;

procedure RemoveOwnedDefault(const Extension, ProgId: String);
var
  Key, Existing: String;
begin
  Key := 'Software\Classes\' + Extension;
  if RegQueryStringValue(HKCU, Key, '', Existing) and (CompareText(Existing, ProgId) = 0) then
    RegDeleteValue(HKCU, Key, '');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveOwnedDefault('.ostf', 'OSTFStudio.Score');
    RemoveOwnedDefault('.ostfp', 'OSTFStudio.Project');
  end;
end;

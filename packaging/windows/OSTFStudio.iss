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
Source: "{#PluginSetup}"; Flags: dontcopy nocompression
Source: "{#Licenses}\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\assets\OstfScore.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\assets\OstfpPackage.ico"; DestDir: "{app}"; Flags: ignoreversion
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
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  ExtractTemporaryFile('OSTF-Studio-VST3-Setup.exe');
  if not ShellExec('runas', ExpandConstant('{tmp}\OSTF-Studio-VST3-Setup.exe'),
      '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-', '', SW_HIDE, ewWaitUntilTerminated, ExitCode)
      or (ExitCode <> 0) then
    Result := 'The shared VST3 plug-ins could not be installed. Close your DAWs, allow administrator access and retry. Error ' + IntToStr(ExitCode);
end;

procedure MigrateBundle(const LegacyFolder, BundleName, ProcessorCID: String);
var
  LegacyPath, BackupPath, BackupDir, NewPath: String;
  Metadata: AnsiString;
  Index: Integer;
begin
  LegacyPath := ExpandConstant('{localappdata}\Programs\Common\VST3\') + LegacyFolder + BundleName;
  NewPath := ExpandConstant('{commoncf64}\VST3\OSTF Studio\') + BundleName;
  if not DirExists(LegacyPath) or not DirExists(NewPath) then exit;
  if not LoadStringFromFile(LegacyPath + '\Contents\Resources\moduleinfo.json', Metadata) then exit;
  if (Pos(ProcessorCID, String(Metadata)) = 0) or (Pos('"OSTF Studio"', String(Metadata)) = 0) then exit;
  BackupDir := ExpandConstant('{localappdata}\OSTF Studio\Previous VST3');
  BackupPath := BackupDir + '\' + BundleName;
  Index := 0;
  while DirExists(BackupPath) do begin
    Index := Index + 1;
    BackupPath := BackupDir + '\' + IntToStr(Index) + '-' + BundleName;
  end;
  if not ForceDirectories(BackupDir) or not RenameFile(LegacyPath, BackupPath) then
    SuppressibleMsgBox('Close any DAWs using the previous plug-in and run setup again to move this duplicate: ' + LegacyPath, mbInformation, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    #include MigrationList
  end;
end;

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

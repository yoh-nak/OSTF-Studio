[Setup]
AppId={{6296B3E9-263E-4F3E-B29E-D5DF48519D34}
AppName=OSTF Studio
AppVersion={#AppVersion}
VersionInfoVersion={#BinaryVersion}
AppPublisher=OSTF
DefaultDirName={localappdata}\Programs\OSTF Studio
UsePreviousAppDir=yes
UsePreviousSetupType=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\OSTF Studio.exe
Uninstallable=WizardIsComponentSelected('studio')
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
OutputDir={#OutputDir}
#ifdef DawFilename
OutputBaseFilename=OSTF_Studio_VST3_Windows_x64_Setup
#else
OutputBaseFilename=OSTF_Studio_Windows_x64_Setup
#endif
SetupIconFile=..\..\assets\AppIcon.ico
WizardStyle=modern
SetupLogging=yes
UsePreviousTasks=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
#include GroupLabels
en.DawDemo=Studio VST3 for another DAW (with Paper Lantern Replies instruments and effects)
ja.DawDemo=他のDAWで使う Studio VST3（デモ曲の音源・エフェクト付き）
en.AllPlugins=Studio and all plug-ins
ja.AllPlugins=Studio 本体とすべてのプラグイン
en.AppOnly=Studio application only
ja.AppOnly=Studio 本体のみ
en.ChoosePlugins=Select individual plug-ins
ja.ChoosePlugins=プラグインを選択
en.Application=OSTF Studio application
ja.Application=OSTF Studio 本体
en.EmptySelection=Select the Studio application or at least one plug-in to install.
ja.EmptySelection=インストールするStudio本体またはプラグインを選んでください。
en.SharedTask=Install or update shared VST3 plug-ins for Studio and other DAWs (requires administrator approval)
ja.SharedTask=Studio と他の DAW で使う共有 VST3 をインストール・更新する（管理者の許可が必要）
en.SharedLaunchError=Windows could not start the shared VST3 installer. %1 (Windows error %2)
ja.SharedLaunchError=Windows が共有 VST3 インストーラーを起動できませんでした。%1（Windows エラー %2）
en.SharedExitError=The shared VST3 installer stopped (setup exit code %1). If a DAW is using an OSTF plug-in that needs updating, save your work and close that DAW before retrying.
ja.SharedExitError=共有 VST3 のインストールが中断されました（セットアップ終了コード %1）。更新が必要な OSTF プラグインを DAW で使用している場合は、作業を保存してその DAW を閉じてから再試行してください。
en.ContinueAppOnly=Continue installing the Studio application now? To add plug-ins later, run this installer again and choose them from the component list.
ja.ContinueAppOnly=今回は Studio 本体のインストールを続けますか？ プラグインは、あとでこのインストーラーを再実行し、一覧から選んで追加できます。

[Types]
Name: "full"; Description: "{cm:AllPlugins}"
Name: "daw"; Description: "{cm:DawDemo}"
Name: "apponly"; Description: "{cm:AppOnly}"
Name: "custom"; Description: "{cm:ChoosePlugins}"; Flags: iscustom

[Components]
Name: "studio"; Description: "{cm:Application}"; Types: full apponly custom
#include PluginComponents

[Tasks]
Name: "sharedvst3"; Description: "{cm:SharedTask}"

[Files]
Source: "{#PluginSetup}"; Flags: dontcopy nocompression
Source: "{#Licenses}\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: studio
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion; Components: studio
Source: "..\..\assets\OstfScore.ico"; DestDir: "{app}"; Flags: ignoreversion; Components: studio
Source: "..\..\assets\OstfpPackage.ico"; DestDir: "{app}"; Flags: ignoreversion; Components: studio
Source: "{#GmLicense}"; DestDir: "{app}"; DestName: "TinySoundFont-License.txt"; Flags: ignoreversion; Components: studio

[Icons]
Name: "{autoprograms}\OSTF Studio"; Filename: "{app}\OSTF Studio.exe"; Components: studio

[Registry]
Root: HKCU; Subkey: "Software\Classes\.ostf"; ValueType: string; ValueName: ""; ValueData: "OSTFStudio.Score"; Check: CanSetDefault('.ostf'); Components: studio
Root: HKCU; Subkey: "Software\Classes\.ostfp"; ValueType: string; ValueName: ""; ValueData: "OSTFStudio.Project"; Check: CanSetDefault('.ostfp'); Components: studio
Root: HKCU; Subkey: "Software\Classes\.ostf\OpenWithProgids"; ValueType: string; ValueName: "OSTFStudio.Score"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: studio
Root: HKCU; Subkey: "Software\Classes\.ostfp\OpenWithProgids"; ValueType: string; ValueName: "OSTFStudio.Project"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Components: studio
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Score"; ValueType: string; ValueName: ""; ValueData: "OSTF Score"; Flags: uninsdeletekey; Components: studio
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Score\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\OstfScore.ico"",0"; Components: studio
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Score\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OSTF Studio.exe"" ""%1"""; Components: studio
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Project"; ValueType: string; ValueName: ""; ValueData: "OSTF Studio Project"; Flags: uninsdeletekey; Components: studio
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\OstfpPackage.ico"",0"; Components: studio
Root: HKCU; Subkey: "Software\Classes\OSTFStudio.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OSTF Studio.exe"" ""%1"""; Components: studio

[Code]
var
  InstalledSelection: String;
#include "StudioChoice.iss"

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { Keep the sharedvst3 command-line task for existing automated installs. }
  Result := PageID = wpSelectTasks;
end;

function SelectedPlugins: String;
begin
  Result := '';
  #include PluginSelection
  if Result <> '' then Delete(Result, 1, 1);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
  Params, Selection: String;
  ShowCommand: Integer;
begin
  Result := '';
  UpdateStudioChoice;
  Selection := SelectedPlugins;
  if not WizardIsComponentSelected('studio') and ((Selection = '') or not WizardIsTaskSelected('sharedvst3')) then begin
    Result := CustomMessage('EmptySelection');
    exit;
  end;
  if (Selection = '') or (Selection = InstalledSelection) or not WizardIsTaskSelected('sharedvst3') then exit;
  ExtractTemporaryFile('OSTF-Studio-VST3-Setup.exe');
  Params := '/TYPE=custom /COMPONENTS="' + Selection + '" /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS';
  if WizardSilent then begin
    Params := '/VERYSILENT ' + Params;
    ShowCommand := SW_HIDE;
  end else begin
    Params := '/SILENT ' + Params;
    ShowCommand := SW_SHOWNORMAL;
  end;
  if not ShellExec('runas', ExpandConstant('{tmp}\OSTF-Studio-VST3-Setup.exe'),
      Params, '', ShowCommand, ewWaitUntilTerminated, ExitCode) then
    Result := FmtMessage(CustomMessage('SharedLaunchError'), [SysErrorMessage(ExitCode), IntToStr(ExitCode)])
  else if ExitCode <> 0 then
    Result := FmtMessage(CustomMessage('SharedExitError'), [IntToStr(ExitCode)])
  else
    InstalledSelection := Selection;
  if Result <> '' then begin
    Log(Result);
    if not WizardSilent and WizardIsComponentSelected('studio') then
      if MsgBox(Result + #13#10#13#10 + CustomMessage('ContinueAppOnly'),
          mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then Result := '';
  end;
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
  if (CurStep = ssPostInstall) and (InstalledSelection <> '') then begin
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

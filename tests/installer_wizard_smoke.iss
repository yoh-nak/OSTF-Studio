; Exercise the native component controls without installing any files or registry entries.
#define StudioDependency Stage + "\studio-dependency.iss"
[Setup]
AppName=Studio installer selection test
AppVersion=1
OutputBaseFilename=wizard-test
PrivilegesRequired=lowest
CreateAppDir=no
Uninstallable=no
UsePreviousSetupType=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
#include Stage + "\groups.iss"

[Types]
Name: "full"; Description: "Full"
Name: "apponly"; Description: "Application only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "studio"; Description: "Studio"; Types: full apponly custom
#include Stage + "\components.iss"

[Code]
#include ChoiceScript

var
  Errors: String;

procedure Expect(const Step: String; Selected, Editable: Boolean);
begin
  if (WizardIsComponentSelected('studio') <> Selected) or
     (WizardForm.ComponentsList.ItemEnabled[0] <> Editable) then
    Errors := Errors + Step + #13#10;
end;

procedure Click(const Selection: String);
begin
  WizardSelectComponents(Selection);
  WizardForm.ComponentsList.OnClickCheck(WizardForm.ComponentsList);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  UpdateStudioChoice;
  Expect('command line with dependent plug-in', True, False);
  Click('*utilities,!effects\Tx01');
  Expect('another dependent remains', True, False);
  Click('!utilities,effects\Fx01');
  Expect('independent plug-in only', True, True);
  Click('!studio');
  Expect('manual opt out', False, True);
  Click('*effects');
  Expect('select dependent category', True, False);
  WizardForm.TypesCombo.ItemIndex := 1;
  WizardForm.TypesCombo.OnChange(WizardForm.TypesCombo);
  Expect('application-only preset', True, True);
  WizardForm.TypesCombo.ItemIndex := 0;
  WizardForm.TypesCombo.OnChange(WizardForm.TypesCombo);
  Expect('full preset', True, False);
  if Errors = '' then Errors := 'ok';
  SaveStringToFile(ExpandConstant('{param:RESULT}'), AnsiString(Errors), False);
end;

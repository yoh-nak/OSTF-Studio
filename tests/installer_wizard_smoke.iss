; Exercise the native component controls without installing any files or registry entries.
#define StudioDependency Stage + "\studio-dependency.iss"
#define GroupLabels Stage + "\groups.iss"
#define PluginComponents Stage + "\components.iss"
[Setup]
AppName=Studio installer selection test
AppVersion=1
OutputBaseFilename=wizard-test
PrivilegesRequired=lowest
CreateAppDir=no
Uninstallable=no
UsePreviousSetupType=no

#include Stage + "\product-choices.iss"

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
var
  I: Integer;
begin
  Result := '';
  UpdateStudioChoice;
  if ExpandConstant('{param:DEFAULTS|0}') = '1' then begin
    if WizardSetupType(False) <> 'full' then Errors := 'Expected full preset';
    for I := 0 to WizardForm.ComponentsList.Items.Count - 1 do
      if not WizardForm.ComponentsList.Checked[I] then
        Errors := Errors + ' Unchecked: ' + WizardForm.ComponentsList.Items[I];
    if Errors = '' then Errors := 'ok';
    SaveStringToFile(ExpandConstant('{param:RESULT}'), AnsiString(Errors), False);
    exit;
  end;
  Expect('command line with dependent plug-in', True, False);
  Click('*utilities,!effects\Tx01');
  Expect('another dependent remains', True, False);
  Click('!utilities,effects\Fx01');
  Expect('independent plug-in only', True, True);
  Click('!studio');
  Expect('manual opt out', False, True);
  Click('*effects');
  Expect('select dependent category', True, False);
  WizardForm.TypesCombo.ItemIndex := 2;
  WizardForm.TypesCombo.OnChange(WizardForm.TypesCombo);
  Expect('application-only preset', True, True);
  WizardForm.TypesCombo.ItemIndex := 0;
  WizardForm.TypesCombo.OnChange(WizardForm.TypesCombo);
  Expect('full preset', True, False);
  WizardForm.TypesCombo.ItemIndex := 1;
  WizardForm.TypesCombo.OnChange(WizardForm.TypesCombo);
  Expect('DAW preset omits standalone application', False, True);
  if not WizardIsComponentSelected('effects\Fx01') or NeedsStudio then
    Errors := Errors + 'DAW preset selected the wrong plug-ins';
  if Errors = '' then Errors := 'ok';
  SaveStringToFile(ExpandConstant('{param:RESULT}'), AnsiString(Errors), False);
end;

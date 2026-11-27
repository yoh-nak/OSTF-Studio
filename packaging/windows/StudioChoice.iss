var
  OriginalComponentClick, OriginalTypeChange: TNotifyEvent;

function NeedsStudio: Boolean;
begin
  #include StudioDependency
end;

procedure UpdateStudioChoice;
var
  Required: Boolean;
begin
  Required := NeedsStudio;
  { Studio is the first row in [Components], ahead of all catalog groups. }
  if Required and not WizardIsComponentSelected('studio') then
    WizardSelectComponents('studio');
  WizardForm.ComponentsList.ItemEnabled[0] := not Required;
end;

procedure ComponentClick(Sender: TObject);
begin
  UpdateStudioChoice;
  OriginalComponentClick(Sender);
end;

procedure TypeChange(Sender: TObject);
begin
  OriginalTypeChange(Sender);
  UpdateStudioChoice;
end;

procedure InitializeWizard;
begin
  OriginalComponentClick := WizardForm.ComponentsList.OnClickCheck;
  OriginalTypeChange := WizardForm.TypesCombo.OnChange;
  WizardForm.ComponentsList.OnClickCheck := @ComponentClick;
  WizardForm.TypesCombo.OnChange := @TypeChange;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectComponents then UpdateStudioChoice;
end;

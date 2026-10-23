; Installer für AutoNeg unter Windows (Inno Setup 6).
; Wird von build_installer.ps1 aufgerufen; StageDir ist der Inhalt von "cmake --install".
;   ISCC /DStageDir=... /DAppVersion=1.0.0 /DOutputDir=... autoneg.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef StageDir
  #error StageDir fehlt (Ordner mit der installierten Anwendung)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Feste Kennung: Aktualisierungen ersetzen eine vorhandene Installation
AppId={{6F1C2A0E-3B7D-4E5A-9C61-8D2F4B7A1E35}
AppName=AutoNeg
AppVersion={#AppVersion}
AppVerName=AutoNeg {#AppVersion}
AppComments=Konvertierung kameradigitalisierter Filmnegative und Dias
DefaultDirName={autopf}\AutoNeg
DefaultGroupName=AutoNeg
DisableProgramGroupPage=yes
; Installation für alle Benutzer (Administrator) oder nur für den eigenen Benutzer
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1903: ICU (icuuc.dll, von Qt genutzt) ist Teil des Systems
MinVersion=10.0.18362
OutputDir={#OutputDir}
OutputBaseFilename=AutoNeg-{#AppVersion}-Setup-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\autoneg.exe
UninstallDisplayName=AutoNeg {#AppVersion}
ChangesEnvironment=yes
CloseApplications=yes

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
de.AddToPath=Kommandozeile autoneg-cli zum Suchpfad (PATH) hinzufügen
en.AddToPath=Add the command line tool autoneg-cli to the search path (PATH)
de.Manual=AutoNeg-Handbuch
en.Manual=AutoNeg manual
de.Licenses=Lizenzen der Fremdbibliotheken
en.Licenses=Third-party licences

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addtopath"; Description: "{cm:AddToPath}"; Flags: unchecked

[InstallDelete]
; Reste einer älteren Version (Plugins, DLLs) vor dem Kopieren entfernen
Type: filesandordirs; Name: "{app}\plugins"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\AutoNeg"; Filename: "{app}\autoneg.exe"
Name: "{autoprograms}\{cm:Manual}"; Filename: "{app}\handbuch.pdf"; Check: FileExists(ExpandConstant('{app}\handbuch.pdf'))
Name: "{autoprograms}\{cm:Licenses}"; Filename: "{app}\licenses"
Name: "{autodesktop}\AutoNeg"; Filename: "{app}\autoneg.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\autoneg.exe"; Description: "{cm:LaunchProgram,AutoNeg}"; Flags: nowait postinstall skipifsilent

[Code]
// Suchpfad: Administrator-Installation im System-, sonst im Benutzerumfeld
function EnvRoot: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function EnvKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

function PathContains(const Path, Dir: String): Boolean;
begin
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') > 0;
end;

procedure AddToPath(const Dir: String);
var
  Path: String;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Path) then Path := '';
  if PathContains(Path, Dir) then Exit;
  if (Path <> '') and (Copy(Path, Length(Path), 1) <> ';') then Path := Path + ';';
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Path + Dir);
end;

procedure RemoveFromPath(const Dir: String);
var
  Path: String;
  P: Integer;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Path) then Exit;
  Path := ';' + Path + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(Path));
  if P = 0 then Exit;
  Delete(Path, P, Length(Dir) + 1);
  Path := Copy(Path, 2, Length(Path) - 2);
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Path);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Einstellungen, Vorlagen und Begleitdateien der Scans bleiben erhalten
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;

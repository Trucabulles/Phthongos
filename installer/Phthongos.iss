#ifndef PackageDir
  #error PackageDir must point to a clean packaged release
#endif
#ifndef OutputDir
  #define OutputDir "..\installers"
#endif
[Setup]
AppId={{6ED12C37-43DD-4E8F-99AF-74AC207A9C2F}
AppName=Phthongos
AppVersion=0.1.0
AppPublisher=Trucabulles
DefaultDirName={localappdata}\Programs\Phthongos
DefaultGroupName=Phthongos
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
ShowLanguageDialog=yes
UsePreviousLanguage=yes
LanguageDetectionMethod=none
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Phthongos-Setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\Phthongos.exe
CloseApplications=yes
RestartApplications=no
[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"; LicenseFile: "{#PackageDir}\LICENSE_PHTHONGOS_EN.md"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"; LicenseFile: "{#PackageDir}\LICENSE_PHTHONGOS.md"
[CustomMessages]
fr.AppLanguageTitle=Langue de Phthongos
fr.AppLanguageDescription=Choisissez la langue de l'application.
fr.AppLanguageHelp=Vous pourrez la changer dans les paramètres de Phthongos. Le choix sera appliqué au prochain lancement.
en.AppLanguageTitle=Phthongos language
en.AppLanguageDescription=Choose the application language.
en.AppLanguageHelp=You can change it in Phthongos settings. The choice is applied the next time you start the application.
fr.StartMenu=Créer un raccourci dans le menu Démarrer
en.StartMenu=Create a Start menu shortcut
fr.Desktop=Créer un raccourci sur le Bureau
en.Desktop=Create a desktop shortcut
fr.PreferenceError=Impossible d'enregistrer la préférence de langue.
en.PreferenceError=Unable to save the language preference.
[Tasks]
Name: "startmenu"; Description: "{cm:StartMenu}"
Name: "desktop"; Description: "{cm:Desktop}"; Flags: unchecked
[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "data\*,test-results\*"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{userprograms}\Phthongos\Phthongos"; Filename: "{app}\Phthongos.exe"; Tasks: startmenu
Name: "{userprograms}\Phthongos\{cm:UninstallProgram,Phthongos}"; Filename: "{uninstallexe}"; Tasks: startmenu
Name: "{userdesktop}\Phthongos"; Filename: "{app}\Phthongos.exe"; Tasks: desktop
[Run]
Filename: "{app}\Phthongos.exe"; Description: "{cm:LaunchProgram,Phthongos}"; Flags: nowait postinstall skipifsilent
[Code]
var LanguagePage: TInputOptionWizardPage;
    PreviousDirectory: String;
procedure InitializeWizard;
begin
  LanguagePage := CreateInputOptionPage(wpSelectDir, CustomMessage('AppLanguageTitle'), CustomMessage('AppLanguageDescription'), CustomMessage('AppLanguageHelp'), True, False);
  LanguagePage.Add('Français');
  LanguagePage.Add('English');
  LanguagePage.SelectedValueIndex := 1;
end;
procedure CurPageChanged(CurPageID: Integer);
var Bytes: AnsiString; Contents, Directory: String;
begin
  if CurPageID = LanguagePage.ID then begin
    Directory := WizardDirValue;
    if Directory <> PreviousDirectory then begin
      PreviousDirectory := Directory;
      if LoadStringFromFile(Directory + '\data\language.json', Bytes) then begin
        Contents := String(Bytes);
        StringChangeEx(Contents, ' ', '', True);
        StringChangeEx(Contents, #9, '', True);
        StringChangeEx(Contents, #13, '', True);
        StringChangeEx(Contents, #10, '', True);
        if Pos('"language":"en"', Contents) > 0 then LanguagePage.SelectedValueIndex := 1
        else if Pos('"language":"fr"', Contents) > 0 then LanguagePage.SelectedValueIndex := 0;
      end;
    end;
  end;
end;
procedure CurStepChanged(CurStep: TSetupStep);
var Code, Path, Contents: String;
begin
  if CurStep = ssPostInstall then begin
    if LanguagePage.SelectedValueIndex = 0 then Code := 'fr' else Code := 'en';
    Path := ExpandConstant('{app}\data');
    ForceDirectories(Path);
    Contents := '{"language":"' + Code + '"}';
    if not SaveStringToFile(Path + '\language.json', Contents, False) then
      MsgBox(CustomMessage('PreferenceError'), mbError, MB_OK);
  end;
end;

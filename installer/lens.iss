; Lens, packaged. windeployqt lays the Qt runtime out beside the executable and Inno Setup packs
; that, plus data/, into one installer:
;
;     cmake --build build-ninja-release --target installer
;
; Run it in the release tree (scripts\build\build-release.bat): the payload below is named by tree, so
; from the debug tree the deploy would land there and this script would pack the release tree
; anyway. The number of the version lives in project() alone -- CMake generates the include below
; from it, so a release bumps one place.
;
; Two things the installer deliberately does not do.
;
; It writes no Run entry for autostart. The app writes that itself, from its own path, when the
; reader turns the switch on (src/app/autostart.cpp); an entry naming this tree would start the
; wrong copy, and one written here would be a second thing claiming to own the setting.
;
; It does not carry the settings document, and it does not remove it on uninstall. It lives in
; %APPDATA%\Lens and holds the reader's API key and word marks (src/app/main.cpp); uninstalling
; the program must not take the reader's data with it.

#include "version.iss"

; SourcePath is the directory of this script, with a trailing backslash. The release tree name
; is scripts\build\build-release.bat's and is written here rather than passed in: this file is in the
; repository, and the repository knows its own layout.
#define PayloadDir SourcePath + "..\build-ninja-release\installer\payload"
#define DataDir SourcePath + "..\data"

[Setup]
AppId={{60F0699D-0B49-4E0C-B700-4497793B55E8}
AppName=Lens
AppVersion={#AppVersion}
AppVerName=Lens {#AppVersion}
DefaultDirName={autopf}\Lens
DefaultGroupName=Lens
DisableProgramGroupPage=yes
; Per-user, so no elevation: the program goes to %LOCALAPPDATA%\Programs\Lens and the shortcut
; to the reader's own Start Menu. One reader's tool, and the Run entry autostart writes is
; per-user too; nothing here needs an administrator's rights or a machine-wide location.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
OutputDir=..\build-ninja-release\installer
OutputBaseFilename=Lens-{#AppVersion}-setup
SetupIconFile=..\icons\lens.ico
UninstallDisplayIcon={app}\lens.exe
Compression=lzma2/max
SolidCompression=yes

[Files]
; The payload is exactly what windeployqt wrote plus lens.exe: a directory of its own, so this
; glob cannot reach the build tree's object files, and the size the package report measures is
; the size the reader gets.
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#DataDir}\*"; DestDir: "{app}\data"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; WorkingDir is named rather than left to the default: the app writes its rotating log to logs/
; below the directory it is started in (src/util/log.cpp), and a shortcut that does not say which
; that is leaves the file wherever the shell happened to choose.
Name: "{group}\Lens"; Filename: "{app}\lens.exe"; WorkingDir: "{app}"

[UninstallDelete]
; The log directory the app creates at run time. It is not in the [Files] list, so nothing else
; would remove it and an uninstall would leave it behind.
Type: filesandordirs; Name: "{app}\logs"

[Run]
Filename: "{app}\lens.exe"; Description: "{cm:LaunchProgram,Lens}"; Flags: nowait postinstall skipifsilent

[Code]
const
  LensBackground = $00EFEBE9;
  LensPanel = $00FFFFFF;
  LensText = $001F1A1A;
  LensMuted = $006F6666;

procedure StyleLabel(const ALabel: TNewStaticText; const ASize: Integer; const AColor: TColor);
begin
  ALabel.Font.Name := 'Segoe UI Variable';
  ALabel.Font.Size := ASize;
  ALabel.Font.Color := AColor;
end;

procedure InitializeWizard;
begin
  { Keep the wizard in the same restrained neutral visual language as Lens. }
  WizardForm.Font.Name := 'Segoe UI Variable';
  WizardForm.Font.Size := 9;
  WizardForm.Font.Color := LensText;
  WizardForm.Color := LensBackground;
  WizardForm.MainPanel.Color := LensPanel;
  WizardForm.InnerPage.Color := LensBackground;

  StyleLabel(WizardForm.PageNameLabel, 14, LensText);
  StyleLabel(WizardForm.PageDescriptionLabel, 9, LensMuted);
  StyleLabel(WizardForm.WelcomeLabel1, 14, LensText);
  StyleLabel(WizardForm.WelcomeLabel2, 9, LensMuted);
  StyleLabel(WizardForm.FinishedHeadingLabel, 14, LensText);
  StyleLabel(WizardForm.FinishedLabel, 9, LensMuted);
  StyleLabel(WizardForm.StatusLabel, 9, LensText);
  StyleLabel(WizardForm.FilenameLabel, 9, LensMuted);

  WizardForm.NextButton.Font.Name := 'Segoe UI Variable';
  WizardForm.NextButton.Font.Size := 9;
  WizardForm.BackButton.Font.Name := 'Segoe UI Variable';
  WizardForm.BackButton.Font.Size := 9;
  WizardForm.CancelButton.Font.Name := 'Segoe UI Variable';
  WizardForm.CancelButton.Font.Size := 9;

end;

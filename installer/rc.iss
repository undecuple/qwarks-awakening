; Qwark's Awakening: Inno Setup 6 script for the launcher and its build kit (one setup exe, Windows x64).
; Adapted from the Killzone 3 port's installer/kz3.iss (Project Helghast, MIT, same author): the per-user install,
; the folder rules, the shortcuts and the uninstaller that asks before it removes the player's data. Left out: the
; game pages, the build step, the update mode, the version check and the licence page.
;
;   iscc /DPayload=<payload dir> /DAppVersion=<x.y.z> installer\rc.iss      (output: installer\Output\)
;
; Payload = the unpacked full build zip's folder (tools/release/pack_zip.py, names.build_zip_top): the launcher,
; SDL3.dll, fonts\, python\, tools\, data\, licenses\, the build kit (port\, cmake\, ps3recomp\, toolchain\) and
; kit.json. Every file of it is installed as it is, so the installed files are byte-identical to the zip's.
; PayloadTops below lists its top-level names; the compile stops on any other name (the [InstallDelete] and
; [UninstallDelete] folder lists must then be extended) and tools/release/test_rc_iss.py checks the list against
; pack_zip.py.
;
; What it does: per-user install (no admin) into {localappdata}\Programs\Qwark's Awakening by default (the folder
; page can change it); Start-menu entry, desktop shortcut (ticked), "Start Qwark's Awakening" on the finish page.
; Nothing is downloaded and no game file is read. Inno's own log (SetupLogging) is the one setup log, in %TEMP%
; ("Setup Log <date> #<n>.txt"; /LOG=FILE puts it elsewhere).
; The launcher writes the player's data next to itself (launcher/src/main.cpp: library.json, games\, build\, logs\,
; saves\, art-cache\). An install over an earlier one keeps it: setup replaces only its own folders. The uninstaller
; removes the installed files and asks before it removes that data (default keep; a silent uninstall keeps it).
; Silent: /VERYSILENT /SUPPRESSMSGBOXES [/DIR="...\Qwark's Awakening"] [/TASKS=""] (no desktop shortcut).

#ifndef AppVersion
  #error AppVersion is needed: iscc /DAppVersion=X.Y.Z /DPayload=DIR rc.iss
#endif
#ifndef Payload
  #error Payload is needed: the unpacked full build zip's folder (iscc /DPayload=DIR)
#endif
#if VER < EncodeVer(6,6,0)
  #error Inno Setup 6.6.0 or later is needed (WizardStyle dark; tested with 6.7.3)
#endif

; Names block: mirrors tools/release/names.py (PRODUCT, ASSET_STEM, LAUNCHER_EXE, REPO_URL, DESCRIPTION,
; setup_exe_name); tools/release/test_rc_iss.py fails when they differ. AppId never changes.
#define AppName "Qwark's Awakening"
#define AppDirName "Qwark's Awakening"
; the same name for Pascal string literals in [Code] (the apostrophe doubled)
#define AppDirNameP StringChange(AppDirName, "'", "''")
#define AppDescription "Ratchet & Clank (PS3) // Unofficial PC Port (static recompilation)"
#define AssetStem "Qwarks-Awakening"
#define LauncherExe "Qwarks-Awakening-launcher.exe"
#define RepoURL "https://github.com/undecuple/qwarks-awakening"

; The payload's top-level names (lower case, '|' between them). Folders also in PayloadDirs.
#define PayloadTops "qwarks-awakening-launcher.exe|sdl3.dll|readme.txt|kit.json|fonts|python|licenses|tools|data|port|cmake|ps3recomp|toolchain"
#define PayloadDirs "fonts|python|licenses|tools|data|port|cmake|ps3recomp|toolchain"

#if !FileExists(AddBackslash(Payload) + LauncherExe)
  #error the payload has no launcher (Payload\Qwarks-Awakening-launcher.exe): not the full build zip's folder
#endif
#if !FileExists(AddBackslash(Payload) + "kit.json")
  #error the payload has no kit.json: the bare launcher zip, not the full build zip
#endif

; Compile-time check of the payload's top level against PayloadTops
#define FindHandle
#define FindResult
#define TopName
#sub CheckTop
  #define public TopName FindGetFileName(FindHandle)
  #if TopName != "." && TopName != ".."
    #if Pos("|" + LowerCase(TopName) + "|", "|" + PayloadTops + "|") == 0
      #pragma message "payload entry not listed in PayloadTops: " + TopName
      #error the payload holds a top-level name rc.iss does not list (see the message above)
    #endif
  #endif
#endsub
#for {FindHandle = FindResult = FindFirst(AddBackslash(Payload) + "*", faAnyFile); FindResult; FindResult = FindNext(FindHandle)} CheckTop
#if FindHandle
  #expr FindClose(FindHandle)
#endif

[Setup]
AppId={{A7611758-5595-4E3C-8AD9-F026B988B363}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName} contributors
AppComments={#AppDescription}
AppPublisherURL={#RepoURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=
DefaultDirName={localappdata}\Programs\{#AppDirName}
DisableDirPage=no
; Browse appends the app's own folder name; the [Code] rules keep the last folder name ours
AppendDefaultDirName=yes
; File times in UTC, converted to the player's time zone on install: the release is built on a UTC runner, and a
; source file dated in the future makes ninja rebuild forever (the Killzone 3 port's lesson, 2026-10-28)
TimeStampsInUTC=yes
DefaultGroupName={#AppDirName}
DisableProgramGroupPage=yes
UsePreviousGroup=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Dark, like the art (installer_art.py PAL "bg"); a Windows high-contrast theme still turns the custom style off
WizardStyle=modern dark
WizardBackColor=#070b16
WizardImageBackColor=#070b16
WizardSmallImageBackColor=#070b16
DisableWelcomePage=no
; Pages: welcome, folder, tasks, installing, finish (no ready page: the tasks page's button says Install)
DisableReadyPage=yes
SetupLogging=yes
OutputBaseFilename={#AssetStem}-{#AppVersion}-setup-windows-x64
Compression=lzma2/max
SolidCompression=yes
; An install over a running launcher: Windows' Restart Manager asks to close it first
CloseApplications=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\rc-uninstall.ico
SetupIconFile=art\rc-setup.ico
; Inno picks the closest size per DPI (100/125/150/200 %); relative to this script's folder
WizardImageFile=art\wizard-100.bmp,art\wizard-125.bmp,art\wizard-150.bmp,art\wizard-200.bmp
WizardSmallImageFile=art\header-100.bmp,art\header-125.bmp,art\header-150.bmp,art\header-200.bmp
WizardImageStretch=yes

[Messages]
WizardSelectDir=Select destination location
WizardSelectTasks=Select additional tasks
WizardUninstalling=Uninstall status
UninstallAppFullTitle=Uninstall %1
BrowseDialogTitle=Browse for folder
ButtonNewFolder=&Make new folder
WelcomeLabel1={#AppName} {#AppVersion}
WelcomeLabel2=An unofficial fan port of Ratchet && Clank games from the PS3 to Windows.%n%nThis installs the launcher and the build kit. The launcher finds your own copy of each game, builds it on this PC and starts it.%n%nNo game files are included. Setup downloads nothing.
FinishedHeadingLabel=Setup is done
FinishedLabel={#AppName} is installed. Start it from the desktop or the Start menu.
FinishedLabelNoIcons={#AppName} is installed.
SelectDirLabel3=Setup installs {#AppName} into a "{#AppDirName}" folder inside the location you choose.
SelectDirBrowseLabel=To continue, click Next.

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
; The whole payload, as it is: the zip's file list is the install's
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Apps & features icon (UninstallDisplayIcon) and the Start menu's Uninstall entry
Source: "art\rc-uninstall.ico"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Install over an earlier version: the payload's own folders are replaced whole, so a file a newer version dropped
; cannot linger and be built or run. Never listed here: the player's data (library.json, games, build, logs, saves,
; art-cache). Keep this list equal to PayloadDirs (test_rc_iss.py).
Type: filesandordirs; Name: "{app}\fonts"
Type: filesandordirs; Name: "{app}\python"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\tools"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\port"
Type: filesandordirs; Name: "{app}\cmake"
Type: filesandordirs; Name: "{app}\ps3recomp"
Type: filesandordirs; Name: "{app}\toolchain"
; the launcher's split debug file (Settings, "Get launcher symbols") belongs to the old launcher only
Type: files; Name: "{app}\launcher.exe.debug"

[Icons]
Name: "{group}\{#AppDirName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppDirName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\rc-uninstall.ico"
Name: "{autodesktop}\{#AppDirName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#LauncherExe}"; Description: "Start {#AppName}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Inno removes every file it installed. These are what the launcher and its Python engine write besides the player's
; data: the payload's folders go whole (Python writes __pycache__ into tools\ and ps3recomp\), the debug file, the build
; lock and the kit download's temporary folders (tools/release/catalog.py BUILD_LOCK, kit.py STAGING and DOWNLOADS).
; The player's data is not listed: CurUninstallStepChanged asks first. Keep the folders equal to PayloadDirs.
Type: filesandordirs; Name: "{app}\fonts"
Type: filesandordirs; Name: "{app}\python"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\tools"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\port"
Type: filesandordirs; Name: "{app}\cmake"
Type: filesandordirs; Name: "{app}\ps3recomp"
Type: filesandordirs; Name: "{app}\toolchain"
Type: files; Name: "{app}\launcher.exe.debug"
Type: files; Name: "{app}\.build.lock"
Type: filesandordirs; Name: "{app}\.kit-staging"
Type: filesandordirs; Name: "{app}\.kit-download"

[Code]
// Install folder rules, adapted from kz3.iss. The uninstaller removes whole payload folders ({app}\tools, ...), so the
// install folder must be ours: its last name is the app's, and an existing folder with files must hold unins000.exe.

function HasCMakeBadChar(Dir: String): String;
var
  I: Integer;
begin
  Result := '';
  for I := 1 to Length(Dir) do
    if (Dir[I] = '[') or (Dir[I] = ']') or (Dir[I] = ';') then begin
      Result := Dir[I];
      Exit;
    end;
end;

var
  StaleDirNote: String;

// The folder an earlier install of this app recorded (Inno's uninstall key), or ''
function PreviousInstallDir: String;
var
  K, S: String;
begin
  Result := '';
  K := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A7611758-5595-4E3C-8AD9-F026B988B363}_is1';
  if RegQueryStringValue(HKCU, K, 'Inno Setup: App Path', S) then
    Result := RemoveBackslashUnlessRoot(S);
end;

// Inno proposes the previous install's folder. When it holds [ ] ; (the build tools cannot use it) or no longer
// exists, the default folder is proposed instead, with the reason on the page.
procedure DropStalePreviousDir;
var
  Prev, Why: String;
begin
  Prev := PreviousInstallDir;
  if (Prev = '') or (CompareText(RemoveBackslashUnlessRoot(WizardDirValue), Prev) <> 0) then Exit;
  if HasCMakeBadChar(Prev) <> '' then
    Why := 'contains the character "' + HasCMakeBadChar(Prev) + '", which the build tools cannot use'
  else if not DirExists(Prev) then
    Why := 'no longer exists'
  else
    Exit;
  WizardForm.DirEdit.Text := ExpandConstant('{localappdata}\Programs\{#AppDirNameP}');
  StaleDirNote := 'The folder of the earlier install (' + Prev + ') ' + Why + ', so the default folder is proposed.';
  WizardForm.SelectDirBrowseLabel.Caption := StaleDirNote + ' ' + WizardForm.SelectDirBrowseLabel.Caption;
  Log('install folder: the previous folder ' + Prev + ' ' + Why + '; proposing the default');
end;

procedure InitializeWizard;
begin
  DropStalePreviousDir;
  // Inno's folder icon left an empty gap under Proton (kz3.iss); the page reads the same without it
  WizardForm.SelectDirBitmapImage.Visible := False;
  WizardForm.SelectDirLabel.Width := WizardForm.SelectDirLabel.Left + WizardForm.SelectDirLabel.Width -
    WizardForm.SelectDirBitmapImage.Left;
  WizardForm.SelectDirLabel.Left := WizardForm.SelectDirBitmapImage.Left;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectDir) and (StaleDirNote = '') then DropStalePreviousDir;
end;

function DirHasFiles(Dir: String): Boolean;
var
  FR: TFindRec;
begin
  Result := False;
  if not FindFirst(AddBackslash(Dir) + '*', FR) then Exit;
  try
    repeat
      if (FR.Name <> '.') and (FR.Name <> '..') then Result := True;
    until Result or (not FindNext(FR));
  finally
    FindClose(FR);
  end;
end;

function IsProtectedFolder(Dir: String): Boolean;
var
  D, Prof: String;
begin
  D := Lowercase(RemoveBackslashUnlessRoot(Dir));
  Prof := RemoveBackslashUnlessRoot(ExpandConstant('{%USERPROFILE|}'));
  Result := (D = Lowercase(RemoveBackslashUnlessRoot(ExtractFileDrive(Dir)))) or
    (D = Lowercase(RemoveBackslashUnlessRoot(ExpandConstant('{win}')))) or
    (D = Lowercase(RemoveBackslashUnlessRoot(ExpandConstant('{commonpf}')))) or
    (D = Lowercase(RemoveBackslashUnlessRoot(ExpandConstant('{userdocs}')))) or
    (D = Lowercase(RemoveBackslashUnlessRoot(ExpandConstant('{userdesktop}')))) or
    ((Prof <> '') and ((D = Lowercase(Prof)) or (D = Lowercase(Prof + '\Downloads'))));
end;

// True when Dir is OneDrive's folder or inside it (whole path components, any letter case)
function InOneDrive(Dir: String): Boolean;
var
  D, B: String;
  I: Integer;
  Vars: array of String;
begin
  Result := False;
  D := Lowercase(RemoveBackslashUnlessRoot(Dir));
  SetArrayLength(Vars, 3);
  Vars[0] := 'OneDrive';
  Vars[1] := 'OneDriveConsumer';
  Vars[2] := 'OneDriveCommercial';
  for I := 0 to 2 do begin
    B := Lowercase(RemoveBackslashUnlessRoot(GetEnv(Vars[I])));
    if (B <> '') and ((D = B) or (Pos(B + '\', D) = 1)) then
      Result := True;
  end;
end;

// '' when Dir (the final install folder) is acceptable, else the reason
function InstallDirProblem(Dir: String): String;
var
  D: String;
begin
  Result := '';
  D := RemoveBackslashUnlessRoot(Dir);
  if HasCMakeBadChar(D) <> '' then
    Result := 'The folder ' + D + ' contains the character "' + HasCMakeBadChar(D) + '". The build tools cannot use ' +
      'a folder with [ ] or ; in its path. Choose a folder whose path has none of [ ] ; (the default is fine).'
  else if CompareText(ExtractFileName(D), '{#AppDirNameP}') <> 0 then
    Result := 'The install folder must be called "{#AppDirNameP}" (it is: ' + D + ').'
  else if InOneDrive(D) then
    Result := 'The folder ' + D + ' is inside OneDrive. OneDrive syncs and locks the build''s files while it runs. ' +
      'Choose a folder outside OneDrive (the default is fine).'
  else if IsProtectedFolder(D) then
    Result := 'This folder cannot be used as the install folder: ' + D
  else if DirExists(D) and DirHasFiles(D) and (not FileExists(AddBackslash(D) + 'unins000.exe')) then
    Result := 'The folder ' + D + ' already holds files that are not from {#AppDirNameP}. Choose another ' +
      'location or an empty folder.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem, D: String;
begin
  Result := True;
  // not in silent mode: a False here would wait for a click nobody gives; PrepareToInstall refuses a bad /DIR=
  if (CurPageID = wpSelectDir) and (not WizardSilent) then begin
    D := RemoveBackslashUnlessRoot(WizardDirValue);
    if (D <> '') and (CompareText(ExtractFileName(D), '{#AppDirNameP}') <> 0) then begin
      WizardForm.DirEdit.Text := AddBackslash(D) + '{#AppDirNameP}';
      Log('install folder: appended "{#AppDirNameP}" to ' + D);
    end;
    Problem := InstallDirProblem(WizardDirValue);
    if Problem <> '' then begin
      MsgBox(Problem, mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// Silent installs skip the folder page: the same rules run here, before any file is written (exit code 7)
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := InstallDirProblem(ExpandConstant('{app}'));
  if Result <> '' then Log('install folder refused: ' + Result);
end;

// The player's data, written next to the launcher (launcher/src/main.cpp DefaultLocation and library.json;
// the build folder firstrun defaults to). Never in [InstallDelete] or [UninstallDelete].
function HasPlayerData(App: String): Boolean;
begin
  Result := FileExists(App + '\library.json') or DirExists(App + '\games') or DirExists(App + '\build') or
    DirExists(App + '\logs') or DirExists(App + '\saves') or DirExists(App + '\art-cache');
end;

procedure DeletePlayerData(App: String);
begin
  DeleteFile(App + '\library.json');
  DelTree(App + '\games', True, True, True);
  DelTree(App + '\build', True, True, True);
  DelTree(App + '\logs', True, True, True);
  DelTree(App + '\saves', True, True, True);
  DelTree(App + '\art-cache', True, True, True);
  RemoveDir(App);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App, Data: String;
begin
  // After the program files are gone. Ask, default No = keep; a silent uninstall keeps everything.
  if CurUninstallStep <> usPostUninstall then Exit;
  if UninstallSilent then Exit;
  App := ExpandConstant('{app}');
  if HasPlayerData(App) then
    if MsgBox('Also delete your library, your built games with their saves, and the logs?' + #13#10 + App + #13#10 + #13#10 +
              'No keeps them for a later install.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DeletePlayerData(App);
  // The games' settings (port/rc/rc_settings.h: %APPDATA%\Qwark's Awakening; the saves are under games\<id>\gamedata
  // by default, port/rc/main.cpp, so the question above covers them). The folder shared with
  // the other PS3 ports (%APPDATA%\PS3Recomp) is never touched.
  Data := ExpandConstant('{userappdata}\{#AppDirNameP}');
  if DirExists(Data) then
    if MsgBox('Also delete the games'' settings (controls and display)?' + #13#10 + Data + #13#10 + #13#10 +
              'No keeps them for a later install.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(Data, True, True, True);
end;

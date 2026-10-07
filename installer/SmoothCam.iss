; SmoothCam installer (Inno Setup 6)
;
; Build with scripts\package.ps1, or by hand:
;   ISCC.exe /DBuildDir=..\build\x64-release\bin installer\SmoothCam.iss
;
; Silent install/uninstall (needed for the Microsoft Store listing):
;   SmoothCam_v0.0.1.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
;   "%ProgramFiles%\SmoothCam\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

#ifndef BuildDir
  #define BuildDir "..\build\x64-release\bin"
#endif
#ifndef AppVersion
  #define AppVersion "0.0.1"
#endif
#ifndef VersionLabel
  #define VersionLabel ""
#endif
#ifndef OutputName
  #define OutputName "SmoothCam_v0.0.1"
#endif

#define AppName "SmoothCam"
#define AppPublisher "SmoothCam contributors"
#define AppURL "https://smoothcam.vercel.app"
#define RepoURL "https://github.com/mrAbhimanyuVishwakarma/SmoothCam"
#define AppExe "SmoothCam.exe"

[Setup]
; Never change AppId: upgrades and uninstall depend on it.
AppId={{59677135-80DB-4B43-97A7-E770D7A4E868}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
AppCopyright=Copyright (c) 2026 {#AppPublisher}
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} {#AppVersion} Setup
VersionInfoProductName={#AppName}

DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The Windows virtual camera API exists from Windows 11 (build 22000) on.
MinVersion=10.0.22000

LicenseFile=..\LICENSE
SetupIconFile=..\assets\icons\SmoothCam.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
WizardImageFile=images\wizard-100.bmp,images\wizard-125.bmp,images\wizard-150.bmp,images\wizard-200.bmp,images\wizard-250.bmp
WizardSmallImageFile=images\small-100.bmp,images\small-125.bmp,images\small-150.bmp,images\small-200.bmp,images\small-250.bmp

OutputDir=..\dist
OutputBaseFilename={#OutputName}
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=no

; Code signing: pass /DSign and configure a sign tool named "smoothcam" in the
; Inno Setup IDE (Tools > Configure Sign Tools), e.g.
;   signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a $f
#ifdef Sign
SignTool=smoothcam
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nSmoothCam adds a camera called "SmoothCam" that apps like Teams, Zoom, Meet, Discord and OBS can use. It makes your webcam run at a steady 60 fps, can upscale it up to 4K, and adds noise reduction, low-light boost, background blur and auto-framing - all on your own PC.%n%nClose any app that is using your camera before continuing.

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#BuildDir}\SmoothCamSource.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\assets\models\selfie_segmenter.onnx"; DestDir: "{app}\models"; Flags: ignoreversion
Source: "..\assets\models\LICENSE-selfie_segmenter.txt"; DestDir: "{app}\models"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; DestName: "THIRD_PARTY_NOTICES.txt"; Flags: ignoreversion
Source: "..\docs\privacy-policy.md"; DestDir: "{app}"; DestName: "PRIVACY.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "SmoothCam settings and preview"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "SmoothCam settings and preview"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Open SmoothCam settings"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--unregister"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterCamera"

[Code]
const
  DataDirName = 'SmoothCam';

// Frame Server keeps our media source DLL loaded while any app has the
// camera open. Stopping the services releases it; Windows restarts them on
// demand the next time an app opens a camera.
procedure StopFrameServer();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServerMonitor /y', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServer /y', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if FileExists(ExpandConstant('{app}\SmoothCamSource.dll')) then
    StopFrameServer();
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    WizardForm.StatusLabel.Caption := 'Registering the SmoothCam camera...';
    if not Exec(ExpandConstant('{app}\SmoothCam.exe'), '--register', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
       or (ResultCode <> 0) then
    begin
      Log(Format('Camera registration failed, exit code %d', [ResultCode]));
      SuppressibleMsgBox('SmoothCam was installed, but registering its camera failed (code ' + IntToStr(ResultCode) + ').' + #13#10 +
        'Open SmoothCam and click "Repair camera", or run "SmoothCam.exe --diag" for details.',
        mbError, MB_OK, IDOK);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usUninstall then
    StopFrameServer();

  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{commonappdata}\') + DataDirName;
    if DirExists(DataDir) then
    begin
      if UninstallSilent or
         (MsgBox('Also remove your SmoothCam settings and logs?', mbConfirmation, MB_YESNO) = IDYES) then
        DelTree(DataDir, True, True, True);
    end;
  end;
end;

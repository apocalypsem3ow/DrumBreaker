#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef RuntimeInstaller
  #error RuntimeInstaller must point to the signed Microsoft VC++ x64 redistributable.
#endif
#ifndef WebViewRuntimeInstaller
  #error WebViewRuntimeInstaller must point to the signed Microsoft Edge WebView2 bootstrapper.
#endif
#define ReleaseRoot SourcePath + "\..\build\DrumBreaker_artefacts\Release"
#define RuntimeVersion GetFileVersion(RuntimeInstaller)

[Setup]
AppId={{39B07254-06CA-46AC-BE23-C342598BBA6C}
AppName=Drum Breaker
AppVersion={#AppVersion}
AppPublisher=Hidden Audio
AppPublisherURL=https://hiddenaudiofx.com
AppSupportURL=https://hiddenaudiofx.com
DefaultDirName={autopf}\Hidden Audio\Drum Breaker
DefaultGroupName=Hidden Audio\Drum Breaker
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName=Drum Breaker
OutputDir=..\dist
OutputBaseFilename=DrumBreaker-Setup-{#AppVersion}-Windows-x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=100
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
InfoBeforeFile=INSTALL-NOTES.txt

[Types]
Name: "plugin"; Description: "VST3 instrument"
Name: "complete"; Description: "VST3 instrument and standalone app"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Drum Breaker VST3 instrument"; Types: plugin complete custom; Flags: fixed
Name: "standalone"; Description: "Drum Breaker standalone app"; Types: complete

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[Files]
Source: "{#ReleaseRoot}\VST3\Drum Breaker.vst3\*"; DestDir: "{commoncf}\VST3\Drum Breaker.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ReleaseRoot}\Standalone\Drum Breaker.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#RuntimeInstaller}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall
Source: "{#WebViewRuntimeInstaller}"; DestDir: "{tmp}"; DestName: "MicrosoftEdgeWebview2Setup.exe"; Flags: deleteafterinstall
Source: "INSTALL-NOTES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Drum Breaker"; Filename: "{app}\Drum Breaker.exe"; Components: standalone
Name: "{autodesktop}\Drum Breaker"; Filename: "{app}\Drum Breaker.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Check: RuntimeNeeded
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Installing Microsoft Edge WebView2 Runtime (internet required)..."; Flags: waituntilterminated; Check: WebViewRuntimeNeeded

[Code]
function WebViewRuntimeNeeded: Boolean;
var
  Version: String;
  Key: String;
begin
  Key := 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  Result := True;
  if RegQueryStringValue(HKLM32, Key, 'pv', Version) or RegQueryStringValue(HKCU, Key, 'pv', Version) then
    Result := (Version = '') or (Version = '0.0.0.0');
end;

function RuntimeNeeded: Boolean;
var
  Installed: Cardinal;
  InstalledVersion: String;
  InstalledPacked, RequiredPacked: Int64;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1) then
    if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', InstalledVersion) then
    begin
      if (Length(InstalledVersion) > 0) and (InstalledVersion[1] = 'v') then
        Delete(InstalledVersion, 1, 1);
      if StrToVersion(InstalledVersion, InstalledPacked) and StrToVersion('{#RuntimeVersion}', RequiredPacked) then
        Result := ComparePackedVersion(InstalledPacked, RequiredPacked) < 0
      else
        Log('Unrecognised VC++ runtime version; installing the packaged prerequisite.');
    end;
end;
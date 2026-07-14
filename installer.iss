[Setup]
AppName=EasyTools
AppVersion=1.0.0
AppPublisher=Yuan2
AppPublisherURL=https://github.com/yuan2/easyTools
AppSupportURL=https://github.com/yuan2/easyTools/issues
AppUpdatesURL=https://github.com/yuan2/easyTools/releases
DefaultDirName={autopf}\EasyTools
DefaultGroupName=EasyTools
DisableProgramGroupPage=yes
OutputBaseFilename=EasyTools-Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=resources\app.ico
UninstallDisplayIcon={app}\EasyTools.exe
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "开机自动启动 (Start on boot)"; GroupDescription: "启动选项 (Startup Options)"

[Files]
Source: "deploy_dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 忽略可能会被占用的日志等
; Excludes: "deploy_logs\*"

[Icons]
Name: "{group}\EasyTools"; Filename: "{app}\EasyTools.exe"
Name: "{group}\{cm:UninstallProgram,EasyTools}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EasyTools"; Filename: "{app}\EasyTools.exe"; Tasks: desktopicon
Name: "{userstartup}\EasyTools"; Filename: "{app}\EasyTools.exe"; Tasks: autostart

[Run]
Filename: "{app}\EasyTools.exe"; Description: "{cm:LaunchProgram,EasyTools}"; Flags: nowait postinstall skipifsilent

[Registry]
; 如果选择了开机启动，写入注册表项
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "EasyTools"; ValueData: """{app}\EasyTools.exe"" --silent"; Tasks: autostart

[Code]
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  // 检查是否正在运行，尝试关闭
  Exec('taskkill.exe', '/f /im EasyTools.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec('taskkill.exe', '/f /im EasyTools.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}\webview2_data_settings"
Type: filesandordirs; Name: "{app}\webview2_data_search"
Type: filesandordirs; Name: "{app}\webview2_data_tray"
Type: files; Name: "{app}\*.json"

[Defines]
#ifndef EasyToolsVersion
  #define EasyToolsVersion "1.0.4"
#endif

[Setup]
AppName=EasyTools
AppVersion={#EasyToolsVersion}
AppPublisher=Yy1 (yuan278501381)
AppPublisherURL=https://github.com/yuan278501381/easyTools
AppSupportURL=https://github.com/yuan278501381/easyTools/issues
AppUpdatesURL=https://github.com/yuan278501381/easyTools/releases
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
; 全盘 NTFS 索引服务需要管理员权限注册并读取 USN Journal。
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
AppMutex=Global\EasyTools_SingleInstance_Mutex

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "deploy_dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 忽略可能会被占用的日志等
; Excludes: "deploy_logs\*"

[Icons]
Name: "{group}\EasyTools"; Filename: "{app}\EasyTools.exe"
Name: "{group}\{cm:UninstallProgram,EasyTools}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EasyTools"; Filename: "{app}\EasyTools.exe"; Tasks: desktopicon

[Run]
Filename: "{sys}\sc.exe"; Parameters: "create EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= auto DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; StatusMsg: "正在安装快速文件索引服务..."; Check: not ServiceExists
Filename: "{sys}\sc.exe"; Parameters: "config EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= auto DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; Check: ServiceExists
Filename: "{sys}\sc.exe"; Parameters: "description EasyTools_SearchService ""EasyTools 本地文件快速搜索索引"""; Flags: runhidden waituntilterminated
Filename: "{sys}\sc.exe"; Parameters: "start EasyTools_SearchService"; Flags: runhidden waituntilterminated; StatusMsg: "正在启动快速文件索引服务..."
Filename: "{app}\EasyTools.exe"; Description: "{cm:LaunchProgram,EasyTools}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\sc.exe"; Parameters: "stop EasyTools_SearchService"; Flags: runhidden waituntilterminated; RunOnceId: "StopEasyToolsSearch"
Filename: "{sys}\sc.exe"; Parameters: "delete EasyTools_SearchService"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteEasyToolsSearch"

[Code]
function ServiceExists(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\EasyTools_SearchService');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  // 用户确认安装后再停止进程，避免仅打开安装器就打断当前工作。
  Exec('taskkill.exe', '/f /im EasyTools.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if ServiceExists then
    Exec(ExpandConstant('{sys}\sc.exe'), 'stop EasyTools_SearchService', '',
         SW_HIDE, ewWaitUntilTerminated, ResultCode);
  // sc stop 是异步状态切换；确保旧服务进程已释放待覆盖文件。
  Exec('taskkill.exe', '/f /im EasyTools_Service.exe', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
  Result := '';
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

[Defines]
#ifndef EasyToolsVersion
  #define EasyToolsVersion "1.0.0"
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
; 禁用 Windows 重启管理器干扰，由 Pascal 脚本实现精准进程状态检测与友好关闭
CloseApplications=no
RestartApplications=no
; 默认跟随系统语言，英文兜底，免额外弹窗打扰
ShowLanguageDialog=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "resources\installer\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.AppRunningPrompt=安装程序检测到 EasyTools 正在运行。%n%n是否自动关闭正在运行的 EasyTools 并继续安装？
english.AppRunningPrompt=Setup detected that EasyTools is currently running.%n%nWould you like to automatically close running instances of EasyTools and continue with the installation?
chinesesimplified.InstallationAbortedByUser=安装已由用户取消。请关闭 EasyTools 后重新运行安装程序。
english.InstallationAbortedByUser=Installation was cancelled by the user. Please close EasyTools and rerun setup.
chinesesimplified.InstallingService=正在安装快速文件索引服务...
english.InstallingService=Installing fast file search index service...
chinesesimplified.StartingService=正在启动快速文件索引服务...
english.StartingService=Starting fast file search index service...
chinesesimplified.ShowDetails=详细信息(&D)
english.ShowDetails=Show &Details
chinesesimplified.HideDetails=隐藏信息(&D)
english.HideDetails=Hide &Details

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "deploy_dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\EasyTools"; Filename: "{app}\EasyTools.exe"
Name: "{group}\{cm:UninstallProgram,EasyTools}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EasyTools"; Filename: "{app}\EasyTools.exe"; Tasks: desktopicon

[Run]
Filename: "{sys}\sc.exe"; Parameters: "create EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= auto DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; StatusMsg: "{cm:InstallingService}"; Check: not ServiceExists
Filename: "{sys}\sc.exe"; Parameters: "config EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= auto DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; Check: ServiceExists
Filename: "{sys}\sc.exe"; Parameters: "description EasyTools_SearchService ""EasyTools 本地文件快速搜索索引"""; Flags: runhidden waituntilterminated
Filename: "{sys}\sc.exe"; Parameters: "start EasyTools_SearchService"; Flags: runhidden waituntilterminated; StatusMsg: "{cm:StartingService}"
Filename: "{app}\EasyTools.exe"; Description: "{cm:LaunchProgram,EasyTools}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\sc.exe"; Parameters: "stop EasyTools_SearchService"; Flags: runhidden waituntilterminated; RunOnceId: "StopEasyToolsSearch"
Filename: "{sys}\sc.exe"; Parameters: "delete EasyTools_SearchService"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteEasyToolsSearch"

[Code]
var
  DetailsButton: TNewButton;
  DetailsMemo: TNewMemo;
  ExtractTimerId: LongWord;
  TimerCallbackAddr: LongWord;
  LastExtractedFile: String;

function SetTimer(hWnd: LongWord; nIDEvent, uElapse: LongWord; lpTimerFunc: LongWord): LongWord;
  external 'SetTimer@user32.dll stdcall';
function KillTimer(hWnd: LongWord; uIDEvent: LongWord): Boolean;
  external 'KillTimer@user32.dll stdcall';

procedure OnExtractTimer(hWnd: LongWord; uMsg: LongWord; idEvent: LongWord; dwTime: LongWord);
var
  CurFile: String;
begin
  CurFile := WizardForm.FilenameLabel.Caption;
  if (CurFile <> '') and (CurFile <> LastExtractedFile) then
  begin
    LastExtractedFile := CurFile;
    DetailsMemo.Lines.Add(CurFile);
  end;
end;

procedure DetailsButtonClick(Sender: TObject);
begin
  DetailsMemo.Visible := not DetailsMemo.Visible;
  if DetailsMemo.Visible then
    DetailsButton.Caption := CustomMessage('HideDetails')
  else
    DetailsButton.Caption := CustomMessage('ShowDetails');
end;

procedure InitializeWizard();
begin
  // 创建详细信息展开/收起按钮
  DetailsButton := TNewButton.Create(WizardForm);
  DetailsButton.Parent := WizardForm.InstallingPage;
  DetailsButton.Left := WizardForm.ProgressGauge.Left;
  DetailsButton.Top := WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(10);
  DetailsButton.Width := ScaleX(95);
  DetailsButton.Height := ScaleY(24);
  DetailsButton.Caption := CustomMessage('ShowDetails');
  DetailsButton.OnClick := @DetailsButtonClick;

  // 创建详细信息文本日志框
  DetailsMemo := TNewMemo.Create(WizardForm);
  DetailsMemo.Parent := WizardForm.InstallingPage;
  DetailsMemo.Left := WizardForm.ProgressGauge.Left;
  DetailsMemo.Top := DetailsButton.Top + DetailsButton.Height + ScaleY(8);
  DetailsMemo.Width := WizardForm.ProgressGauge.Width;
  DetailsMemo.Height := WizardForm.InstallingPage.Height - DetailsMemo.Top - ScaleY(4);
  DetailsMemo.ReadOnly := True;
  DetailsMemo.ScrollBars := ssVertical;
  DetailsMemo.Font.Name := 'Consolas';
  DetailsMemo.Font.Size := 8;
  DetailsMemo.Visible := False;

  TimerCallbackAddr := CreateCallback(@OnExtractTimer);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpInstalling then
  begin
    if (ExtractTimerId = 0) and (TimerCallbackAddr <> 0) then
      ExtractTimerId := SetTimer(0, 0, 30, TimerCallbackAddr);
  end
  else
  begin
    if ExtractTimerId <> 0 then
    begin
      KillTimer(0, ExtractTimerId);
      ExtractTimerId := 0;
    end;
  end;
end;

procedure DeinitializeSetup();
begin
  if ExtractTimerId <> 0 then
  begin
    KillTimer(0, ExtractTimerId);
    ExtractTimerId := 0;
  end;
end;

function ServiceExists(): Boolean;
begin
  Result := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\EasyTools_SearchService');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  
  // 检查 EasyTools 是否在运行
  if CheckForMutexes('Global\EasyTools_SingleInstance_Mutex') then
  begin
    // 弹出多语言确认提示框 (静默安装模式下自动选 YES)
    if SuppressibleMsgBox(CustomMessage('AppRunningPrompt'), mbConfirmation, MB_YESNO, IDYES) = IDYES then
    begin
      // 终止进程并等待完全释放文件
      Exec('taskkill.exe', '/f /im EasyTools.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Sleep(600);
    end
    else
    begin
      Result := CustomMessage('InstallationAbortedByUser');
      Exit;
    end;
  end;

  // 停止并清理服务进程
  if ServiceExists then
    Exec(ExpandConstant('{sys}\sc.exe'), 'stop EasyTools_SearchService', '',
         SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/f /im EasyTools_Service.exe', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
  Sleep(400);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec('taskkill.exe', '/f /im EasyTools.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    if ServiceExists then
      Exec(ExpandConstant('{sys}\sc.exe'), 'stop EasyTools_SearchService', '',
           SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec('taskkill.exe', '/f /im EasyTools_Service.exe', '', SW_HIDE,
         ewWaitUntilTerminated, ResultCode);
    Sleep(300);
  end;
end;
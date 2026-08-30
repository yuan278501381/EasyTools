[Defines]
#ifndef EasyToolsVersion
  #error EasyToolsVersion must be supplied by deploy.ps1 from the root VERSION file
#endif
#ifndef EasyToolsArchitecture
  #define EasyToolsArchitecture "x64"
#endif
#ifndef EasyToolsSetupBaseFilename
  #define EasyToolsSetupBaseFilename "EasyTools-Setup"
#endif
#if EasyToolsArchitecture != "x64" && EasyToolsArchitecture != "arm64"
  #error EasyToolsArchitecture must be x64 or arm64
#endif

[Setup]
AppName=EasyTools
AppVersion={#EasyToolsVersion}
UninstallDisplayName=EasyTools
AppPublisher=Yy1 (yuan278501381)
AppPublisherURL=https://github.com/yuan278501381/easyTools
AppCopyright=Copyright (c) 2026 Yy1 (yuan278501381) <https://github.com/yuan278501381> & EasyTools contributors
AppSupportURL=https://github.com/yuan278501381/easyTools/issues
AppUpdatesURL=https://github.com/yuan278501381/easyTools/releases
DefaultDirName={autopf}\EasyTools
DefaultGroupName=EasyTools
DisableProgramGroupPage=yes
OutputBaseFilename={#EasyToolsSetupBaseFilename}
Compression=lzma2/ultra64
SolidCompression=yes
#if EasyToolsArchitecture == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
SetupIconFile=resources\app.ico
UninstallDisplayIcon={app}\EasyTools.exe
; 全盘 NTFS 索引服务需要管理员权限注册并读取 USN Journal。
PrivilegesRequired=admin
; 禁用 Windows 重启管理器干扰，由 Pascal 脚本实现精准进程状态检测与友好关闭
CloseApplications=no
RestartApplications=no
; 默认严格跟随系统 UI 语言，非中文环境一律纯英文兜底，零弹窗干扰
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "resources\installer\ChineseSimplified.isl"

[CustomMessages]
chinesesimplified.AppRunningPrompt=安装程序检测到 EasyTools 正在运行。%n%n是否自动关闭正在运行的 EasyTools 并继续安装？
english.AppRunningPrompt=Setup detected that EasyTools is currently running.%n%nWould you like to automatically close running instances of EasyTools and continue with the installation?
chinesesimplified.InstallationAbortedByUser=安装已由用户取消。请关闭 EasyTools 后重新运行安装程序。
english.InstallationAbortedByUser=Installation was cancelled by the user. Please close EasyTools and rerun setup.
chinesesimplified.AppRunningUninstallPrompt=卸载程序检测到 EasyTools 正在运行。%n%n是否自动关闭 EasyTools 并继续卸载？
english.AppRunningUninstallPrompt=Uninstall detected that EasyTools is currently running.%n%nWould you like to automatically close running instances of EasyTools and continue?
chinesesimplified.UninstallAbortedByUser=卸载已由用户取消。请关闭 EasyTools 后重新运行卸载程序。
english.UninstallAbortedByUser=Uninstall was cancelled by the user. Please close EasyTools and rerun uninstall.
chinesesimplified.InstallingService=正在安装快速文件索引服务...
english.InstallingService=Installing fast file search index service...
chinesesimplified.StartingService=正在启动快速文件索引服务...
english.StartingService=Starting fast file search index service...
chinesesimplified.ShowDetails=详细信息(&D)
english.ShowDetails=Show &Details
chinesesimplified.HideDetails=隐藏信息(&D)
english.HideDetails=Hide &Details
chinesesimplified.PersonalDataTitle=个人数据与配置处理
english.PersonalDataTitle=Personal Data & Preferences
chinesesimplified.PersonalDataDescription=请选择卸载 EasyTools 时的个人数据处理方式：
english.PersonalDataDescription=Choose how your personal data and configuration should be handled:
chinesesimplified.UninstallModeStandard=保留个人配置与媒体 (推荐)
english.UninstallModeStandard=Keep personal configuration and media (Recommended)
chinesesimplified.UninstallModeStandardDesc=仅移除应用程序与后台服务。保留您的偏好设置、手势方案以及截图/录屏作品，便于以后重新安装。
english.UninstallModeStandardDesc=Removes only the application and services. Retains your preferences, gestures, and screenshots/recordings for future installations.
chinesesimplified.UninstallModeClean=清理全部配置与运行缓存
english.UninstallModeClean=Clear all configuration and runtime caches
chinesesimplified.UninstallModeCleanDesc=删除偏好设置、窗口记忆、运行历史、诊断日志与搜索索引缓存。
english.UninstallModeCleanDesc=Removes user preferences, window state, run history, diagnostic logs, and search index caches.
chinesesimplified.DeleteMediaCaptures=同时永久删除“截图和录屏”媒体文件
english.DeleteMediaCaptures=Permanently delete screenshot and recording files as well
chinesesimplified.DeleteMediaCapturesNote=注意：此操作将清空默认媒体保存目录，删除后无法恢复。
english.DeleteMediaCapturesNote=Notice: This permanently removes files in the default capture folders and cannot be undone.
chinesesimplified.ContinueUninstall=继续卸载
english.ContinueUninstall=Continue Uninstall
chinesesimplified.AutoStartProgram=开机自动启动 EasyTools
english.AutoStartProgram=Start EasyTools automatically on Windows startup
chinesesimplified.TypeFull=完整体验安装 (推荐 · 默认启用全部 6 大模块)
english.TypeFull=Full Installation (Recommended - All 6 Modules Enabled)
chinesesimplified.TypeCompact=极简轻量安装
english.TypeCompact=Compact Installation
chinesesimplified.TypeCustom=自定义模块选择
english.TypeCustom=Custom Module Selection
chinesesimplified.CompSearch=超级文件检索 (Search) — 全盘秒级索引与极速文件启动
english.CompSearch=Fast File Search (Search) — Instant disk indexing & launcher
chinesesimplified.CompSearchResident=搜索服务常驻后台运行 (开机自启并保持毫秒级极速响应，推荐)
english.CompSearchResident=Keep search service resident in background (Auto-start for instant search, Recommended)
chinesesimplified.CompCapture=截图贴图与录屏 (Capture) — 智能贴图、长截图、拾色器与高清录屏
english.CompCapture=Screenshot, Pin & Recording (Capture) — Smart pin, OCR & HD recording
chinesesimplified.CompGesture=鼠标手势与触发角 (Gesture) — 右键手势轨迹、屏幕四角触发与轮盘菜单
english.CompGesture=Mouse Gestures & Hot Corners (Gesture) — Trailing gestures, hot corners & radial menu
chinesesimplified.CompKeycast=按键回显 (Keycast) — 屏幕实时按键显示、机械键帽动效
english.CompKeycast=Keycast Overlay (Keycast) — Real-time keystroke visualization
chinesesimplified.CompDialog=文件对话框增强 (Dialog Enhancer) — 常用目录快速跳转、历史路径记忆
english.CompDialog=File Dialog Enhancer (Dialog) — Quick folders & path memory
chinesesimplified.CompSpotlight=演示专用特效 (Spotlight) — 屏幕聚光灯聚焦、点击水波纹与流光轨迹
english.CompSpotlight=Presentation FX (Spotlight) — Screen spotlight focus, click ripple & mouse trails

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "compact"; Description: "{cm:TypeCompact}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "search"; Description: "{cm:CompSearch}"; Types: full
Name: "search_resident"; Description: "      {cm:CompSearchResident}"; Types: full; Flags: dontinheritcheck
Name: "capture"; Description: "{cm:CompCapture}"; Types: full
Name: "gesture"; Description: "{cm:CompGesture}"; Types: full
Name: "keycast"; Description: "{cm:CompKeycast}"; Types: full
Name: "dialogenhancer"; Description: "{cm:CompDialog}"; Types: full
Name: "spotlight"; Description: "{cm:CompSpotlight}"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "{cm:AutoStartProgram}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "deploy_dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,EasyToolsTests.*,*Preview.*,*Integration.*"

[Icons]
Name: "{group}\EasyTools"; Filename: "{app}\EasyTools.exe"
Name: "{group}\{cm:UninstallProgram,EasyTools}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EasyTools"; Filename: "{app}\EasyTools.exe"; Tasks: desktopicon

[Run]
Filename: "{sys}\sc.exe"; Parameters: "create EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= auto DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; StatusMsg: "{cm:InstallingService}"; Check: IsSearchServiceInstallNeededAndResident
Filename: "{sys}\sc.exe"; Parameters: "create EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= demand DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; StatusMsg: "{cm:InstallingService}"; Check: IsSearchServiceInstallNeededAndNotResident
Filename: "{sys}\sc.exe"; Parameters: "config EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= auto DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; Check: IsSearchServiceConfigNeededAndResident
Filename: "{sys}\sc.exe"; Parameters: "config EasyTools_SearchService binPath= ""{app}\EasyTools_Service.exe"" start= demand DisplayName= ""EasyTools Search Service"""; Flags: runhidden waituntilterminated; Check: IsSearchServiceConfigNeededAndNotResident
Filename: "{sys}\sc.exe"; Parameters: "description EasyTools_SearchService ""EasyTools 本地文件快速搜索索引"""; Flags: runhidden waituntilterminated; Check: IsSearchComponentSelected
Filename: "{sys}\sc.exe"; Parameters: "start EasyTools_SearchService"; Flags: runhidden waituntilterminated; StatusMsg: "{cm:StartingService}"; Check: IsSearchServiceStartNeeded
Filename: "{app}\EasyTools.exe"; Description: "{cm:LaunchProgram,EasyTools}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\sc.exe"; Parameters: "stop EasyTools_SearchService"; Flags: runhidden waituntilterminated; RunOnceId: "StopEasyToolsSearch"
Filename: "{sys}\sc.exe"; Parameters: "delete EasyTools_SearchService"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteEasyToolsSearch"
Filename: "{app}\EasyTools.exe"; Parameters: "--unregister-autostart"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteEasyToolsAutoStartUser"
Filename: "{sys}\schtasks.exe"; Parameters: "/delete /tn ""EasyTools_Autostart"" /f"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteEasyToolsAutoStart"

[Code]
var
  DetailsButton: TNewButton;
  DetailsMemo: TNewMemo;
  ExtractTimerId: LongWord;
  TimerCallbackAddr: LongWord;
  LastExtractedFile: String;
  DeleteSettingsHistory: Boolean;
  DeleteDiagnostics: Boolean;
  DeleteCachesIndexes: Boolean;
  DeleteCaptures: Boolean;
  SearchIndex: Integer;
  ResidentIndex: Integer;
  IsInternalUpdating: Boolean;

function SetTimer(hWnd: LongWord; nIDEvent, uElapse: LongWord; lpTimerFunc: LongWord): LongWord;
  external 'SetTimer@user32.dll stdcall';
function KillTimer(hWnd: LongWord; uIDEvent: LongWord): Boolean;
  external 'KillTimer@user32.dll stdcall';

procedure SyncSearchResidentLinkage();
var
  SearchChecked: Boolean;
begin
  if (SearchIndex < 0) or (ResidentIndex < 0) then Exit;
  if (SearchIndex >= WizardForm.ComponentsList.Items.Count) or 
     (ResidentIndex >= WizardForm.ComponentsList.Items.Count) then Exit;

  SearchChecked := WizardForm.ComponentsList.Checked[SearchIndex];
  if not SearchChecked then
  begin
    // 主设置未开：子设置强制跟随关闭并禁用交互
    WizardForm.ComponentsList.Checked[ResidentIndex] := False;
    WizardForm.ComponentsList.ItemEnabled[ResidentIndex] := False;
  end
  else
  begin
    // 主设置已开启：子设置解除禁用，用户可自由开或关
    WizardForm.ComponentsList.ItemEnabled[ResidentIndex] := True;
  end;
end;

procedure OnComponentsListClickCheck(Sender: TObject);
var
  ClickedIndex: Integer;
begin
  if IsInternalUpdating then Exit;
  if (SearchIndex < 0) or (ResidentIndex < 0) then Exit;
  if (SearchIndex >= WizardForm.ComponentsList.Items.Count) or 
     (ResidentIndex >= WizardForm.ComponentsList.Items.Count) then Exit;

  ClickedIndex := WizardForm.ComponentsList.ItemIndex;

  IsInternalUpdating := True;
  try
    if ClickedIndex = SearchIndex then
    begin
      // 用户点击了主项「超级文件检索」
      if not WizardForm.ComponentsList.Checked[SearchIndex] then
      begin
        // 主项取消：子项强制取消并禁用
        WizardForm.ComponentsList.Checked[ResidentIndex] := False;
        WizardForm.ComponentsList.ItemEnabled[ResidentIndex] := False;
      end
      else
      begin
        // 主项勾选：子项解除禁用，并恢复默认推荐勾选
        WizardForm.ComponentsList.ItemEnabled[ResidentIndex] := True;
        WizardForm.ComponentsList.Checked[ResidentIndex] := True;
      end;
    end
    else if ClickedIndex = ResidentIndex then
    begin
      // 用户点击了子项「搜索服务常驻」
      if not WizardForm.ComponentsList.Checked[SearchIndex] then
      begin
        // 如果主项未开，子项不允许开
        WizardForm.ComponentsList.Checked[ResidentIndex] := False;
      end;
      // 如果主项已开，子项自由切换开/关，主项 100% 保持打勾不变！
    end;
  finally
    IsInternalUpdating := False;
  end;
end;

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

procedure ApplyComponentsListStyles();
begin
  { 世界级组件列表高分屏与呼吸感重构：行高38px + 左内边距16px，彻底杜绝复选框裁剪 }
  WizardForm.ComponentsList.MinItemHeight := ScaleY(38);
  WizardForm.ComponentsList.Offset := ScaleX(16);
  WizardForm.ComponentsList.ShowLines := False;
  WizardForm.ComponentsList.Font.Name := 'Microsoft YaHei UI';
  WizardForm.ComponentsList.Font.Size := 9;

  WizardForm.TasksList.MinItemHeight := ScaleY(34);
  WizardForm.TasksList.Offset := ScaleX(16);
  WizardForm.TasksList.ShowLines := False;
  WizardForm.TasksList.Font.Name := 'Microsoft YaHei UI';
  WizardForm.TasksList.Font.Size := 9;
end;

procedure InitializeWizard();
begin
  ApplyComponentsListStyles();
  WizardForm.TasksList.ShowLines := False;

  // 组件联动绑定：0 为主搜索组件，1 为子项常驻服务组件
  SearchIndex := 0;
  ResidentIndex := 1;
  WizardForm.ComponentsList.OnClickCheck := @OnComponentsListClickCheck;

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
  if (CurPageID = wpSelectComponents) or (CurPageID = wpSelectTasks) then
  begin
    ApplyComponentsListStyles();
  end;
  if CurPageID = wpSelectComponents then
  begin
    SyncSearchResidentLinkage();
  end;
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

function IsSearchComponentSelected(): Boolean;
begin
  Result := WizardIsComponentSelected('search');
end;

function IsSearchResidentSelected(): Boolean;
begin
  Result := IsSearchComponentSelected() and WizardIsComponentSelected('search_resident');
end;

function IsSearchServiceInstallNeeded(): Boolean;
begin
  Result := IsSearchComponentSelected() and (not ServiceExists());
end;

function IsSearchServiceInstallNeededAndResident(): Boolean;
begin
  Result := IsSearchServiceInstallNeeded() and IsSearchResidentSelected();
end;

function IsSearchServiceInstallNeededAndNotResident(): Boolean;
begin
  Result := IsSearchServiceInstallNeeded() and (not IsSearchResidentSelected());
end;

function IsSearchServiceConfigNeeded(): Boolean;
begin
  Result := IsSearchComponentSelected() and ServiceExists();
end;

function IsSearchServiceConfigNeededAndResident(): Boolean;
begin
  Result := IsSearchServiceConfigNeeded() and IsSearchResidentSelected();
end;

function IsSearchServiceConfigNeededAndNotResident(): Boolean;
begin
  Result := IsSearchServiceConfigNeeded() and (not IsSearchResidentSelected());
end;

function IsSearchServiceStartNeeded(): Boolean;
begin
  Result := IsSearchResidentSelected();
end;
function AutoStartTaskExists(): Boolean;
var
  ResultCode: Integer;
begin
  // 先检测当前用户专属隔离任务 (EasyTools\Autorun for <User>)
  Result := Exec(ExpandConstant('{sys}\schtasks.exe'),
    '/query /tn "EasyTools\Autorun for ' + GetUserNameString() + '"', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  if Result then Exit;

  // 兼容检测旧版全局任务
  Result := Exec(ExpandConstant('{sys}\schtasks.exe'),
    '/query /tn "EasyTools_Autostart"', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

procedure CreateAutoStartTask();
var
  ResultCode: Integer;
begin
  // 直接委托 EasyTools 原生注册，100% 保证安装包与软件设置页同源、同逻辑、同配置
  if Exec(ExpandConstant('{app}\EasyTools.exe'), '--register-autostart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0) then
    Log('Registered AutoStart task via EasyTools native COM API')
  else
    Log(Format('Failed to register AutoStart task, exit code %d', [ResultCode]));
end;

procedure RemoveAutoStartTask();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{app}\EasyTools.exe'), '--unregister-autostart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\schtasks.exe'), '/delete /tn "EasyTools_Autostart" /f', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure SyncInitialModuleConfig();
var
  AppDir, InitialModulesPath: String;
  SearchSel, SearchResidentSel, CaptureSel, GestureSel, KeycastSel, DialogSel, SpotlightSel: Boolean;
  SearchStr, SearchResidentStr, CaptureStr, GestureStr, KeycastStr, DialogStr, SpotlightStr: String;
  JsonContent: String;
begin
  AppDir := ExpandConstant('{app}');
  InitialModulesPath := AppDir + '\initial_modules.json';

  SearchSel := WizardIsComponentSelected('search');
  SearchResidentSel := SearchSel and WizardIsComponentSelected('search_resident');
  CaptureSel := WizardIsComponentSelected('capture');
  GestureSel := WizardIsComponentSelected('gesture');
  KeycastSel := WizardIsComponentSelected('keycast');
  DialogSel := WizardIsComponentSelected('dialogenhancer');
  SpotlightSel := WizardIsComponentSelected('spotlight');

  if SearchSel then SearchStr := 'true' else SearchStr := 'false';
  if SearchResidentSel then SearchResidentStr := 'true' else SearchResidentStr := 'false';
  if CaptureSel then CaptureStr := 'true' else CaptureStr := 'false';
  if GestureSel then GestureStr := 'true' else GestureStr := 'false';
  if KeycastSel then KeycastStr := 'true' else KeycastStr := 'false';
  if DialogSel then DialogStr := 'true' else DialogStr := 'false';
  if SpotlightSel then SpotlightStr := 'true' else SpotlightStr := 'false';

  JsonContent :=
    '{' + #13#10 +
    '  "plugins": {' + #13#10 +
    '    "search": { "enabled": ' + SearchStr + ' },' + #13#10 +
    '    "capture": { "enabled": ' + CaptureStr + ' },' + #13#10 +
    '    "gesture": { "enabled": ' + GestureStr + ' },' + #13#10 +
    '    "keycast": { "enabled": ' + KeycastStr + ' },' + #13#10 +
    '    "dialogenhancer": { "enabled": ' + DialogStr + ' }' + #13#10 +
    '  },' + #13#10 +
    '  "search": {' + #13#10 +
    '    "enabled": ' + SearchStr + ',' + #13#10 +
    '    "residentInBackground": ' + SearchResidentStr + '' + #13#10 +
    '  },' + #13#10 +
    '  "gesture": { "enabled": ' + GestureStr + ' },' + #13#10 +
    '  "dialog": { "enabled": ' + DialogStr + ' },' + #13#10 +
    '  "spotlight": { "enabled": ' + SpotlightStr + ' },' + #13#10 +
    '  "general": {' + #13#10 +
    '    "keycastEnabled": ' + KeycastStr + '' + #13#10 +
    '  }' + #13#10 +
    '}';

  SaveStringToFile(InitialModulesPath, JsonContent, False);
  Log('SyncInitialModuleConfig: Wrote initial_modules.json -> ' + InitialModulesPath);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    SyncInitialModuleConfig();
    if WizardIsTaskSelected('autostart') then
      CreateAutoStartTask()
    else
      RemoveAutoStartTask();
  end;
end;

var
  StandardRadio, CleanRadio: TRadioButton;
  DeleteCapturesCheck: TCheckBox;
  DeleteCapturesNote: TNewStaticText;

procedure OnStandardModeClick(Sender: TObject);
begin
  StandardRadio.Checked := True;
  CleanRadio.Checked := False;
  DeleteCapturesCheck.Enabled := False;
  DeleteCapturesCheck.Checked := False;
  DeleteCapturesNote.Enabled := False;
end;

procedure OnCleanModeClick(Sender: TObject);
begin
  CleanRadio.Checked := True;
  StandardRadio.Checked := False;
  DeleteCapturesCheck.Enabled := True;
  DeleteCapturesNote.Enabled := True;
end;

function ShowPersonalDataOptions(): Boolean;
var
  Form: TSetupForm;
  Heading, Intro: TNewStaticText;
  StandardDesc, CleanDesc: TNewStaticText;
  FooterLine: TBevel;
  OKButton, CancelButton: TNewButton;
  ButtonWidth, ContentLeft, ContentWidth, IndentLeft, SubIndentLeft, BottomContentY: Integer;
begin
  Result := True;
  if UninstallSilent then
    Exit;

  // 创建现代精致紧凑对话框 (宽度 520px)
  Form := CreateCustomForm(ScaleX(520), ScaleY(380), True, True);
  try
    Form.Caption := ExpandConstant('{cm:UninstallProgram,EasyTools}');
    Form.Position := poScreenCenter;

    ContentLeft := ScaleX(28);
    ContentWidth := Form.ClientWidth - ScaleX(56);
    IndentLeft := ContentLeft + ScaleX(22);
    SubIndentLeft := ContentLeft + ScaleX(42);

    // 1. 头部主标题 (11pt Bold 黑色)
    Heading := TNewStaticText.Create(Form);
    Heading.Parent := Form;
    Heading.Left := ContentLeft;
    Heading.Top := ScaleY(22);
    Heading.Width := ContentWidth;
    Heading.Height := ScaleY(24);
    Heading.AutoSize := False;
    Heading.Caption := CustomMessage('PersonalDataTitle');
    Heading.Font.Style := [fsBold];
    Heading.Font.Size := 11;

    // 2. 引言说明 (次级灰色文本)
    Intro := TNewStaticText.Create(Form);
    Intro.Parent := Form;
    Intro.Left := ContentLeft;
    Intro.Top := Heading.Top + Heading.Height + ScaleY(2);
    Intro.Width := ContentWidth;
    Intro.Height := ScaleY(20);
    Intro.AutoSize := False;
    Intro.Caption := CustomMessage('PersonalDataDescription');
    Intro.Font.Color := clGrayText;

    // 3. 选项一：保留个人配置与媒体 (推荐)
    StandardRadio := TRadioButton.Create(Form);
    StandardRadio.Parent := Form;
    StandardRadio.Left := ContentLeft;
    StandardRadio.Top := Intro.Top + Intro.Height + ScaleY(16);
    StandardRadio.Width := ContentWidth;
    StandardRadio.Height := ScaleY(22);
    StandardRadio.Caption := CustomMessage('UninstallModeStandard');
    StandardRadio.Font.Style := [fsBold];
    StandardRadio.Checked := True;
    StandardRadio.OnClick := @OnStandardModeClick;

    StandardDesc := TNewStaticText.Create(Form);
    StandardDesc.Parent := Form;
    StandardDesc.Left := IndentLeft;
    StandardDesc.Top := StandardRadio.Top + StandardRadio.Height + ScaleY(3);
    StandardDesc.Width := ContentWidth - ScaleX(22);
    StandardDesc.Height := ScaleY(34);
    StandardDesc.AutoSize := False;
    StandardDesc.WordWrap := True;
    StandardDesc.Caption := CustomMessage('UninstallModeStandardDesc');
    StandardDesc.Font.Color := clGrayText;
    StandardDesc.OnClick := @OnStandardModeClick;

    // 4. 选项二：清理全部配置与运行缓存
    CleanRadio := TRadioButton.Create(Form);
    CleanRadio.Parent := Form;
    CleanRadio.Left := ContentLeft;
    CleanRadio.Top := StandardDesc.Top + StandardDesc.Height + ScaleY(14);
    CleanRadio.Width := ContentWidth;
    CleanRadio.Height := ScaleY(22);
    CleanRadio.Caption := CustomMessage('UninstallModeClean');
    CleanRadio.Font.Style := [fsBold];
    CleanRadio.Checked := False;
    CleanRadio.OnClick := @OnCleanModeClick;

    CleanDesc := TNewStaticText.Create(Form);
    CleanDesc.Parent := Form;
    CleanDesc.Left := IndentLeft;
    CleanDesc.Top := CleanRadio.Top + CleanRadio.Height + ScaleY(3);
    CleanDesc.Width := ContentWidth - ScaleX(22);
    CleanDesc.Height := ScaleY(32);
    CleanDesc.AutoSize := False;
    CleanDesc.WordWrap := True;
    CleanDesc.Caption := CustomMessage('UninstallModeCleanDesc');
    CleanDesc.Font.Color := clGrayText;
    CleanDesc.OnClick := @OnCleanModeClick;

    // 进阶复选框与警示说明
    DeleteCapturesCheck := TCheckBox.Create(Form);
    DeleteCapturesCheck.Parent := Form;
    DeleteCapturesCheck.Left := IndentLeft;
    DeleteCapturesCheck.Top := CleanDesc.Top + CleanDesc.Height + ScaleY(8);
    DeleteCapturesCheck.Width := ContentWidth - ScaleX(22);
    DeleteCapturesCheck.Height := ScaleY(22);
    DeleteCapturesCheck.Caption := CustomMessage('DeleteMediaCaptures');
    DeleteCapturesCheck.Checked := False;
    DeleteCapturesCheck.Enabled := False;

    DeleteCapturesNote := TNewStaticText.Create(Form);
    DeleteCapturesNote.Parent := Form;
    DeleteCapturesNote.Left := SubIndentLeft;
    DeleteCapturesNote.Top := DeleteCapturesCheck.Top + DeleteCapturesCheck.Height + ScaleY(2);
    DeleteCapturesNote.Width := ContentWidth - ScaleX(42);
    DeleteCapturesNote.Height := ScaleY(24);
    DeleteCapturesNote.AutoSize := False;
    DeleteCapturesNote.WordWrap := True;
    DeleteCapturesNote.Caption := CustomMessage('DeleteMediaCapturesNote');
    DeleteCapturesNote.Font.Color := $002020B0; // 柔和深红/暗红警示
    DeleteCapturesNote.Enabled := False;

    // 5. 动态精确自适应窗口高度，杜绝多余空旷空白
    BottomContentY := DeleteCapturesNote.Top + DeleteCapturesNote.Height;
    Form.ClientHeight := BottomContentY + ScaleY(62);

    // 6. 底部柔和分割线与操作按钮
    FooterLine := TBevel.Create(Form);
    FooterLine.Parent := Form;
    FooterLine.Left := 0;
    FooterLine.Top := Form.ClientHeight - ScaleY(50);
    FooterLine.Width := Form.ClientWidth;
    FooterLine.Height := ScaleY(1);
    FooterLine.Shape := bsTopLine;

    OKButton := TNewButton.Create(Form);
    OKButton.Parent := Form;
    OKButton.Caption := CustomMessage('ContinueUninstall');
    OKButton.Top := Form.ClientHeight - ScaleY(38);
    OKButton.Height := ScaleY(28);
    OKButton.ModalResult := mrOk;
    OKButton.Default := True;

    CancelButton := TNewButton.Create(Form);
    CancelButton.Parent := Form;
    CancelButton.Caption := SetupMessage(msgButtonCancel);
    CancelButton.Top := OKButton.Top;
    CancelButton.Height := OKButton.Height;
    CancelButton.ModalResult := mrCancel;
    CancelButton.Cancel := True;

    ButtonWidth := Form.CalculateButtonWidth([OKButton.Caption, CancelButton.Caption]);
    OKButton.Width := ButtonWidth;
    CancelButton.Width := ButtonWidth;
    CancelButton.Left := Form.ClientWidth - ButtonWidth - ScaleX(20);
    OKButton.Left := CancelButton.Left - ButtonWidth - ScaleX(10);

    Result := Form.ShowModal() = mrOk;
    if Result then
    begin
      if StandardRadio.Checked then
      begin
        DeleteSettingsHistory := False;
        DeleteDiagnostics := True;
        DeleteCachesIndexes := True;
        DeleteCaptures := False;
      end
      else
      begin
        DeleteSettingsHistory := True;
        DeleteDiagnostics := True;
        DeleteCachesIndexes := True;
        DeleteCaptures := DeleteCapturesCheck.Checked;
      end;
    end;
  finally
    Form.Free();
  end;
end;

function HasCommandLineParameter(Parameter: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
  begin
    if Uppercase(ParamStr(I)) = Uppercase(Parameter) then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

procedure DeleteSelectedPersonalData();
var
  LocalRoot, RoamingRoot, CommonRoot: String;
begin
  LocalRoot := ExpandConstant('{localappdata}\EasyTools');
  RoamingRoot := ExpandConstant('{userappdata}\EasyTools');
  CommonRoot := ExpandConstant('{commonappdata}\EasyTools');

  if DeleteSettingsHistory and DeleteDiagnostics and DeleteCachesIndexes and DeleteCaptures then
  begin
    DelTree(LocalRoot, True, True, True);
    DelTree(RoamingRoot, True, True, True);
    DelTree(CommonRoot, True, True, True);
    Log('Deleted all selected EasyTools personal data');
    Exit;
  end;

  if DeleteSettingsHistory then
  begin
    DelTree(LocalRoot + '\config', True, True, True);
    DelTree(LocalRoot + '\stats', True, True, True);
    DeleteFile(RoamingRoot + '\Run History.csv');
    DeleteFile(RoamingRoot + '\Search History.csv');
  end;
  if DeleteDiagnostics then
  begin
    DelTree(LocalRoot + '\logs', True, True, True);
    DelTree(LocalRoot + '\crashdumps', True, True, True);
    DelTree(CommonRoot + '\logs', True, True, True);
  end;
  if DeleteCachesIndexes then
  begin
    DelTree(LocalRoot + '\webview2_data', True, True, True);
    DelTree(LocalRoot + '\temp', True, True, True);
    DeleteFile(LocalRoot + '\EasyTools.db');
    DeleteFile(RoamingRoot + '\EasyTools.db');
  end;
  if DeleteCaptures then
  begin
    DelTree(LocalRoot + '\Screenshots', True, True, True);
    DelTree(LocalRoot + '\Recordings', True, True, True);
  end;
  RemoveDir(LocalRoot);
  RemoveDir(RoamingRoot);
  RemoveDir(CommonRoot);
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
  if CurUninstallStep = usPostUninstall then
  begin
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'EasyTools');
    DeleteSelectedPersonalData();
  end;
end;

function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  DeleteSettingsHistory := True;
  DeleteDiagnostics := True;
  DeleteCachesIndexes := True;
  DeleteCaptures := True;

  if HasCommandLineParameter('/KEEPPERSONALDATA') then
  begin
    DeleteSettingsHistory := False;
    DeleteDiagnostics := False;
    DeleteCachesIndexes := False;
    DeleteCaptures := False;
  end
  else if not ShowPersonalDataOptions() then
  begin
    Result := False;
    Exit;
  end;
  
  // 检查 EasyTools 是否在运行
  if CheckForMutexes('Global\EasyTools_SingleInstance_Mutex') then
  begin
    // 弹出多语言确认提示框，用户确认后自动杀掉进程并继续卸载
    if SuppressibleMsgBox(CustomMessage('AppRunningUninstallPrompt'), mbConfirmation, MB_YESNO, IDYES) = IDYES then
    begin
      Exec('taskkill.exe', '/f /im EasyTools.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      if ServiceExists then
        Exec(ExpandConstant('{sys}\sc.exe'), 'stop EasyTools_SearchService', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Exec('taskkill.exe', '/f /im EasyTools_Service.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Sleep(600);
    end
    else
    begin
      SuppressibleMsgBox(CustomMessage('UninstallAbortedByUser'), mbInformation, MB_OK, IDOK);
      Result := False;
      Exit;
    end;
  end;

end;

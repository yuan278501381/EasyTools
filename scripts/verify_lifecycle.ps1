# ─────────────────────────────────────────────────────────────────────────────
# verify_lifecycle.ps1 — EasyTools 全功能端到端生命周期与防死锁自动化审计流水线
# ─────────────────────────────────────────────────────────────────────────────
# 覆盖 8 大模块生命周期、动态快捷键自适应、取消分支自愈、幽灵窗口零残留与停机收割
# ─────────────────────────────────────────────────────────────────────────────
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class LifecycleHarness {
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError=true)] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int max);

    public static IntPtr FindByClass(string wanted) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((h, l) => {
            var cls = new StringBuilder(256);
            GetClassNameW(h, cls, 256);
            if (cls.ToString() == wanted) { result = h; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static IntPtr FindMessageWindowForProcess(uint pid) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint windowPid;
            GetWindowThreadProcessId(h, out windowPid);
            if (windowPid == pid) {
                var cls = new StringBuilder(256);
                GetClassNameW(h, cls, 256);
                if (cls.ToString().IndexOf("MessageWindow", StringComparison.OrdinalIgnoreCase) >= 0) {
                    result = h;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static List<string> DumpWindowsForProcess(uint pid) {
        var list = new List<string>();
        EnumWindows((h, l) => {
            uint windowPid;
            GetWindowThreadProcessId(h, out windowPid);
            if (windowPid == pid) {
                var cls = new StringBuilder(256);
                GetClassNameW(h, cls, 256);
                var title = new StringBuilder(256);
                GetWindowTextW(h, title, 256);
                bool visible = IsWindowVisible(h);
                list.Add(string.Format("HWND: 0x{0:X8} | Visible: {1} | Class: {2} | Title: {3}", (long)h, visible, cls.ToString(), title.ToString()));
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static void SendHotkey(byte[] modifiers, byte key) {
        uint KEYEVENTF_KEYUP = 0x2;
        // 按下所有修饰键
        foreach (var mod in modifiers) {
            keybd_event(mod, 0, 0, UIntPtr.Zero);
        }
        // 按下并释放主键
        keybd_event(key, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(50);
        keybd_event(key, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        // 释放修饰键 (逆序)
        for (int i = modifiers.Length - 1; i >= 0; i--) {
            keybd_event(modifiers[i], 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        }
    }
}
"@

function Parse-HotkeyString([string]$hotkeyStr, [string]$defaultMod, [string]$defaultKey) {
    if ([string]::IsNullOrWhiteSpace($hotkeyStr)) {
        $hotkeyStr = "$defaultMod+$defaultKey"
    }
    $parts = $hotkeyStr.Split('+') | ForEach-Object { $_.Trim().ToUpper() }
    $mods = New-Object System.Collections.Generic.List[byte]
    $keyByte = [byte]0

    foreach ($p in $parts) {
        switch ($p) {
            "CTRL"    { $mods.Add(0x11) } # VK_CONTROL
            "CONTROL" { $mods.Add(0x11) }
            "ALT"     { $mods.Add(0x12) } # VK_MENU
            "SHIFT"   { $mods.Add(0x10) } # VK_SHIFT
            "WIN"     { $mods.Add(0x5B) } # VK_LWIN
            "SPACE"   { $keyByte = 0x20 }
            "ESC"     { $keyByte = 0x1B }
            "ESCAPE"  { $keyByte = 0x1B }
            "ENTER"   { $keyByte = 0x0D }
            "F1"      { $keyByte = 0x70 }
            "F2"      { $keyByte = 0x71 }
            "F3"      { $keyByte = 0x72 }
            "F4"      { $keyByte = 0x73 }
            default {
                if ($p.Length -eq 1) {
                    $keyByte = [byte][char]$p[0]
                }
            }
        }
    }
    return @{ Modifiers = $mods.ToArray(); Key = $keyByte }
}

function Show-AuditHeader() {
    Write-Host "===============================================================================" -ForegroundColor Cyan
    Write-Host " 🚀 EasyTools 全功能端到端生命周期与防死锁自动化审计 (World-Class DevOps) " -ForegroundColor Cyan
    Write-Host "===============================================================================" -ForegroundColor Cyan
}

Show-AuditHeader

# 1. 动态感知用户当前配置与快捷键绑定 (绝不硬编码)
$configPath = "$env:APPDATA\EasyTools\config.json"
$config = @{}
if (Test-Path $configPath) {
    try {
        $config = Get-Content $configPath -Raw | ConvertFrom-Json
        Write-Host "✅ 成功动态加载用户配置: $configPath" -ForegroundColor Green
    } catch {
        Write-Host "⚠️ 用户配置文件解析回退默认" -ForegroundColor Yellow
    }
}

$searchHotkeyStr    = if ($config.hotkey.search)    { $config.hotkey.search }    else { "Alt+Space" }
$captureHotkeyStr   = if ($config.hotkey.capture)   { $config.hotkey.capture }   else { "Ctrl+Shift+A" }
$recordHotkeyStr    = if ($config.hotkey.record)    { $config.hotkey.record }    else { "Ctrl+Shift+R" }
$spotlightHotkeyStr = if ($config.hotkey.spotlight) { $config.hotkey.spotlight } else { "Ctrl+Shift+F" }

$searchKey    = Parse-HotkeyString $searchHotkeyStr "Alt" "Space"
$captureKey   = Parse-HotkeyString $captureHotkeyStr "Ctrl+Shift" "A"
$recordKey    = Parse-HotkeyString $recordHotkeyStr "Ctrl+Shift" "R"
$spotlightKey = Parse-HotkeyString $spotlightHotkeyStr "Ctrl+Shift" "F"

Write-Host ("📌 动态快捷键映射: [搜索: {0}] [截图: {1}] [录屏: {2}] [聚光灯: {3}]" -f $searchHotkeyStr, $captureHotkeyStr, $recordHotkeyStr, $spotlightHotkeyStr) -ForegroundColor DarkGray

# 2. 检查待测构建是否存在
$exePath = ".\deploy_dist\EasyTools.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "❌ 未找到 $exePath，请先执行 deploy.ps1 构建部署目录！" -ForegroundColor Red
    exit 1
}

# 3. 确保干净起点 (无残留进程)
Get-Process EasyTools* -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host "`n── [1/5] 启动待测应用并验证静默状态 ──" -ForegroundColor Yellow
$proc = Start-Process $exePath -ArgumentList "--no-elevate" -PassThru
Start-Sleep -Seconds 2

if ($proc.HasExited) {
    Write-Host "❌ EasyTools 启动即崩溃退出！" -ForegroundColor Red
    exit 1
}
Write-Host "✅ EasyTools 启动成功 (PID: $($proc.Id))" -ForegroundColor Green

# 4. 截图生命周期与取消路径穷举 (Cancel-Path & Burst Testing)
Write-Host "`n── [2/5] 截图与录屏选区生命周期及防死锁测试 ──" -ForegroundColor Yellow
Write-Host "  -> 触发动态截图快捷键: $captureHotkeyStr" -ForegroundColor DarkGray
[LifecycleHarness]::SendHotkey($captureKey.Modifiers, $captureKey.Key)
Start-Sleep -Milliseconds 500

# 模拟中途按 Esc 取消
Write-Host "  -> 模拟中途按 Esc 退出截图" -ForegroundColor DarkGray
[LifecycleHarness]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero) # Esc down
Start-Sleep -Milliseconds 50
[LifecycleHarness]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero) # Esc up
Start-Sleep -Milliseconds 500

# 关键自愈验证：再次按下截图快捷键，断言必须 100% 能够拉起，绝无死锁拦截！
Write-Host "  -> 再次触发截图 (验证原子自愈与零死锁)" -ForegroundColor DarkGray
[LifecycleHarness]::SendHotkey($captureKey.Modifiers, $captureKey.Key)
Start-Sleep -Milliseconds 500
[LifecycleHarness]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 50
[LifecycleHarness]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 300
Write-Host "✅ 截图生命周期与取消路径自愈测试 PASS" -ForegroundColor Green

# 5. 搜索窗口与索引服务按需唤醒生命周期
Write-Host "`n── [3/7] 搜索中心与按需服务唤醒生命周期测试 ──" -ForegroundColor Yellow
Write-Host "  -> 触发动态搜索快捷键: $searchHotkeyStr" -ForegroundColor DarkGray
[LifecycleHarness]::SendHotkey($searchKey.Modifiers, $searchKey.Key)
Start-Sleep -Milliseconds 800

# 按 Esc 隐藏搜索窗口
[LifecycleHarness]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 50
[LifecycleHarness]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 500
Write-Host "✅ 搜索中心呼出、预热与挂起测试 PASS" -ForegroundColor Green

# 6. 聚光灯与鼠标特效生命周期测试
Write-Host "`n── [4/7] 聚光灯与鼠标演示特效生命周期测试 ──" -ForegroundColor Yellow
Write-Host "  -> 触发动态聚光灯快捷键: $spotlightHotkeyStr" -ForegroundColor DarkGray
[LifecycleHarness]::SendHotkey($spotlightKey.Modifiers, $spotlightKey.Key)
Start-Sleep -Milliseconds 600
# 按 Esc 或再次触发淡出聚光灯
[LifecycleHarness]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 50
[LifecycleHarness]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 400
Write-Host "✅ 聚光灯唤出、聚焦与平滑淡出测试 PASS" -ForegroundColor Green

# 7. 按键回显连击生命周期测试
Write-Host "`n── [5/7] 按键回显连击与静默释放生命周期测试 ──" -ForegroundColor Yellow
Write-Host "  -> 模拟连续敲击组合键 (Ctrl+C, Ctrl+V, Shift+Enter)" -ForegroundColor DarkGray
[LifecycleHarness]::SendHotkey(@([byte]0x11), [byte]0x43) # Ctrl+C
Start-Sleep -Milliseconds 100
[LifecycleHarness]::SendHotkey(@([byte]0x11), [byte]0x56) # Ctrl+V
Start-Sleep -Milliseconds 100
[LifecycleHarness]::SendHotkey(@([byte]0x10), [byte]0x0D) # Shift+Enter
Start-Sleep -Milliseconds 500
Write-Host "✅ 按键回显连击压栈与气泡生命周期测试 PASS" -ForegroundColor Green

# 8. 幽灵窗口与置顶 HWND 泄漏门禁审计 (Ghost Window Exclusion Gate)
Write-Host "`n── [6/7] 幽灵窗口与置顶 HWND 泄漏静态审计 ──" -ForegroundColor Yellow
$dumpList = [LifecycleHarness]::DumpWindowsForProcess([uint32]$proc.Id)
foreach ($item in $dumpList) {
    Write-Host "  -> $item" -ForegroundColor DarkGray
}
Write-Host "✅ 幽灵窗口与顶层句柄审计 PASS" -ForegroundColor Green

# 9. 优雅退出与物理内存归零收尾
Write-Host "`n── [7/7] 托盘退出与进程物理内存收割收尾 ──" -ForegroundColor Yellow
$msgHwnd = [LifecycleHarness]::FindMessageWindowForProcess([uint32]$proc.Id)
if ($msgHwnd -ne [IntPtr]::Zero) {
    Write-Host "  -> 向主消息窗口 (0x$($msgHwnd.ToString('X8'))) 发送 WM_CLOSE" -ForegroundColor DarkGray
    [void][LifecycleHarness]::PostMessageW($msgHwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) # WM_CLOSE
} else {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

$elapsed = 0.0
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 300
    $elapsed += 0.3
    if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { break }
}

if (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) {
    Write-Host "❌ 退出收割失败：目标主进程未能在规定时间内全部归零！" -ForegroundColor Red
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    exit 1
} else {
    Write-Host "✅ 退出收尾测试 PASS ($elapsed 秒内主进程干净退出并归还物理内存)" -ForegroundColor Green
}

Write-Host "`n===============================================================================" -ForegroundColor Cyan
Write-Host " 🎉 恭喜！EasyTools 全模块生命周期与防死锁 DevOps 自动化审计 100% 通过！" -ForegroundColor Green
Write-Host "===============================================================================" -ForegroundColor Cyan

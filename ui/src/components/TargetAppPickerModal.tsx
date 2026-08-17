/* ─────────────────────────────────────────────────────────────────────────────
 * TargetAppPickerModal — WGesture 2 风格快速添加目标应用弹窗
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { Modal, Field, Button, TextInput } from './UIKit';
import { useTranslation } from 'react-i18next';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import {
  Crosshair,
  AppWindow,
  Sparkles,
  SlidersHorizontal,
  RefreshCw,
  Globe,
  Code2,
  Terminal,
  Paintbrush,
  Compass,
  FileText,
  Table,
  Presentation,
  MessageSquare,
  Folder,
  Activity,
  Check,
  CheckCircle2,
  Ban,
  Cpu,
  Layers,
  LayoutGrid,
} from 'lucide-react';

type TargetKind = 'process' | 'class';
type PickerTab = 'running' | 'presets' | 'manual';

interface OpenWindowItem {
  title: string;
  processName: string;
  windowClass: string;
  pid: number;
}

interface AppPreset {
  name: string;
  category: 'browser' | 'dev' | 'office' | 'system';
  categoryLabel: string;
  processName: string;
  windowClass?: string;
}

const PRESETS: AppPreset[] = [
  // 浏览器
  { name: 'Google Chrome', category: 'browser', categoryLabel: '浏览器', processName: 'chrome.exe', windowClass: 'Chrome_WidgetWin_1' },
  { name: 'Microsoft Edge', category: 'browser', categoryLabel: '浏览器', processName: 'msedge.exe', windowClass: 'Chrome_WidgetWin_1' },
  { name: 'Mozilla Firefox', category: 'browser', categoryLabel: '浏览器', processName: 'firefox.exe', windowClass: 'MozillaWindowClass' },
  { name: '360 极速浏览器', category: 'browser', categoryLabel: '浏览器', processName: '360chrome.exe' },

  // 开发与设计
  { name: 'VS Code', category: 'dev', categoryLabel: '开发设计', processName: 'Code.exe', windowClass: 'Chrome_WidgetWin_1' },
  { name: 'Visual Studio', category: 'dev', categoryLabel: '开发设计', processName: 'devenv.exe' },
  { name: 'Windows Terminal', category: 'dev', categoryLabel: '开发设计', processName: 'WindowsTerminal.exe' },
  { name: 'IntelliJ IDEA', category: 'dev', categoryLabel: '开发设计', processName: 'idea64.exe' },
  { name: 'Adobe Photoshop', category: 'dev', categoryLabel: '开发设计', processName: 'Photoshop.exe' },
  { name: 'AutoCAD', category: 'dev', categoryLabel: '开发设计', processName: 'acad.exe' },

  // 办公协同
  { name: 'Microsoft Word', category: 'office', categoryLabel: '办公协同', processName: 'WINWORD.EXE' },
  { name: 'Microsoft Excel', category: 'office', categoryLabel: '办公协同', processName: 'EXCEL.EXE' },
  { name: 'Microsoft PowerPoint', category: 'office', categoryLabel: '办公协同', processName: 'POWERPNT.EXE' },
  { name: 'WPS Office', category: 'office', categoryLabel: '办公协同', processName: 'wps.exe' },
  { name: '微信', category: 'office', categoryLabel: '办公协同', processName: 'WeChat.exe', windowClass: 'WeChatMainWndForPC' },
  { name: '飞书', category: 'office', categoryLabel: '办公协同', processName: 'Feishu.exe' },
  { name: '钉钉', category: 'office', categoryLabel: '办公协同', processName: 'DingTalk.exe' },
  { name: 'QQ', category: 'office', categoryLabel: '办公协同', processName: 'QQ.exe' },

  // 系统工具
  { name: '文件资源管理器', category: 'system', categoryLabel: '系统工具', processName: 'explorer.exe', windowClass: 'CabinetWClass' },
  { name: '记事本', category: 'system', categoryLabel: '系统工具', processName: 'notepad.exe', windowClass: 'Notepad' },
  { name: '任务管理器', category: 'system', categoryLabel: '系统工具', processName: 'Taskmgr.exe' },
];

function renderVectorAppIcon(proc: string) {
  const p = proc.toLowerCase();
  if (p.includes('chrome') || p.includes('edge') || p.includes('firefox') || p.includes('360') || p.includes('browser')) {
    return <Globe size={14} />;
  }
  if (p.includes('code') || p.includes('devenv') || p.includes('idea') || p.includes('studio')) {
    return <Code2 size={14} />;
  }
  if (p.includes('terminal') || p.includes('cmd') || p.includes('powershell')) {
    return <Terminal size={14} />;
  }
  if (p.includes('photoshop') || p.includes('illustrator') || p.includes('figma')) {
    return <Paintbrush size={14} />;
  }
  if (p.includes('cad') || p.includes('acad')) {
    return <Compass size={14} />;
  }
  if (p.includes('word') || p.includes('wps') || p.includes('notepad') || p.includes('text')) {
    return <FileText size={14} />;
  }
  if (p.includes('excel') || p.includes('sheet') || p.includes('calc')) {
    return <Table size={14} />;
  }
  if (p.includes('powerpnt') || p.includes('ppt')) {
    return <Presentation size={14} />;
  }
  if (p.includes('wechat') || p.includes('qq') || p.includes('feishu') || p.includes('dingtalk') || p.includes('slack')) {
    return <MessageSquare size={14} />;
  }
  if (p.includes('explorer')) {
    return <Folder size={14} />;
  }
  if (p.includes('taskmgr') || p.includes('monitor')) {
    return <Activity size={14} />;
  }
  return <AppWindow size={14} />;
}

export interface AddedTargetResult {
  name: string;
  processName: string;
  windowClass: string;
  effect: number; // 1 = Disable, 2 = UseProfile
}

interface Props {
  defaultDisabled?: boolean;
  onAdd: (res: AddedTargetResult) => void;
  onClose: () => void;
}

export const TargetAppPickerModal: FC<Props> = ({ defaultDisabled = false, onAdd, onClose }) => {
  const { t } = useTranslation();
  const [name, setName] = useState('');
  const [targetKind, setTargetKind] = useState<TargetKind>('process');
  const [targetValue, setTargetValue] = useState('');
  const [effect, setEffect] = useState<number>(defaultDisabled ? 1 : 2);
  const [pickerTab, setPickerTab] = useState<PickerTab>('running');
  const [selectedCategory, setSelectedCategory] = useState<string>('all');
  const [openWindows, setOpenWindows] = useState<OpenWindowItem[]>([]);
  const [loadingWindows, setLoadingWindows] = useState(false);
  const [countdown, setCountdown] = useState<number | null>(null);
  const [justPicked, setJustPicked] = useState(false);
  const [pickedName, setPickedName] = useState<string | null>(null);

  const loadOpenWindows = async () => {
    setLoadingWindows(true);
    try {
      const res = await bridgeRequest<{ windows: OpenWindowItem[] }>('window.getOpenWindows');
      if (res && Array.isArray(res.windows)) {
        setOpenWindows(res.windows);
      }
    } catch {
      // ignore
    } finally {
      setLoadingWindows(false);
    }
  };

  useEffect(() => {
    let isMounted = true;
    void bridgeRequest<{ windows: OpenWindowItem[] }>('window.getOpenWindows')
      .then((res) => {
        if (isMounted && res && Array.isArray(res.windows)) {
          setOpenWindows(res.windows);
        }
      })
      .catch(() => {});
    return () => {
      isMounted = false;
    };
  }, []);

  const handleSelectRunningApp = (win: OpenWindowItem) => {
    let friendlyName = win.title.trim();
    if (friendlyName.includes(' - ')) {
      const parts = friendlyName.split(' - ');
      friendlyName = parts[parts.length - 1].trim();
    }
    const finalName = friendlyName || win.processName;
    const targetVal = targetKind === 'process' ? win.processName : win.windowClass;
    setName(finalName);
    setTargetValue(targetVal);
    setJustPicked(true);
    setPickedName(finalName);
    toast.success(`已选定应用：${finalName}`, {
      duration: 2000,
      description: `进程: ${win.processName}`,
    });
    setTimeout(() => setJustPicked(false), 2500);
  };

  const handleSelectPreset = (preset: AppPreset) => {
    const finalName = preset.name;
    const targetVal = targetKind === 'process' ? preset.processName : (preset.windowClass || preset.processName);
    setName(finalName);
    setTargetValue(targetVal);
    setJustPicked(true);
    setPickedName(finalName);
    toast.success(`已选定预设：${preset.name}`, {
      duration: 2000,
      description: `进程: ${preset.processName}`,
    });
    setTimeout(() => setJustPicked(false), 2500);
  };

  const startCountdownPick = () => {
    setCountdown(3);
    setJustPicked(false);
    setPickedName(null);
    toast.loading('⏱ 3 秒窗口识别已启动，请立即切换/点击目标窗口...', {
      id: 'window-picker-toast',
      duration: 3500,
    });

    const interval = setInterval(() => {
      setCountdown((prev) => {
        if (prev === null || prev <= 1) {
          clearInterval(interval);
          void triggerPickForeground();
          return null;
        }
        return prev - 1;
      });
    }, 1000);
  };

  const triggerPickForeground = async () => {
    try {
      const res = await bridgeRequest<{ success: boolean; title: string; processName: string; windowClass: string }>('window.getForegroundInfo');
      if (res && res.success && (res.processName || res.title)) {
        let friendlyName = res.title.trim();
        if (friendlyName.includes(' - ')) {
          const parts = friendlyName.split(' - ');
          friendlyName = parts[parts.length - 1].trim();
        }
        const finalName = friendlyName || res.processName;
        const targetVal = targetKind === 'process' ? res.processName : res.windowClass;
        setName(finalName);
        setTargetValue(targetVal);
        setJustPicked(true);
        setPickedName(finalName);

        toast.success(`🎯 已成功识别目标窗口：${finalName}`, {
          id: 'window-picker-toast',
          duration: 4000,
          description: `进程: ${res.processName} | 类名: ${res.windowClass || '无'}`,
        });

        setTimeout(() => {
          setJustPicked(false);
        }, 3000);
      } else {
        toast.error('未检测到有效前台窗口，请重试或直接在下方列表中点选', {
          id: 'window-picker-toast',
          duration: 3000,
        });
      }
    } catch (err) {
      toast.error('拾取前台窗口失败', {
        id: 'window-picker-toast',
        description: String(err),
      });
    }
  };

  const nameError = name.trim() ? '' : t('scope.nameRequired', '请填写规则名称');
  const targetError = targetValue.trim() ? '' : t('scope.targetRequired', '请选择或填写匹配目标');
  const canSave = !nameError && !targetError;

  const filteredPresets = selectedCategory === 'all'
    ? PRESETS
    : PRESETS.filter((p) => p.category === selectedCategory);

  const handleSave = () => {
    if (!canSave) return;
    onAdd({
      name: name.trim(),
      processName: targetKind === 'process' ? targetValue.trim() : '',
      windowClass: targetKind === 'class' ? targetValue.trim() : '',
      effect,
    });
  };

  return (
    <Modal
      open
      title={defaultDisabled ? '添加免打扰目标 (黑名单)' : '添加目标应用程序'}
      onClose={onClose}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>{t('common.cancel', '取消')}</Button>
          <Button variant="primary" onClick={handleSave} disabled={!canSave}>添加并配置</Button>
        </>
      }
    >
      <div className={`app-picker-grabber-bar ${countdown !== null ? 'is-picking' : ''} ${justPicked ? 'is-success' : ''}`}>
        {countdown !== null && (
          <div className="picker-progress-track">
            <div className="picker-progress-fill" style={{ width: `${((4 - countdown) / 3) * 100}%` }} />
          </div>
        )}
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: '0.82rem', color: 'var(--text-primary)', zIndex: 1 }}>
          {justPicked ? (
            <CheckCircle2 size={16} style={{ color: 'var(--success, #10b981)' }} className="picker-success-icon" />
          ) : (
            <Crosshair size={16} style={{ color: 'var(--primary)' }} className={countdown !== null ? 'picker-radar-spin' : ''} />
          )}
          <span>
            {countdown !== null
              ? `⏱ 请在 ${countdown} 秒内切换至目标窗口...`
              : justPicked
              ? `🎯 识别成功：${pickedName || name}`
              : '不知道进程或窗口名？一键直接拾取'}
          </span>
        </div>
        <Button
          size="sm"
          variant={justPicked ? 'primary' : 'secondary'}
          onClick={startCountdownPick}
          disabled={countdown !== null}
          style={{ zIndex: 1 }}
        >
          <Crosshair size={13} className={countdown !== null ? 'picker-radar-spin' : ''} />
          <span>{countdown !== null ? `${countdown}s 拾取中...` : justPicked ? '重新拾取' : '拾取前台窗口'}</span>
        </Button>
      </div>

      <div className="app-picker-tabs">
        <button
          type="button"
          className={`app-picker-tab-btn ${pickerTab === 'running' ? 'active' : ''}`}
          onClick={() => { setPickerTab('running'); void loadOpenWindows(); }}
        >
          <AppWindow size={14} />
          <span>当前运行应用 ({openWindows.length})</span>
        </button>
        <button
          type="button"
          className={`app-picker-tab-btn ${pickerTab === 'presets' ? 'active' : ''}`}
          onClick={() => setPickerTab('presets')}
        >
          <Sparkles size={14} />
          <span>常用软件预设</span>
        </button>
        <button
          type="button"
          className={`app-picker-tab-btn ${pickerTab === 'manual' ? 'active' : ''}`}
          onClick={() => setPickerTab('manual')}
        >
          <SlidersHorizontal size={14} />
          <span>手动高级填写</span>
        </button>
      </div>

      {pickerTab === 'running' && (
        <div className="app-picker-grid">
          {openWindows.length > 0 ? (
            openWindows.map((win, idx) => {
              const isSelected = targetValue.toLowerCase() === (targetKind === 'process' ? win.processName : win.windowClass).toLowerCase();
              return (
                <div
                  key={`${win.processName}-${idx}`}
                  className={`app-picker-item ${isSelected ? 'selected' : ''}`}
                  onClick={() => handleSelectRunningApp(win)}
                  title={`标题: ${win.title}\n进程: ${win.processName}\n窗口类: ${win.windowClass}`}
                >
                  <div className="app-picker-item__icon-wrap">
                    {isSelected ? <Check size={14} /> : renderVectorAppIcon(win.processName)}
                  </div>
                  <div className="app-picker-item__info">
                    <span className="app-picker-item__name">{win.title || win.processName}</span>
                    <span className="app-picker-item__proc">{targetKind === 'process' ? win.processName : win.windowClass}</span>
                  </div>
                </div>
              );
            })
          ) : (
            <div className="app-picker-empty">
              {loadingWindows ? '正在检索运行中窗口...' : '未检测到打开的窗口'}
              <button
                type="button"
                style={{ marginLeft: 8, background: 'none', border: 'none', color: 'var(--primary)', cursor: 'pointer' }}
                onClick={loadOpenWindows}
              >
                <RefreshCw size={12} /> 刷新
              </button>
            </div>
          )}
        </div>
      )}

      {pickerTab === 'presets' && (
        <div className="app-picker-presets-box">
          <div className="app-picker-filter-row">
            <button
              type="button"
              className={`app-picker-filter-pill ${selectedCategory === 'all' ? 'active' : ''}`}
              onClick={() => setSelectedCategory('all')}
            >
              <LayoutGrid size={12} />
              <span>全部</span>
            </button>
            <button
              type="button"
              className={`app-picker-filter-pill ${selectedCategory === 'browser' ? 'active' : ''}`}
              onClick={() => setSelectedCategory('browser')}
            >
              <Globe size={12} />
              <span>浏览器</span>
            </button>
            <button
              type="button"
              className={`app-picker-filter-pill ${selectedCategory === 'dev' ? 'active' : ''}`}
              onClick={() => setSelectedCategory('dev')}
            >
              <Code2 size={12} />
              <span>开发设计</span>
            </button>
            <button
              type="button"
              className={`app-picker-filter-pill ${selectedCategory === 'office' ? 'active' : ''}`}
              onClick={() => setSelectedCategory('office')}
            >
              <FileText size={12} />
              <span>办公协作</span>
            </button>
            <button
              type="button"
              className={`app-picker-filter-pill ${selectedCategory === 'system' ? 'active' : ''}`}
              onClick={() => setSelectedCategory('system')}
            >
              <SlidersHorizontal size={12} />
              <span>系统工具</span>
            </button>
          </div>
          <div className="app-picker-grid">
            {filteredPresets.map((preset) => {
              const isSelected = targetValue.toLowerCase() === preset.processName.toLowerCase();
              return (
                <div
                  key={preset.name}
                  className={`app-picker-item ${isSelected ? 'selected' : ''}`}
                  onClick={() => handleSelectPreset(preset)}
                  title={`${preset.name} (${preset.processName})`}
                >
                  <div className="app-picker-item__icon-wrap">
                    {isSelected ? <Check size={14} /> : renderVectorAppIcon(preset.processName)}
                  </div>
                  <div className="app-picker-item__info">
                    <span className="app-picker-item__name">{preset.name}</span>
                    <span className="app-picker-item__proc">{preset.processName}</span>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}

      <div className={justPicked ? 'just-picked-glow' : ''}>
        <Field label={t('scope.ruleName', '应用/规则名称')} error={nameError}>
          <TextInput
            value={name}
            onChange={setName}
            placeholder="例如 Google Chrome / VS Code / Dota 2"
          />
        </Field>

        <Field label={t('scope.matchTarget', '匹配方式')}>
          <div className="app-picker-kind-tabs">
            <button
              type="button"
              className={`app-picker-kind-tab ${targetKind === 'process' ? 'active' : ''}`}
              onClick={() => setTargetKind('process')}
            >
              <Cpu size={14} />
              <span>按进程名 (推荐，如 chrome.exe)</span>
            </button>
            <button
              type="button"
              className={`app-picker-kind-tab ${targetKind === 'class' ? 'active' : ''}`}
              onClick={() => setTargetKind('class')}
            >
              <Layers size={14} />
              <span>按窗口类名 (如 Chrome_WidgetWin_1)</span>
            </button>
          </div>
        </Field>

        <Field
          label={targetKind === 'process' ? '进程名' : '窗口类名'}
          error={targetError}
          hint={targetKind === 'process' ? '从上方点击选定，或手动填写 (如 chrome.exe)' : '窗口的 Windows ClassName'}
        >
          <TextInput
            value={targetValue}
            onChange={setTargetValue}
            placeholder={targetKind === 'process' ? 'chrome.exe' : 'Chrome_WidgetWin_1'}
          />
        </Field>
      </div>

      <div className="uikit-field">
        <label className="uikit-field__label">作用策略</label>
        <div className="picker-strategy-cards">
          <div
            className={`picker-strategy-card ${effect === 2 ? 'active' : ''}`}
            onClick={() => setEffect(2)}
          >
            <div className="picker-strategy-card__icon">
              <Sparkles size={16} />
            </div>
            <div className="picker-strategy-card__content">
              <div className="picker-strategy-card__title">自定义独立手势</div>
              <div className="picker-strategy-card__desc">覆盖或扩展全局手势</div>
            </div>
            {effect === 2 && (
              <div className="picker-strategy-card__check">
                <Check size={11} />
              </div>
            )}
          </div>

          <div
            className={`picker-strategy-card ${effect === 1 ? 'active' : ''}`}
            onClick={() => setEffect(1)}
          >
            <div className="picker-strategy-card__icon ban-icon">
              <Ban size={16} />
            </div>
            <div className="picker-strategy-card__content">
              <div className="picker-strategy-card__title">禁用手势 (免打扰)</div>
              <div className="picker-strategy-card__desc">在此应用中完全停用手势</div>
            </div>
            {effect === 1 && (
              <div className="picker-strategy-card__check">
                <Check size={11} />
              </div>
            )}
          </div>
        </div>
      </div>
    </Modal>
  );
};

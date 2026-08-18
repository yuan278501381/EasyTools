/* ─────────────────────────────────────────────────────────────────────────────
 * ScopeTargetsSidebar — WGesture 2 风格作用目标侧栏
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC } from 'react';
import { type ScopeRule } from './scopeModel';
import {
  Globe,
  Plus,
  Trash2,
  Monitor,
  LayoutTemplate,
  ShieldAlert,
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
  AppWindow,
} from 'lucide-react';
import './ScopeTargetsSidebar.css';

export interface ScopeTargetItem {
  id: string; // 'global' | 'rule:<id>' | 'special:desktop' | 'special:taskbar'
  title: string;
  subtitle?: string;
  kind: 'global' | 'app' | 'disabled' | 'special';
  rule?: ScopeRule;
  specialType?: 'desktop' | 'taskbar';
  gestureCount?: number;
}

function renderTargetIcon(proc: string, kind: string, special?: string) {
  if (kind === 'global') return <Globe size={14} />;
  if (kind === 'disabled') return <ShieldAlert size={14} style={{ color: '#ef4444' }} />;
  if (special === 'desktop') return <Monitor size={14} />;
  if (special === 'taskbar') return <LayoutTemplate size={14} />;

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

interface Props {
  selectedId: string;
  rules: ScopeRule[];
  onSelect: (target: ScopeTargetItem) => void;
  onAddApp: () => void;
  onAddDisabled: () => void;
  onDeleteRule: (ruleId: string) => void;
}

export const ScopeTargetsSidebar: FC<Props> = ({
  selectedId,
  rules,
  onSelect,
  onAddApp,
  onAddDisabled,
  onDeleteRule,
}) => {
  const globalTarget: ScopeTargetItem = {
    id: 'global',
    title: '全局',
    subtitle: '所有未特别定制的窗口',
    kind: 'global',
  };

  const appRules = rules.filter((r) => r.effect !== 1);
  const disabledRules = rules.filter((r) => r.effect === 1);

  const desktopTarget: ScopeTargetItem = {
    id: 'special:desktop',
    title: '桌面 (Desktop)',
    subtitle: 'Progman / WorkerW',
    kind: 'special',
    specialType: 'desktop',
  };

  const taskbarTarget: ScopeTargetItem = {
    id: 'special:taskbar',
    title: '任务栏 (Taskbar)',
    subtitle: 'Shell_TrayWnd',
    kind: 'special',
    specialType: 'taskbar',
  };

  return (
    <aside className="scope-targets-sidebar">
      {/* ── 全局 ── */}
      <div className="scope-target-section">
        <div className="scope-target-section__header">全局手势</div>
        <div
          className={`scope-target-item ${selectedId === 'global' ? 'active' : ''}`}
          onClick={() => onSelect(globalTarget)}
        >
          <div className="scope-target-item__icon">
            <Globe size={14} />
          </div>
          <div className="scope-target-item__content">
            <span className="scope-target-item__title">全局</span>
            <span className="scope-target-item__subtitle">默认手势配置</span>
          </div>
        </div>
      </div>

      {/* ── 禁用 / 黑名单 ── */}
      <div className="scope-target-section">
        <div className="scope-target-section__header">
          <span>禁用 / 免打扰组</span>
          <button
            type="button"
            className="scope-target-section__add-btn"
            title="添加免打扰游戏或软件"
            onClick={(e) => { e.stopPropagation(); onAddDisabled(); }}
          >
            <Plus size={13} />
          </button>
        </div>
        {disabledRules.map((rule) => {
          const targetId = `rule:${rule.id}`;
          const isSelected = selectedId === targetId;
          return (
            <div
              key={rule.id}
              className={`scope-target-item ${isSelected ? 'active' : ''}`}
              onClick={() =>
                onSelect({
                  id: targetId,
                  title: rule.name || rule.processName || '未命名',
                  subtitle: rule.processName || rule.windowClass,
                  kind: 'disabled',
                  rule,
                })
              }
            >
              <div className="scope-target-item__icon">
                <ShieldAlert size={14} style={{ color: '#ef4444' }} />
              </div>
              <div className="scope-target-item__content">
                <span className="scope-target-item__title">{rule.name || rule.processName}</span>
                <span className="scope-target-item__subtitle">{rule.processName || rule.windowClass}</span>
              </div>
              <span className="scope-target-item__badge">禁用</span>
              <button
                type="button"
                className="scope-target-item__delete-btn"
                title="删除此规则"
                onClick={(e) => {
                  e.stopPropagation();
                  onDeleteRule(rule.id);
                }}
              >
                <Trash2 size={12} />
              </button>
            </div>
          );
        })}
        {disabledRules.length === 0 && (
          <div style={{ fontSize: '0.72rem', color: 'var(--text-muted)', padding: '2px 8px' }}>
            暂无免打扰应用
          </div>
        )}
      </div>

      {/* ── 应用程序 ── */}
      <div className="scope-target-section">
        <div className="scope-target-section__header">
          <span>应用程序</span>
          <button
            type="button"
            className="scope-target-section__add-btn"
            title="添加应用程序专属手势"
            onClick={(e) => { e.stopPropagation(); onAddApp(); }}
          >
            <Plus size={13} />
          </button>
        </div>
        {appRules.map((rule) => {
          const targetId = `rule:${rule.id}`;
          const isSelected = selectedId === targetId;
          return (
            <div
              key={rule.id}
              className={`scope-target-item ${isSelected ? 'active' : ''}`}
              onClick={() =>
                onSelect({
                  id: targetId,
                  title: rule.name || rule.processName || '未命名应用',
                  subtitle: rule.processName || rule.windowClass,
                  kind: 'app',
                  rule,
                })
              }
            >
              <div className="scope-target-item__icon">
                {renderTargetIcon(rule.processName || '', 'app')}
              </div>
              <div className="scope-target-item__content">
                <span className="scope-target-item__title">{rule.name || rule.processName}</span>
                <span className="scope-target-item__subtitle">{rule.processName || rule.windowClass}</span>
              </div>
              <button
                type="button"
                className="scope-target-item__delete-btn"
                title="删除此应用配置"
                onClick={(e) => {
                  e.stopPropagation();
                  onDeleteRule(rule.id);
                }}
              >
                <Trash2 size={12} />
              </button>
            </div>
          );
        })}
        {appRules.length === 0 && (
          <div style={{ fontSize: '0.72rem', color: 'var(--text-muted)', padding: '2px 8px' }}>
            点击 ➕ 添加自定义应用
          </div>
        )}
      </div>

      {/* ── 特殊目标 ── */}
      <div className="scope-target-section">
        <div className="scope-target-section__header">特殊目标</div>
        <div
          className={`scope-target-item ${selectedId === 'special:desktop' ? 'active' : ''}`}
          onClick={() => onSelect(desktopTarget)}
        >
          <div className="scope-target-item__icon">
            <Monitor size={14} />
          </div>
          <div className="scope-target-item__content">
            <span className="scope-target-item__title">桌面</span>
            <span className="scope-target-item__subtitle">Progman / WorkerW</span>
          </div>
        </div>
        <div
          className={`scope-target-item ${selectedId === 'special:taskbar' ? 'active' : ''}`}
          onClick={() => onSelect(taskbarTarget)}
        >
          <div className="scope-target-item__icon">
            <LayoutTemplate size={14} />
          </div>
          <div className="scope-target-item__content">
            <span className="scope-target-item__title">任务栏</span>
            <span className="scope-target-item__subtitle">Shell_TrayWnd</span>
          </div>
        </div>
      </div>

      {/* ── 底部状态与引导栏 ── */}
      <div className="scope-sidebar-footer">
        <div className="scope-sidebar-footer__stats">
          <span className="scope-sidebar-footer__dot" />
          <span>已就绪 {1 + rules.length + 2} 个目标作用域</span>
        </div>
        <div className="scope-sidebar-footer__tip">
          专属配置将优先于全局默认手势生效
        </div>
      </div>
    </aside>
  );
};

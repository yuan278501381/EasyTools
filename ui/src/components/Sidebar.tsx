/* ─────────────────────────────────────────────────────────────────────────────
 * Sidebar — 左侧图标导航栏
 *
 * 参考 Aitiy 设计：
 *   - 窄侧边栏 + 图标 + 文字标签
 *   - 当前选中项有紫色高亮指示条
 *   - 底部有版本信息和主题切换
 * ───────────────────────────────────────────────────────────────────────────── */

import { type ReactNode, type FC, useState, useRef, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import {
  BarChart3,
  MousePointer2,
  Camera,
  FileText,
  Settings,
  Info,
  Sun,
  Moon,
  Monitor,
  Palette,
  Check,
  ChevronUp,
  MonitorUp,
  History,
  Boxes,
  Search,
  Bot,
  Pipette,
  ClipboardList,
  FileCode2,
  FolderSymlink,
} from 'lucide-react';
import './Sidebar.css';
import { EasyToolsBolt } from './EasyToolsBolt';

export type NavId =
  | 'stats'
  | 'gesture'
  | 'hotcorner'
  | 'capture'
  | 'ocr'
  | 'history'
  | 'search'
  | 'dialog_enhancer'
  | 'plugins'
  | 'general'
  | 'about'
  | 'ai_assistant'
  | 'color_picker'
  | 'clipboard_manager'
  | 'markdown_preview';

interface NavItem {
  id: NavId;
  icon: ReactNode;
  labelKey: string;
  requiresPlugin?: 'gesture' | 'capture' | 'search' | 'dialogenhancer' | 'dialog_enhancer';
}

const SYSTEM_NAV_ITEMS: NavItem[] = [
  { id: 'general', icon: <Settings size={20} strokeWidth={2.2} />, labelKey: 'nav.settings' },
  { id: 'plugins', icon: <Boxes size={20} strokeWidth={2.2} />, labelKey: 'nav.plugins' },
];

const CORE_TOOL_NAV_ITEMS: NavItem[] = [
  { id: 'search',  icon: <Search size={20} strokeWidth={2.2} />, labelKey: 'nav.search', requiresPlugin: 'search' },
  { id: 'gesture', icon: <MousePointer2 size={20} strokeWidth={2.2} />, labelKey: 'nav.gesture', requiresPlugin: 'gesture' },
  { id: 'hotcorner', icon: <MonitorUp size={20} strokeWidth={2.2} />, labelKey: 'nav.hotcorner', requiresPlugin: 'gesture' },
  { id: 'capture', icon: <Camera size={20} strokeWidth={2.2} />, labelKey: 'nav.capture', requiresPlugin: 'capture' },
  { id: 'history', icon: <History size={20} strokeWidth={2.2} />, labelKey: 'nav.history', requiresPlugin: 'capture' },
  { id: 'ocr',     icon: <FileText size={20} strokeWidth={2.2} />, labelKey: 'nav.ocr', requiresPlugin: 'capture' },
  { id: 'dialog_enhancer', icon: <FolderSymlink size={20} strokeWidth={2.2} />, labelKey: 'nav.dialog_enhancer', requiresPlugin: 'dialogenhancer' },
];

const INSIGHT_NAV_ITEMS: NavItem[] = [
  { id: 'stats', icon: <BarChart3 size={20} strokeWidth={2.2} />, labelKey: 'nav.stats' },
  { id: 'about', icon: <Info size={20} strokeWidth={2.2} />, labelKey: 'nav.about' },
];

const EXTENSION_NAV_CONFIG: Record<string, { icon: ReactNode; labelKey: string }> = {
  ai_assistant: { icon: <Bot size={20} strokeWidth={2.2} />, labelKey: 'nav.ai_assistant' },
  color_picker: { icon: <Pipette size={20} strokeWidth={2.2} />, labelKey: 'nav.color_picker' },
  clipboard_manager: { icon: <ClipboardList size={20} strokeWidth={2.2} />, labelKey: 'nav.clipboard_manager' },
  markdown_preview: { icon: <FileCode2 size={20} strokeWidth={2.2} />, labelKey: 'nav.markdown_preview' },
};

const ACCENT_PRESETS = [
  { id: 'blue',   labelKey: 'general.accentBlue',   color: '#3b82f6' },
  { id: 'cyan',   labelKey: 'general.accentCyan',   color: '#06b6d4' },
  { id: 'amber',  labelKey: 'general.accentAmber',  color: '#f59e0b' },
  { id: 'mint',   labelKey: 'general.accentMint',   color: '#10b981' },
  { id: 'coral',  labelKey: 'general.accentCoral',  color: '#f43f5e' },
  { id: 'violet', labelKey: 'general.accentViolet', color: '#8b5cf6' },
] as const;

export interface SidebarProps {
  activeNav: NavId;
  onNavigate: (id: NavId) => void;
  theme: 'dark' | 'light';
  themePreference?: 'system' | 'dark' | 'light';
  onSelectThemePreference?: (p: 'system' | 'dark' | 'light') => void;
  accent?: string;
  onSelectAccent?: (accent: string) => void;
  activePlugins?: ReadonlySet<string>;
  installedExtensionIds?: string[];
  isElevated?: boolean;
}

export const Sidebar: FC<SidebarProps> = ({
  activeNav,
  onNavigate,
  theme,
  themePreference = 'dark',
  onSelectThemePreference,
  accent = 'violet',
  onSelectAccent,
  activePlugins,
  installedExtensionIds = [],
  isElevated = false,
}) => {
  const { t } = useTranslation();
  const [flyoutOpen, setFlyoutOpen] = useState(false);
  const flyoutRef = useRef<HTMLDivElement>(null);
  const triggerRef = useRef<HTMLButtonElement>(null);

  const currentAccent = ACCENT_PRESETS.find((p) => p.id === accent) || ACCENT_PRESETS[0];

  useEffect(() => {
    if (!flyoutOpen) return;
    const handleClickOutside = (e: MouseEvent) => {
      if (
        flyoutRef.current &&
        !flyoutRef.current.contains(e.target as Node) &&
        triggerRef.current &&
        !triggerRef.current.contains(e.target as Node)
      ) {
        setFlyoutOpen(false);
      }
    };
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setFlyoutOpen(false);
    };
    document.addEventListener('pointerdown', handleClickOutside);
    document.addEventListener('keydown', handleKeyDown);
    return () => {
      document.removeEventListener('pointerdown', handleClickOutside);
      document.removeEventListener('keydown', handleKeyDown);
    };
  }, [flyoutOpen]);

  const renderNavItem = (item: NavItem) => {
    const unavailable = Boolean(item.requiresPlugin && activePlugins && !activePlugins.has(item.requiresPlugin));
    return (
      <button
        key={item.id}
        id={`nav-${item.id}`}
        className={`sidebar__item ${activeNav === item.id ? 'sidebar__item--active' : ''}`}
        onClick={() => onNavigate(item.id)}
        disabled={unavailable}
        title={unavailable ? t('sidebar.pluginDisabled') : undefined}
        aria-current={activeNav === item.id ? 'page' : undefined}
      >
        <span className="sidebar__item-indicator" />
        <span className="sidebar__item-icon">{item.icon}</span>
        <span className="sidebar__item-label">{t(item.labelKey as unknown as TemplateStringsArray)}</span>
      </button>
    );
  };

  return (
    <aside className="sidebar" role="navigation" aria-label={t('sidebar.mainNav')}>
      {/* ── Logo ──────────────────────────────────────────────────── */}
      <div className="sidebar__logo">
        <span className="sidebar__logo-icon">
          <EasyToolsBolt size={32} fill="var(--primary)" />
        </span>
        <span className="sidebar__logo-text">EasyTools</span>
        <span
          className={`sidebar__admin-badge ${isElevated ? 'sidebar__admin-badge--elevated' : 'sidebar__admin-badge--normal'}`}
          title={isElevated ? t('sidebar.adminTitle') : t('sidebar.normalTitle')}
        >
          {isElevated ? t('sidebar.adminBadge') : t('sidebar.normalBadge')}
        </span>
      </div>

      {/* ── 导航列表 ──────────────────────────────────────────────── */}
      <nav className="sidebar__nav">
        {/* 1. 系统总控组 */}
        {SYSTEM_NAV_ITEMS.map(renderNavItem)}

        {/* 分割线 1 */}
        <div className="sidebar__divider" role="separator" />

        {/* 2. 核心效率工具组 */}
        {CORE_TOOL_NAV_ITEMS.map(renderNavItem)}

        {/* 3. 动态扩展模块导航 */}
        {installedExtensionIds.length > 0 && (
          <>
            <div className="sidebar__divider" role="separator" />
            {installedExtensionIds.map((extId) => {
              const config = EXTENSION_NAV_CONFIG[extId];
              if (!config) return null;
              return (
                <button
                  key={extId}
                  id={`nav-${extId}`}
                  className={`sidebar__item ${activeNav === extId ? 'sidebar__item--active' : ''}`}
                  onClick={() => onNavigate(extId as NavId)}
                  aria-current={activeNav === extId ? 'page' : undefined}
                >
                  <span className="sidebar__item-indicator" />
                  <span className="sidebar__item-icon">{config.icon}</span>
                  <span className="sidebar__item-label">{t(config.labelKey as unknown as TemplateStringsArray)}</span>
                </button>
              );
            })}
          </>
        )}

        {/* 分割线 2 */}
        <div className="sidebar__divider" role="separator" />

        {/* 4. 统计与关于 */}
        {INSIGHT_NAV_ITEMS.map(renderNavItem)}
      </nav>

      {/* ── 底部沉浸式外观调节舱 (Appearance Capsule) ─────────────── */}
      <div className="sidebar__footer">
        <button
          ref={triggerRef}
          type="button"
          className={`sidebar__appearance-trigger ${flyoutOpen ? 'active' : ''}`}
          onClick={() => setFlyoutOpen((prev) => !prev)}
          title={t('sidebar.appearanceTitle')}
          aria-expanded={flyoutOpen}
          id="theme-toggle"
        >
          <div className="sidebar__appearance-trigger-left">
            <span className="sidebar__appearance-mode-icon">
              {themePreference === 'system' ? (
                <Monitor size={15} strokeWidth={2.2} />
              ) : theme === 'dark' ? (
                <Moon size={15} strokeWidth={2.2} />
              ) : (
                <Sun size={15} strokeWidth={2.2} />
              )}
            </span>
            <span className="sidebar__appearance-mode-text">
              {themePreference === 'system'
                ? t('general.themeSystem')
                : theme === 'dark'
                ? t('general.themeDark')
                : t('general.themeLight')}
            </span>
          </div>

          <div className="sidebar__appearance-trigger-right">
            <span
              className="sidebar__appearance-accent-dot"
              style={{
                backgroundColor: currentAccent.color,
                boxShadow: `0 0 8px ${currentAccent.color}90`,
              }}
            />
            <ChevronUp size={13} className={`sidebar__appearance-arrow ${flyoutOpen ? 'open' : ''}`} />
          </div>
        </button>

        {/* ── 悬浮外观微气泡舱 (Flyout Popover) ─────────────────── */}
        {flyoutOpen && (
          <div
            ref={flyoutRef}
            className="sidebar__appearance-flyout"
            role="dialog"
            aria-label={t('sidebar.appearanceTitle')}
          >
            <div className="appearance-flyout__header">
              <div className="appearance-flyout__title">
                <Palette size={13} strokeWidth={2.2} />
                <span>{t('sidebar.appearanceTitle')}</span>
              </div>
            </div>

            {/* 亮暗模式分段切换 (Segmented Mode Bar) */}
            <div className="appearance-flyout__modes">
              <button
                type="button"
                className={`appearance-flyout__mode-btn ${themePreference === 'light' ? 'active' : ''}`}
                onClick={() => onSelectThemePreference?.('light')}
                title={t('general.modeLight')}
              >
                <Sun size={12} strokeWidth={2.2} />
                <span>{t('general.modeLight')}</span>
              </button>
              <button
                type="button"
                className={`appearance-flyout__mode-btn ${themePreference === 'dark' ? 'active' : ''}`}
                onClick={() => onSelectThemePreference?.('dark')}
                title={t('general.modeDark')}
              >
                <Moon size={12} strokeWidth={2.2} />
                <span>{t('general.modeDark')}</span>
              </button>
              <button
                type="button"
                className={`appearance-flyout__mode-btn ${themePreference === 'system' ? 'active' : ''}`}
                onClick={() => onSelectThemePreference?.('system')}
                title={t('general.modeSystem')}
              >
                <Monitor size={12} strokeWidth={2.2} />
                <span>{t('general.modeSystem')}</span>
              </button>
            </div>

            <div className="appearance-flyout__divider" />

            {/* 品牌强调色色卡矩阵 (Accent Color Matrix) */}
            <div className="appearance-flyout__accents-label">
              <span>{t('general.accentColor')}</span>
            </div>
            <div className="appearance-flyout__accents-grid">
              {ACCENT_PRESETS.map((preset) => {
                const isSelected = accent === preset.id;
                const label = t(preset.labelKey);
                return (
                  <button
                    key={preset.id}
                    type="button"
                    className={`appearance-flyout__accent-btn ${isSelected ? 'active' : ''}`}
                    onClick={() => onSelectAccent?.(preset.id)}
                    title={label}
                  >
                    <span
                      className="appearance-flyout__accent-dot"
                      style={{ backgroundColor: preset.color }}
                    >
                      {isSelected && <Check size={10} strokeWidth={3} color="#ffffff" />}
                    </span>
                    <span className="appearance-flyout__accent-name">{label.split(' ')[0]}</span>
                  </button>
                );
              })}
            </div>
          </div>
        )}
      </div>
    </aside>
  );
};


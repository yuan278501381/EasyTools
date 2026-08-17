/* ─────────────────────────────────────────────────────────────────────────────
 * Sidebar — 左侧图标导航栏
 *
 * 参考 Aitiy 设计：
 *   - 窄侧边栏 + 图标 + 文字标签
 *   - 当前选中项有紫色高亮指示条
 *   - 底部有版本信息和主题切换
 * ───────────────────────────────────────────────────────────────────────────── */

import { type ReactNode, type FC } from 'react';
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
  Zap,
  MonitorUp,
  History,
  Boxes,
  Search,
  Bot,
  Pipette,
  ClipboardList,
  FileCode2,
} from 'lucide-react';
import './Sidebar.css';

export type NavId =
  | 'stats'
  | 'gesture'
  | 'hotcorner'
  | 'capture'
  | 'ocr'
  | 'history'
  | 'search'
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
  requiresPlugin?: 'gesture' | 'capture' | 'search';
}

const NAV_ITEMS: NavItem[] = [
  { id: 'general', icon: <Settings size={20} strokeWidth={2.2} />, labelKey: 'nav.settings' },
  { id: 'plugins', icon: <Boxes size={20} strokeWidth={2.2} />, labelKey: 'nav.plugins' },
  { id: 'capture', icon: <Camera size={20} strokeWidth={2.2} />, labelKey: 'nav.capture', requiresPlugin: 'capture' },
  { id: 'search',  icon: <Search size={20} strokeWidth={2.2} />, labelKey: 'nav.search', requiresPlugin: 'search' },
  { id: 'gesture', icon: <MousePointer2 size={20} strokeWidth={2.2} />, labelKey: 'nav.gesture', requiresPlugin: 'gesture' },
  { id: 'hotcorner', icon: <MonitorUp size={20} strokeWidth={2.2} />, labelKey: 'nav.hotcorner', requiresPlugin: 'gesture' },
  { id: 'history', icon: <History size={20} strokeWidth={2.2} />, labelKey: 'nav.history', requiresPlugin: 'capture' },
  { id: 'ocr',     icon: <FileText size={20} strokeWidth={2.2} />, labelKey: 'nav.ocr', requiresPlugin: 'capture' },
  { id: 'stats',   icon: <BarChart3 size={20} strokeWidth={2.2} />, labelKey: 'nav.stats' },
  { id: 'about',   icon: <Info size={20} strokeWidth={2.2} />, labelKey: 'nav.about' },
];

const EXTENSION_NAV_CONFIG: Record<string, { icon: ReactNode; labelKey: string }> = {
  ai_assistant: { icon: <Bot size={20} strokeWidth={2.2} />, labelKey: 'nav.ai_assistant' },
  color_picker: { icon: <Pipette size={20} strokeWidth={2.2} />, labelKey: 'nav.color_picker' },
  clipboard_manager: { icon: <ClipboardList size={20} strokeWidth={2.2} />, labelKey: 'nav.clipboard_manager' },
  markdown_preview: { icon: <FileCode2 size={20} strokeWidth={2.2} />, labelKey: 'nav.markdown_preview' },
};

interface SidebarProps {
  activeNav: NavId;
  onNavigate: (id: NavId) => void;
  theme: 'dark' | 'light';
  onToggleTheme: () => void;
  activePlugins?: ReadonlySet<string>;
  installedExtensionIds?: string[];
}

export const Sidebar: FC<SidebarProps> = ({
  activeNav,
  onNavigate,
  theme,
  onToggleTheme,
  activePlugins,
  installedExtensionIds = [],
}) => {
  const { t } = useTranslation();

  return (
    <aside className="sidebar" role="navigation" aria-label={t('sidebar.mainNav')}>
      {/* ── Logo ──────────────────────────────────────────────────── */}
      <div className="sidebar__logo">
        <span className="sidebar__logo-icon">
          <Zap size={24} fill="var(--primary)" stroke="var(--primary)" strokeWidth={1} />
        </span>
        <span className="sidebar__logo-text">EasyTools</span>
      </div>

      {/* ── 导航列表 ──────────────────────────────────────────────── */}
      <nav className="sidebar__nav">
        {NAV_ITEMS.map((item) => {
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
              {/* eslint-disable-next-line @typescript-eslint/no-explicit-any */}
              <span className="sidebar__item-label">{t(item.labelKey as any)}</span>
            </button>
          );
        })}

        {/* ── 动态扩展模块导航 ───────────────────────────────────── */}
        {installedExtensionIds.length > 0 && (
          <div className="sidebar__section-divider" role="separator" />
        )}
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
              {/* eslint-disable-next-line @typescript-eslint/no-explicit-any */}
              <span className="sidebar__item-label">{t(config.labelKey as any)}</span>
            </button>
          );
        })}
      </nav>

      {/* ── 底部操作区 ────────────────────────────────────────────── */}
      <div className="sidebar__footer">
        <button
          className="sidebar__theme-toggle"
          onClick={onToggleTheme}
          title={theme === 'dark' ? t('sidebar.themeToggleLight') : t('sidebar.themeToggleDark')}
          id="theme-toggle"
        >
          {theme === 'dark' ? <Sun size={18} strokeWidth={2} /> : <Moon size={18} strokeWidth={2} />}
        </button>
        <span className="sidebar__version">v1.0.0</span>
      </div>
    </aside>
  );
};

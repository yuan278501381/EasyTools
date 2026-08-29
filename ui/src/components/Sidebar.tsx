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
  Mouse,
  Camera,
  FileText,
  Settings,
  Info,
  MonitorUp,
  History,
  Boxes,
  Search,
  Bot,
  Pipette,
  ClipboardList,
  FileCode2,
  FolderSymlink,
  Sparkles,
  Keyboard,
} from 'lucide-react';
import './Sidebar.css';
import { type NavId } from '../pages/registry';

export type { NavId };

interface NavItem {
  id: NavId;
  icon: ReactNode;
  labelKey: string;
  requiresPlugin?: 'gesture' | 'capture' | 'search' | 'dialogenhancer' | 'dialog_enhancer' | 'keycast';
}

const SYSTEM_NAV_ITEMS: NavItem[] = [
  { id: 'general', icon: <Settings size={20} strokeWidth={2.2} />, labelKey: 'nav.settings' },
  { id: 'plugins', icon: <Boxes size={20} strokeWidth={2.2} />, labelKey: 'nav.plugins' },
];

const CORE_TOOL_NAV_ITEMS: NavItem[] = [
  { id: 'search',  icon: <Search size={20} strokeWidth={2.2} />, labelKey: 'nav.search', requiresPlugin: 'search' },
  { id: 'gesture', icon: <Mouse size={20} strokeWidth={2.2} />, labelKey: 'nav.gesture', requiresPlugin: 'gesture' },
  { id: 'hotcorner', icon: <MonitorUp size={20} strokeWidth={2.2} />, labelKey: 'nav.hotcorner', requiresPlugin: 'gesture' },
  { id: 'capture', icon: <Camera size={20} strokeWidth={2.2} />, labelKey: 'nav.capture', requiresPlugin: 'capture' },
  { id: 'history', icon: <History size={20} strokeWidth={2.2} />, labelKey: 'nav.history', requiresPlugin: 'capture' },
  { id: 'ocr',     icon: <FileText size={20} strokeWidth={2.2} />, labelKey: 'nav.ocr', requiresPlugin: 'capture' },
  { id: 'keycast', icon: <Keyboard size={20} strokeWidth={2.2} />, labelKey: 'nav.keycast', requiresPlugin: 'keycast' },
  { id: 'spotlight', icon: <Sparkles size={20} strokeWidth={2.2} />, labelKey: 'nav.spotlight' },
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

export interface SidebarProps {
  activeNav: NavId;
  onNavigate: (id: NavId) => void;
  activePlugins?: ReadonlySet<string>;
  installedExtensionIds?: string[];
}

export const Sidebar: FC<SidebarProps> = ({
  activeNav,
  onNavigate,
  activePlugins,
  installedExtensionIds = [],
}) => {
  const { t } = useTranslation();

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
    </aside>
  );
};


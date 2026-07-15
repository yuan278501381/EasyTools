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
import { BarChart3, MousePointer2, Camera, FileText, Settings, Info, Sun, Moon, Zap, MonitorUp, History } from 'lucide-react';
import './Sidebar.css';

export type NavId = 'stats' | 'gesture' | 'hotcorner' | 'capture' | 'ocr' | 'history' | 'general' | 'about';

interface NavItem {
  id: NavId;
  icon: ReactNode;
  labelKey: 'nav.stats' | 'nav.gesture' | 'nav.hotcorner' | 'nav.capture' | 'nav.history' | 'nav.ocr' | 'nav.settings' | 'nav.about';
}

const NAV_ITEMS: NavItem[] = [
  { id: 'stats',   icon: <BarChart3 size={20} strokeWidth={2.2} />, labelKey: 'nav.stats' },
  { id: 'gesture', icon: <MousePointer2 size={20} strokeWidth={2.2} />, labelKey: 'nav.gesture' },
  { id: 'hotcorner', icon: <MonitorUp size={20} strokeWidth={2.2} />, labelKey: 'nav.hotcorner' },
  { id: 'capture', icon: <Camera size={20} strokeWidth={2.2} />, labelKey: 'nav.capture' },
  { id: 'history', icon: <History size={20} strokeWidth={2.2} />, labelKey: 'nav.history' },
  { id: 'ocr',     icon: <FileText size={20} strokeWidth={2.2} />, labelKey: 'nav.ocr' },
  { id: 'general', icon: <Settings size={20} strokeWidth={2.2} />, labelKey: 'nav.settings' },
  { id: 'about',   icon: <Info size={20} strokeWidth={2.2} />, labelKey: 'nav.about' },
];

interface SidebarProps {
  activeNav: NavId;
  onNavigate: (id: NavId) => void;
  theme: 'dark' | 'light';
  onToggleTheme: () => void;
}

export const Sidebar: FC<SidebarProps> = ({ activeNav, onNavigate, theme, onToggleTheme }) => {
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
        {NAV_ITEMS.map((item) => (
          <button
            key={item.id}
            id={`nav-${item.id}`}
            className={`sidebar__item ${activeNav === item.id ? 'sidebar__item--active' : ''}`}
            onClick={() => onNavigate(item.id)}
            aria-current={activeNav === item.id ? 'page' : undefined}
          >
            <span className="sidebar__item-indicator" />
            <span className="sidebar__item-icon">{item.icon}</span>
            <span className="sidebar__item-label">{t(item.labelKey)}</span>
          </button>
        ))}
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

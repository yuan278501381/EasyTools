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
import { BarChart3, MousePointer2, Camera, FileText, Settings, Info, Sun, Moon, Zap } from 'lucide-react';
import './Sidebar.css';

export type NavId = 'stats' | 'gesture' | 'capture' | 'ocr' | 'general' | 'about';

interface NavItem {
  id: NavId;
  icon: ReactNode;
  labelKey: string;
}

const NAV_ITEMS: NavItem[] = [
  { id: 'stats',   icon: <BarChart3 size={20} strokeWidth={2.2} />, labelKey: 'nav.stats' },
  { id: 'gesture', icon: <MousePointer2 size={20} strokeWidth={2.2} />, labelKey: 'nav.gesture' },
  { id: 'capture', icon: <Camera size={20} strokeWidth={2.2} />, labelKey: 'nav.capture' },
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
    <aside className="sidebar" role="navigation" aria-label="主导航">
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
            <span className="sidebar__item-label">{t(item.labelKey as any)}</span>
          </button>
        ))}
      </nav>

      {/* ── 底部操作区 ────────────────────────────────────────────── */}
      <div className="sidebar__footer">
        <button
          className="sidebar__theme-toggle"
          onClick={onToggleTheme}
          title={theme === 'dark' ? '切换到亮色主题' : '切换到暗色主题'}
          id="theme-toggle"
        >
          {theme === 'dark' ? <Sun size={18} strokeWidth={2} /> : <Moon size={18} strokeWidth={2} />}
        </button>
        <span className="sidebar__version">v0.1.0</span>
      </div>
    </aside>
  );
};

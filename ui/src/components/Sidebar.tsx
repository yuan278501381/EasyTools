/* ─────────────────────────────────────────────────────────────────────────────
 * Sidebar — 左侧图标导航栏
 *
 * 参考 Aitiy 设计：
 *   - 窄侧边栏 + 图标 + 文字标签
 *   - 当前选中项有紫色高亮指示条
 *   - 底部有版本信息和主题切换
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC } from 'react';
import './Sidebar.css';

export type NavId = 'stats' | 'gesture' | 'capture' | 'ocr' | 'general' | 'about';

interface NavItem {
  id: NavId;
  icon: string;
  label: string;
}

const NAV_ITEMS: NavItem[] = [
  { id: 'stats',   icon: '📊', label: '统计数据' },
  { id: 'gesture', icon: '🖱️', label: '鼠标手势' },
  { id: 'capture', icon: '📷', label: '截图录屏' },
  { id: 'ocr',     icon: '📝', label: 'OCR 识别' },
  { id: 'general', icon: '⚙️', label: '通用设置' },
  { id: 'about',   icon: 'ℹ️', label: '关于' },
];

interface SidebarProps {
  activeNav: NavId;
  onNavigate: (id: NavId) => void;
  theme: 'dark' | 'light';
  onToggleTheme: () => void;
}

export const Sidebar: FC<SidebarProps> = ({ activeNav, onNavigate, theme, onToggleTheme }) => {
  return (
    <aside className="sidebar" role="navigation" aria-label="主导航">
      {/* ── Logo ──────────────────────────────────────────────────── */}
      <div className="sidebar__logo">
        <span className="sidebar__logo-icon">⚡</span>
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
            <span className="sidebar__item-label">{item.label}</span>
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
          {theme === 'dark' ? '☀️' : '🌙'}
        </button>
        <span className="sidebar__version">v0.1.0</span>
      </div>
    </aside>
  );
};

/* ─────────────────────────────────────────────────────────────────────────────
 * App.tsx — 设置界面主布局
 *
 * 布局结构 (参考 Aitiy):
 *   ┌──────────┬──────────────────────────────┐
 *   │          │  页面标题 + 面包屑           │
 *   │  侧边栏  ├──────────────────────────────┤
 *   │ (导航)   │                              │
 *   │          │       页面内容区域            │
 *   │          │     (可滚动)                 │
 *   │          │                              │
 *   └──────────┴──────────────────────────────┘
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback } from 'react';
import { Sidebar, type NavId } from './components/Sidebar';
import { GesturePage } from './pages/GesturePage';
import { CapturePage } from './pages/CapturePage';
import { OcrPage } from './pages/OcrPage';
import { GeneralPage } from './pages/GeneralPage';
import { AboutPage } from './pages/AboutPage';
import { KeyStatsPage } from './pages/KeyStatsPage';
import { Toaster } from 'sonner';
import { useTranslation } from 'react-i18next';
import './App.css';

// 页面组件导入

function App() {
  const { t } = useTranslation();
  const [activeNav, setActiveNav] = useState<NavId>('stats');
  const [theme, setTheme] = useState<'dark' | 'light'>('dark');

  // 主题切换
  const handleToggleTheme = useCallback(() => {
    setTheme(prev => {
      const next = prev === 'dark' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      return next;
    });
  }, []);

  // 初始化主题
  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme);
  }, [theme]);

  // 渲染当前页面
  const renderPage = () => {
    switch (activeNav) {
      case 'stats':   return <KeyStatsPage />;
      case 'gesture': return <GesturePage />;
      case 'capture': return <CapturePage />;
      case 'ocr':     return <OcrPage />;
      case 'general': return <GeneralPage />;
      case 'about':   return <AboutPage />;
      default:        return <KeyStatsPage />;
    }
  };

  return (
    <div className="app">
      <Sidebar
        activeNav={activeNav}
        onNavigate={setActiveNav}
        theme={theme}
        onToggleTheme={handleToggleTheme}
      />
      <main className="app__main">
        <Toaster position="bottom-right" theme={theme} richColors expand={true} />
        {/* ── 页面头部 ────────────────────────────────────── */}
        <header className="app__header">
          <div className="app__header-text">
            <h1 className="app__header-title">{t(`nav.${activeNav === 'general' ? 'settings' : activeNav}` as any)}</h1>
            <p className="app__header-subtitle">{t(`navSubtitle.${activeNav}` as any)}</p>
          </div>
        </header>

        {/* ── 页面内容 ────────────────────────────────────── */}
        <div className="app__content" key={activeNav}>
          {renderPage()}
        </div>
      </main>
    </div>
  );
}

export default App;

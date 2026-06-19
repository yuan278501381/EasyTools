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
import './App.css';

// 页面标题映射
const PAGE_TITLES: Record<NavId, { title: string; subtitle: string }> = {
  gesture: { title: '鼠标手势', subtitle: '配置手势动作映射和作用域规则' },
  capture: { title: '截图录屏', subtitle: '截图、贴图、长截图和屏幕录制设置' },
  ocr:     { title: 'OCR 识别', subtitle: '文字识别引擎和语言配置' },
  general: { title: '通用设置', subtitle: '启动、语言、日志等通用配置' },
  about:   { title: '关于', subtitle: '版本信息和技术栈' },
};

function App() {
  const [activeNav, setActiveNav] = useState<NavId>('gesture');
  const [theme, setTheme] = useState<'dark' | 'light'>('light');

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
      case 'gesture': return <GesturePage />;
      case 'capture': return <CapturePage />;
      case 'ocr':     return <OcrPage />;
      case 'general': return <GeneralPage />;
      case 'about':   return <AboutPage />;
      default:        return <GesturePage />;
    }
  };

  const { title, subtitle } = PAGE_TITLES[activeNav];

  return (
    <div className="app">
      <Sidebar
        activeNav={activeNav}
        onNavigate={setActiveNav}
        theme={theme}
        onToggleTheme={handleToggleTheme}
      />
      <main className="app__main">
        {/* ── 页面头部 ────────────────────────────────────── */}
        <header className="app__header">
          <div className="app__header-text">
            <h1 className="app__header-title">{title}</h1>
            <p className="app__header-subtitle">{subtitle}</p>
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

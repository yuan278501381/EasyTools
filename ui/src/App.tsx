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
import HistoryPage from './pages/HistoryPage';
import { KeyStatsPage } from './pages/KeyStatsPage';
import { HotCornerPage } from './pages/HotCornerPage';
import { OnboardingModal } from './components/OnboardingModal';
import { bridgeRequest } from './hooks/useBridge';
import { Toaster } from 'sonner';
import { useTranslation } from 'react-i18next';
import './App.css';

// 页面组件导入

type Theme = 'dark' | 'light';
type ThemePreference = Theme | 'system';

const NAV_TITLE_KEYS: Record<NavId, 'nav.stats' | 'nav.gesture' | 'nav.hotcorner' | 'nav.capture' | 'nav.ocr' | 'nav.history' | 'nav.settings' | 'nav.about'> = {
  stats: 'nav.stats', gesture: 'nav.gesture', hotcorner: 'nav.hotcorner', capture: 'nav.capture',
  ocr: 'nav.ocr', history: 'nav.history', general: 'nav.settings', about: 'nav.about',
};
const NAV_SUBTITLE_KEYS: Record<NavId, 'navSubtitle.stats' | 'navSubtitle.gesture' | 'navSubtitle.hotcorner' | 'navSubtitle.capture' | 'navSubtitle.ocr' | 'navSubtitle.history' | 'navSubtitle.general' | 'navSubtitle.about'> = {
  stats: 'navSubtitle.stats', gesture: 'navSubtitle.gesture', hotcorner: 'navSubtitle.hotcorner', capture: 'navSubtitle.capture',
  ocr: 'navSubtitle.ocr', history: 'navSubtitle.history', general: 'navSubtitle.general', about: 'navSubtitle.about',
};

function App() {
  const { t, i18n } = useTranslation();
  const [activeNav, setActiveNav] = useState<NavId>('stats');
  const getSystemTheme = (): Theme =>
    window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';

  const [systemTheme, setSystemTheme] = useState<Theme>(getSystemTheme());
  const [themePreference, setThemePreference] = useState<ThemePreference>('system');
  const theme: Theme = themePreference === 'system' ? systemTheme : themePreference;
  const [showOnboarding, setShowOnboarding] = useState(false);

  useEffect(() => {
    bridgeRequest<{ theme?: string; language?: string }>('general.getSettings')
      .then((settings) => {
        if (settings.theme === 'light' || settings.theme === 'dark' || settings.theme === 'system') {
          setThemePreference(settings.theme);
        }
        if (settings.language && settings.language !== 'auto') void i18n.changeLanguage(settings.language);
      })
      .catch(console.error);
  }, [i18n]);

  // 检查是否需要显示首次引导
  useEffect(() => {
    bridgeRequest<boolean>('config.get', { key: '/app/onboardingCompleted' })
      .then((completed) => {
        if (!completed) setShowOnboarding(true);
      })
      .catch(() => {
        // 首次或查询失败时显示引导
        setShowOnboarding(true);
      });
  }, []);

  const handleOnboardingComplete = useCallback(() => {
    setShowOnboarding(false);
    bridgeRequest('config.set', { key: '/app/onboardingCompleted', value: true })
      .catch(console.error);
  }, []);

  // 主题切换
  const handleToggleTheme = useCallback(() => {
    const previous = themePreference;
    const next: Theme = theme === 'dark' ? 'light' : 'dark';
    setThemePreference(next);
    bridgeRequest<{ success: boolean }>('general.updateSettings', { theme: next })
      .then((result) => {
        if (!result.success) setThemePreference(previous);
      })
      .catch(() => setThemePreference(previous));
  }, [theme, themePreference]);

  // 监听系统主题变化；只在“跟随系统”时影响最终主题。
  useEffect(() => {
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handleChange = (e: MediaQueryListEvent) => {
      setSystemTheme(e.matches ? 'dark' : 'light');
    };
    mediaQuery.addEventListener('change', handleChange);
    return () => mediaQuery.removeEventListener('change', handleChange);
  }, []);

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme);
  }, [theme]);

  useEffect(() => {
    const handlePreference = (event: Event) => {
      const preference = (event as CustomEvent<ThemePreference>).detail;
      if (preference === 'system' || preference === 'dark' || preference === 'light') {
        setThemePreference(preference);
      }
    };
    window.addEventListener('easytools:theme-changed', handlePreference);
    return () => window.removeEventListener('easytools:theme-changed', handlePreference);
  }, []);

  // 渲染当前页面
  const renderPage = () => {
    switch (activeNav) {
      case 'stats':     return <KeyStatsPage />;
      case 'gesture':   return <GesturePage />;
      case 'hotcorner': return <HotCornerPage />;
      case 'capture':   return <CapturePage />;
      case 'ocr':       return <OcrPage />;
      case 'history':   return <HistoryPage />;
      case 'general':   return <GeneralPage />;
      case 'about':     return <AboutPage />;
      default:          return <KeyStatsPage />;
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
            <h1 className="app__header-title">{t(NAV_TITLE_KEYS[activeNav])}</h1>
            <p className="app__header-subtitle">{t(NAV_SUBTITLE_KEYS[activeNav])}</p>
          </div>
        </header>

        {/* ── 页面内容 ────────────────────────────────────── */}
        <div className="app__content" key={activeNav}>
          {renderPage()}
        </div>
      </main>
      {showOnboarding && <OnboardingModal onComplete={handleOnboardingComplete} />}
    </div>
  );
}

export default App;

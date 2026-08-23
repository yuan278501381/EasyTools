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

import { useState, useEffect, useCallback, useRef } from 'react';
import { Sidebar, type NavId } from './components/Sidebar';
import { GesturePage } from './pages/GesturePage';
import { CapturePage } from './pages/CapturePage';
import { OcrPage } from './pages/OcrPage';
import { GeneralPage } from './pages/GeneralPage';
import { AboutPage } from './pages/AboutPage';
import { PluginsPage, type PluginStatus } from './pages/PluginsPage';
import HistoryPage from './pages/HistoryPage';
import { KeyStatsPage } from './pages/KeyStatsPage';
import { HotCornerPage } from './pages/HotCornerPage';
import { SearchPage } from './pages/SearchPage';
import { ExtensionPage } from './pages/ExtensionPage';
import { OnboardingModal } from './components/OnboardingModal';
import { bridgeRequest } from './hooks/useBridge';
import { Toaster } from 'sonner';
import { useTranslation } from 'react-i18next';
import './App.css';

// 页面组件导入

type Theme = 'dark' | 'light';
type ThemePreference = Theme | 'system';

const NAV_TITLE_KEYS: Record<NavId, string> = {
  stats: 'nav.stats', gesture: 'nav.gesture', hotcorner: 'nav.hotcorner', capture: 'nav.capture',
  ocr: 'nav.ocr', history: 'nav.history', search: 'nav.search', plugins: 'nav.plugins', general: 'nav.settings', about: 'nav.about',
  ai_assistant: 'nav.ai_assistant', color_picker: 'nav.color_picker', clipboard_manager: 'nav.clipboard_manager', markdown_preview: 'nav.markdown_preview',
};
const NAV_SUBTITLE_KEYS: Record<NavId, string> = {
  stats: 'navSubtitle.stats', gesture: 'navSubtitle.gesture', hotcorner: 'navSubtitle.hotcorner', capture: 'navSubtitle.capture',
  ocr: 'navSubtitle.ocr', history: 'navSubtitle.history', search: 'navSubtitle.search', plugins: 'navSubtitle.plugins', general: 'navSubtitle.general', about: 'navSubtitle.about',
  ai_assistant: 'navSubtitle.ai_assistant', color_picker: 'navSubtitle.color_picker', clipboard_manager: 'navSubtitle.clipboard_manager', markdown_preview: 'navSubtitle.markdown_preview',
};

function App() {
  const { t, i18n } = useTranslation();
  const [activeNav, setActiveNav] = useState<NavId>(() => {
    try {
      const saved = localStorage.getItem('easytools:last-nav');
      if (saved && saved in NAV_TITLE_KEYS) {
        return saved as NavId;
      }
    } catch {
      // 忽略本地存储访问异常
    }
    return 'general';
  });
  const getSystemTheme = (): Theme =>
    window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';

  const [systemTheme, setSystemTheme] = useState<Theme>(getSystemTheme());
  const [themePreference, setThemePreference] = useState<ThemePreference>('system');
  const [accent, setAccent] = useState<string>(() => {
    try {
      return localStorage.getItem('easytools:accent-color') || 'blue';
    } catch {
      return 'blue';
    }
  });
  const theme: Theme = themePreference === 'system' ? systemTheme : themePreference;
  const [showOnboarding, setShowOnboarding] = useState(false);
  const [plugins, setPlugins] = useState<PluginStatus[]>([]);
  const pageTitleRef = useRef<HTMLHeadingElement>(null);

  const handleNavSelect = useCallback((nav: NavId) => {
    setActiveNav(nav);
    try {
      localStorage.setItem('easytools:last-nav', nav);
    } catch {
      // 忽略本地存储保存异常
    }
  }, []);

  useEffect(() => {
    const applyPlugins = (items: PluginStatus[]) => setPlugins(Array.isArray(items) ? items : []);
    void bridgeRequest<PluginStatus[]>('plugins.getAll').then(applyPlugins).catch(console.error);
    const handleChange = (event: Event) => applyPlugins((event as CustomEvent<PluginStatus[]>).detail);
    window.addEventListener('easytools:plugins-changed', handleChange);
    return () => window.removeEventListener('easytools:plugins-changed', handleChange);
  }, []);

  const activePlugins = new Set(plugins.filter((plugin) => plugin.active).map((plugin) => plugin.id));

  useEffect(() => {
    let cancelled = false;
    bridgeRequest<{ theme?: string; language?: string; accentColor?: string }>('general.getSettings')
      .then((settings) => {
        if (cancelled) return;
        if (settings.theme === 'light' || settings.theme === 'dark' || settings.theme === 'system') {
          setThemePreference(settings.theme);
        }
        if (typeof settings.accentColor === 'string' && settings.accentColor) {
          setAccent(settings.accentColor);
        }
        if (settings.language && settings.language !== 'auto' && i18n.language !== settings.language) {
          void i18n.changeLanguage(settings.language);
        }
      })
      .catch(console.error);
    return () => { cancelled = true; };
    // 见 GeneralPage：不能把 i18n 放进依赖，否则 changeLanguage 会形成 IPC 风暴。
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

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
    document.documentElement.setAttribute('data-accent', accent);
  }, [accent]);

  useEffect(() => {
    if (showOnboarding) return;
    const frame = requestAnimationFrame(() => pageTitleRef.current?.focus());
    return () => cancelAnimationFrame(frame);
  }, [activeNav, showOnboarding]);

  useEffect(() => {
    const handlePreference = (event: Event) => {
      const preference = (event as CustomEvent<ThemePreference>).detail;
      if (preference === 'system' || preference === 'dark' || preference === 'light') {
        setThemePreference(preference);
      }
    };
    const handleAccent = (event: Event) => {
      const newAccent = (event as CustomEvent<string>).detail;
      if (newAccent) {
        setAccent(newAccent);
        try {
          localStorage.setItem('easytools:accent-color', newAccent);
        } catch (e) {
          void e;
        }
      }
    };
    window.addEventListener('easytools:theme-changed', handlePreference);
    window.addEventListener('easytools:accent-changed', handleAccent);
    return () => {
      window.removeEventListener('easytools:theme-changed', handlePreference);
      window.removeEventListener('easytools:accent-changed', handleAccent);
    };
  }, []);

  const standardPluginIds = new Set(['gesture', 'capture', 'search', 'keycast']);
  const installedExtensionIds = plugins
    .filter((p) => !standardPluginIds.has(p.id))
    .map((p) => p.id);

  // 渲染当前页面
  const renderPage = () => {
    switch (activeNav) {
      case 'stats':     return <KeyStatsPage />;
      case 'gesture':   return <GesturePage />;
      case 'hotcorner': return <HotCornerPage />;
      case 'capture':   return <CapturePage />;
      case 'ocr':       return <OcrPage />;
      case 'history':   return <HistoryPage />;
      case 'search':    return <SearchPage />;
      case 'plugins':   return <PluginsPage initialPlugins={plugins} />;
      case 'general':   return <GeneralPage />;
      case 'about':     return <AboutPage />;
      case 'ai_assistant':
      case 'color_picker':
      case 'clipboard_manager':
      case 'markdown_preview':
        return (
          <ExtensionPage
            pluginId={activeNav}
            plugin={plugins.find((p) => p.id === activeNav)}
            onUninstall={() => {
              handleNavSelect('plugins');
              void bridgeRequest<PluginStatus[]>('plugins.getAll').then((items) =>
                setPlugins(Array.isArray(items) ? items : [])
              );
            }}
          />
        );
      default:          return <KeyStatsPage />;
    }
  };

  return (
    <div className="app">
      <Sidebar
        activeNav={activeNav}
        onNavigate={handleNavSelect}
        theme={theme}
        themePreference={themePreference}
        onSelectThemePreference={(pref) => {
          setThemePreference(pref);
          bridgeRequest<{ success: boolean }>('general.updateSettings', { theme: pref }).catch(console.error);
          window.dispatchEvent(new CustomEvent('easytools:theme-changed', { detail: pref }));
        }}
        accent={accent}
        onSelectAccent={(newAccent) => {
          setAccent(newAccent);
          try {
            localStorage.setItem('easytools:accent-color', newAccent);
          } catch (e) {
            void e;
          }
          bridgeRequest<{ success: boolean }>('general.updateSettings', { accentColor: newAccent }).catch(console.error);
          window.dispatchEvent(new CustomEvent('easytools:accent-changed', { detail: newAccent }));
        }}
        activePlugins={plugins.length > 0 ? activePlugins : undefined}
        installedExtensionIds={installedExtensionIds}
      />
      <main className="app__main" aria-labelledby="app-page-title">
        <Toaster position="bottom-right" theme={theme} richColors expand={true} />
        {/* ── 页面头部 ────────────────────────────────────── */}
        <header className="app__header">
          <div className="app__header-text">
            {/* eslint-disable-next-line @typescript-eslint/no-explicit-any */}
            <h1 id="app-page-title" ref={pageTitleRef} tabIndex={-1} className="app__header-title">{t(NAV_TITLE_KEYS[activeNav] as any)}</h1>
            {/* eslint-disable-next-line @typescript-eslint/no-explicit-any */}
            <p className="app__header-subtitle">{t(NAV_SUBTITLE_KEYS[activeNav] as any)}</p>
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

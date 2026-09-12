import { useLayoutEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { bridgeRequest } from './useBridge';

type Theme = 'light' | 'dark';
type ThemePreference = Theme | 'system';

const systemTheme = (): Theme =>
  window.matchMedia?.('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';

const applyTheme = (preference: ThemePreference) => {
  const theme = preference === 'system' ? systemTheme() : preference;
  document.documentElement.setAttribute('data-theme', theme);
  document.documentElement.style.colorScheme = theme;
};

const applyAccent = (accent: string) => {
  document.documentElement.setAttribute('data-accent', accent || 'blue');
};

/** Keep auxiliary WebView surfaces aligned with the shared app appearance & typography. */
export function useAppearance() {
  const { i18n } = useTranslation();

  useLayoutEffect(() => {
    let disposed = false;
    let preference: ThemePreference = 'system';
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const onSystemThemeChanged = () => {
      if (preference === 'system') applyTheme(preference);
    };

    const syncLanguage = (lang?: unknown) => {
      if (typeof lang !== 'string' || !lang) return;
      const targetLang = lang === 'auto'
        ? (navigator.language.toLowerCase().startsWith('zh') ? 'zh' : 'en')
        : (lang.startsWith('zh') ? 'zh' : 'en');
      if (i18n.language !== targetLang) {
        void i18n.changeLanguage(targetLang);
      }
    };

    try {
      const storedAccent = localStorage.getItem('tools3000:accent-color');
      if (storedAccent) applyAccent(storedAccent);
      const storedLang = localStorage.getItem('tools3000:language');
      if (storedLang) syncLanguage(storedLang);
    } catch (e) {
      void e;
    }

    applyTheme(preference);
    mediaQuery.addEventListener('change', onSystemThemeChanged);

    const onStorage = (e: StorageEvent) => {
      if (e.key === 'tools3000:theme') {
        const val = e.newValue as ThemePreference | null;
        if (val) {
          preference = val;
          applyTheme(preference);
        }
      }
      if (e.key === 'tools3000:accent-color' && e.newValue) {
        applyAccent(e.newValue);
      }
      if (e.key === 'tools3000:language' && e.newValue) {
        syncLanguage(e.newValue);
      }
    };

    const onAccentChanged = (e: Event) => {
      const customEvent = e as CustomEvent<string>;
      const newAccent = customEvent.detail || localStorage.getItem('tools3000:accent-color');
      if (newAccent) applyAccent(newAccent);
    };

    const onThemeChanged = (e: Event) => {
      const customEvent = e as CustomEvent<ThemePreference>;
      const newTheme = customEvent.detail || (localStorage.getItem('tools3000:theme') as ThemePreference);
      if (newTheme) {
        preference = newTheme;
        applyTheme(preference);
      }
    };

    const onLanguageChanged = (e: Event) => {
      const customEvent = e as CustomEvent<string>;
      const newLang = customEvent.detail || localStorage.getItem('tools3000:language');
      if (newLang) syncLanguage(newLang);
    };

    window.addEventListener('storage', onStorage);
    window.addEventListener('tools3000:accent-changed', onAccentChanged);
    window.addEventListener('tools3000:theme-changed', onThemeChanged);
    window.addEventListener('tools3000:language-changed', onLanguageChanged);

    void bridgeRequest<{ theme?: unknown; language?: unknown; accentColor?: unknown }>('general.getSettings')
      .then((settings) => {
        if (disposed) return;
        if (settings.theme === 'light' || settings.theme === 'dark' || settings.theme === 'system') {
          preference = settings.theme;
          applyTheme(preference);
        }
        if (typeof settings.accentColor === 'string' && settings.accentColor) {
          applyAccent(settings.accentColor);
        }
        syncLanguage(settings.language);
      })
      .catch(() => {
        // System appearance is already applied as a resilient fallback.
      });

    return () => {
      disposed = true;
      mediaQuery.removeEventListener('change', onSystemThemeChanged);
      window.removeEventListener('storage', onStorage);
      window.removeEventListener('tools3000:accent-changed', onAccentChanged);
      window.removeEventListener('tools3000:theme-changed', onThemeChanged);
      window.removeEventListener('tools3000:language-changed', onLanguageChanged);
    };
  }, [i18n]);
}

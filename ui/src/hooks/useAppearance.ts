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

const applyFontFamily = (fontFamily?: string) => {
  const preset = fontFamily || 'auto';
  document.documentElement.setAttribute('data-font-preset', preset);
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
      const storedAccent = localStorage.getItem('easytools:accent-color');
      if (storedAccent) applyAccent(storedAccent);
      const storedFont = localStorage.getItem('easytools:font-family');
      if (storedFont) applyFontFamily(storedFont);
      const storedLang = localStorage.getItem('easytools:language');
      if (storedLang) syncLanguage(storedLang);
    } catch (e) {
      void e;
    }

    applyTheme(preference);
    mediaQuery.addEventListener('change', onSystemThemeChanged);

    const onStorage = (e: StorageEvent) => {
      if (e.key === 'easytools:accent-color' && e.newValue) {
        applyAccent(e.newValue);
      }
      if (e.key === 'easytools:font-family' && e.newValue) {
        applyFontFamily(e.newValue);
      }
      if (e.key === 'easytools:language' && e.newValue) {
        syncLanguage(e.newValue);
      }
    };
    const onAccentChanged = (e: Event) => {
      const newAccent = (e as CustomEvent<string>).detail;
      if (newAccent) applyAccent(newAccent);
    };
    const onThemeChanged = (e: Event) => {
      const newTheme = (e as CustomEvent<ThemePreference>).detail;
      if (newTheme) {
        preference = newTheme;
        applyTheme(preference);
      }
    };
    const onFontChanged = (e: Event) => {
      const newFont = (e as CustomEvent<string>).detail;
      if (newFont) applyFontFamily(newFont);
    };
    const onLanguageChanged = (e: Event) => {
      const newLang = (e as CustomEvent<string>).detail;
      if (newLang) syncLanguage(newLang);
    };

    window.addEventListener('storage', onStorage);
    window.addEventListener('easytools:accent-changed', onAccentChanged);
    window.addEventListener('easytools:theme-changed', onThemeChanged);
    window.addEventListener('easytools:font-changed', onFontChanged);
    window.addEventListener('easytools:language-changed', onLanguageChanged);

    void bridgeRequest<{ theme?: unknown; language?: unknown; accentColor?: unknown; fontFamily?: unknown }>('general.getSettings')
      .then((settings) => {
        if (disposed) return;
        if (settings.theme === 'light' || settings.theme === 'dark' || settings.theme === 'system') {
          preference = settings.theme;
          applyTheme(preference);
        }
        if (typeof settings.accentColor === 'string' && settings.accentColor) {
          applyAccent(settings.accentColor);
        }
        if (typeof settings.fontFamily === 'string' && settings.fontFamily) {
          applyFontFamily(settings.fontFamily);
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
      window.removeEventListener('easytools:accent-changed', onAccentChanged);
      window.removeEventListener('easytools:theme-changed', onThemeChanged);
      window.removeEventListener('easytools:font-changed', onFontChanged);
      window.removeEventListener('easytools:language-changed', onLanguageChanged);
    };
  }, [i18n]);
}

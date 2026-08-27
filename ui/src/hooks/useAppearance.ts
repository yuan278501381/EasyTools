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

    try {
      const storedAccent = localStorage.getItem('easytools:accent-color');
      if (storedAccent) applyAccent(storedAccent);
      const storedFont = localStorage.getItem('easytools:font-family');
      if (storedFont) applyFontFamily(storedFont);
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

    window.addEventListener('storage', onStorage);
    window.addEventListener('easytools:accent-changed', onAccentChanged);
    window.addEventListener('easytools:theme-changed', onThemeChanged);
    window.addEventListener('easytools:font-changed', onFontChanged);

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
        if (typeof settings.language === 'string' && settings.language !== 'auto' &&
            i18n.language !== settings.language) {
          void i18n.changeLanguage(settings.language);
        }
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
    };
  }, [i18n]);
}

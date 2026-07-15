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

/** Keep auxiliary WebView surfaces aligned with the shared app appearance. */
export function useAppearance() {
  const { i18n } = useTranslation();

  useLayoutEffect(() => {
    let disposed = false;
    let preference: ThemePreference = 'system';
    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const onSystemThemeChanged = () => {
      if (preference === 'system') applyTheme(preference);
    };

    applyTheme(preference);
    mediaQuery.addEventListener('change', onSystemThemeChanged);

    void bridgeRequest<{ theme?: unknown; language?: unknown }>('general.getSettings')
      .then((settings) => {
        if (disposed) return;
        if (settings.theme === 'light' || settings.theme === 'dark' || settings.theme === 'system') {
          preference = settings.theme;
          applyTheme(preference);
        }
        if (typeof settings.language === 'string' && settings.language !== 'auto') {
          void i18n.changeLanguage(settings.language);
        }
      })
      .catch(() => {
        // System appearance is already applied as a resilient fallback.
      });

    return () => {
      disposed = true;
      mediaQuery.removeEventListener('change', onSystemThemeChanged);
    };
  }, [i18n]);
}

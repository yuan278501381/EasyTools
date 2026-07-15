import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { File, Folder, Search, ServerOff } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { bridgeRequest } from './hooks/useBridge';
import { useAppearance } from './hooks/useAppearance';
import './SearchApp.css';

interface SearchResult {
  name: string;
  path: string;
  isDirectory: boolean;
}

interface SearchResponse {
  results: SearchResult[];
  available: boolean;
  error?: string;
}

const IMAGE_EXTENSIONS = /\.(png|jpe?g|webp|bmp)$/i;

export default function SearchApp() {
  useAppearance();
  const { t } = useTranslation();
  const [query, setQuery] = useState('');
  const [results, setResults] = useState<SearchResult[]>([]);
  const [selectedIndex, setSelectedIndex] = useState(0);
  const [loading, setLoading] = useState(false);
  const [serviceAvailable, setServiceAvailable] = useState(true);
  const [actionError, setActionError] = useState('');
  const inputRef = useRef<HTMLInputElement>(null);
  const requestSequence = useRef(0);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  useEffect(() => {
    const trimmed = query.trim();
    if (!trimmed) return;

    const sequence = ++requestSequence.current;
    const timer = window.setTimeout(async () => {
      setLoading(true);
      try {
        const response = await bridgeRequest<SearchResponse>('search.query', { query: trimmed });
        if (sequence !== requestSequence.current) return;
        setResults(Array.isArray(response.results) ? response.results : []);
        setSelectedIndex(0);
        setServiceAvailable(response.available !== false);
      } catch {
        if (sequence !== requestSequence.current) return;
        setResults([]);
        setServiceAvailable(false);
      } finally {
        if (sequence === requestSequence.current) setLoading(false);
      }
    }, 120);

    return () => window.clearTimeout(timer);
  }, [query]);

  const updateQuery = (value: string) => {
    setQuery(value);
    setActionError('');
    if (!value.trim()) {
      requestSequence.current++;
      setResults([]);
      setSelectedIndex(0);
      setLoading(false);
    }
  };

  const hide = useCallback(() => {
    void bridgeRequest('search.hide').catch(() => undefined);
  }, []);

  const openResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      const response = await bridgeRequest<{ success: boolean }>('search.openFile', {
        filepath: result.path,
      });
      if (response.success) hide();
      else setActionError(t('search.openFailed'));
    } catch {
      setActionError(t('search.openFailed'));
    }
  }, [hide, t]);

  const pinResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result || result.isDirectory || !IMAGE_EXTENSIONS.test(result.path)) return;
    setActionError('');
    try {
      const response = await bridgeRequest<{ success: boolean }>('capture.pinImageFile', {
        path: result.path,
      });
      if (response.success) hide();
      else setActionError(t('search.pinFailed'));
    } catch {
      setActionError(t('search.pinFailed'));
    }
  }, [hide, t]);

  const handleKeyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      setSelectedIndex((index) => Math.min(index + 1, Math.max(results.length - 1, 0)));
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      setSelectedIndex((index) => Math.max(index - 1, 0));
    } else if (event.key === 'Enter') {
      event.preventDefault();
      void openResult(results[selectedIndex]);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      hide();
    } else if (event.key.toLowerCase() === 'p' && event.ctrlKey) {
      event.preventDefault();
      void pinResult(results[selectedIndex]);
    }
  };

  return (
    <main className="search-app">
      <section className="search-container" aria-label={t('search.title')}>
        <div className="search-input-wrapper">
          <Search className="search-icon" size={22} aria-hidden="true" />
          <input
            ref={inputRef}
            className="search-input"
            placeholder={t('search.placeholder')}
            value={query}
            onChange={(event) => updateQuery(event.target.value)}
            onKeyDown={handleKeyDown}
            role="combobox"
            aria-expanded={results.length > 0}
            aria-controls="search-results"
            aria-activedescendant={results[selectedIndex] ? `search-result-${selectedIndex}` : undefined}
            spellCheck={false}
          />
          {loading && <span className="search-loading" aria-label={t('common.loading')} />}
        </div>

        {!serviceAvailable && (
          <div className="search-status" role="status">
            <ServerOff size={18} aria-hidden="true" />
            <span>{t('search.serviceUnavailable')}</span>
          </div>
        )}

        {actionError && <div className="search-status search-status--error" role="alert">{actionError}</div>}

        {serviceAvailable && query.trim() && !loading && results.length === 0 && (
          <div className="search-status" role="status">{t('search.noResults')}</div>
        )}

        {results.length > 0 && (
          <ul id="search-results" className="search-results" role="listbox">
            {results.map((result, index) => (
              <li
                id={`search-result-${index}`}
                key={result.path}
                className={`search-result-item ${index === selectedIndex ? 'selected' : ''}`}
                role="option"
                aria-selected={index === selectedIndex}
                onMouseEnter={() => setSelectedIndex(index)}
                onMouseDown={(event) => event.preventDefault()}
                onClick={() => setSelectedIndex(index)}
                onDoubleClick={() => void openResult(result)}
              >
                {result.isDirectory
                  ? <Folder className="file-icon" size={20} aria-hidden="true" />
                  : <File className="file-icon" size={20} aria-hidden="true" />}
                <span className="file-info">
                  <span className="file-name">{result.name}</span>
                  <span className="file-path">{result.path}</span>
                </span>
              </li>
            ))}
          </ul>
        )}

        <footer className="search-footer">
          <span className="search-hint"><kbd>Enter</kbd> {t('search.open')}</span>
          <span className="search-hint"><kbd>Ctrl+P</kbd> {t('search.pinImage')}</span>
          <span className="search-hint"><kbd>Esc</kbd> {t('search.close')}</span>
        </footer>
      </section>
    </main>
  );
}

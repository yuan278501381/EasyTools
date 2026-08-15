import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { 
  File, 
  Folder, 
  Search, 
  ServerOff, 
  FileImage, 
  FileCode, 
  FileArchive, 
  FileText, 
  FileVideo, 
  FileAudio, 
  AppWindow 
} from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
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

const IMAGE_EXTENSIONS = /\.(png|jpe?g|webp|bmp|gif|svg|ico)$/i;

function renderFileIcon(name: string, isDirectory: boolean) {
  if (isDirectory) {
    return <Folder className="file-icon file-icon--folder" size={20} aria-hidden="true" />;
  }
  const ext = name.slice(name.lastIndexOf('.')).toLowerCase();
  if (IMAGE_EXTENSIONS.test(ext)) {
    return <FileImage className="file-icon file-icon--image" size={20} aria-hidden="true" />;
  }
  if (/\.(zip|rar|7z|tar|gz|bz2|iso)$/i.test(ext)) {
    return <FileArchive className="file-icon file-icon--archive" size={20} aria-hidden="true" />;
  }
  if (/\.(exe|msi|bat|cmd|ps1|com)$/i.test(ext)) {
    return <AppWindow className="file-icon file-icon--exe" size={20} aria-hidden="true" />;
  }
  if (/\.(cpp|c|h|hpp|ts|tsx|js|jsx|json|py|rs|go|java|html|css|lua|sql|yml|yaml|xml)$/i.test(ext)) {
    return <FileCode className="file-icon file-icon--code" size={20} aria-hidden="true" />;
  }
  if (/\.(mp4|mkv|avi|mov|flv|webm|wmv)$/i.test(ext)) {
    return <FileVideo className="file-icon file-icon--media" size={20} aria-hidden="true" />;
  }
  if (/\.(mp3|wav|flac|ogg|aac|m4a)$/i.test(ext)) {
    return <FileAudio className="file-icon file-icon--audio" size={20} aria-hidden="true" />;
  }
  if (/\.(txt|md|pdf|doc|docx|xls|xlsx|ppt|pptx|log|csv)$/i.test(ext)) {
    return <FileText className="file-icon file-icon--doc" size={20} aria-hidden="true" />;
  }
  return <File className="file-icon" size={20} aria-hidden="true" />;
}

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
    }, 100);

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

  const openFolderResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      const response = await bridgeRequest<{ success: boolean }>('search.openFolder', {
        filepath: result.path,
      });
      if (response.success) hide();
      else setActionError(t('search.openFailed'));
    } catch {
      setActionError(t('search.openFailed'));
    }
  }, [hide, t]);

  const copyPathResult = useCallback((result: SearchResult | undefined) => {
    if (!result) return;
    void navigator.clipboard.writeText(result.path);
    toast.success(t('search.copiedPath'));
  }, [t]);

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
    } else if (event.key === 'PageDown') {
      event.preventDefault();
      setSelectedIndex((index) => Math.min(index + 5, Math.max(results.length - 1, 0)));
    } else if (event.key === 'PageUp') {
      event.preventDefault();
      setSelectedIndex((index) => Math.max(index - 5, 0));
    } else if (event.key === 'Home') {
      event.preventDefault();
      setSelectedIndex(0);
    } else if (event.key === 'End') {
      event.preventDefault();
      setSelectedIndex(Math.max(results.length - 1, 0));
    } else if (event.key === 'Enter') {
      event.preventDefault();
      if (event.ctrlKey || event.shiftKey) {
        void openFolderResult(results[selectedIndex]);
      } else {
        void openResult(results[selectedIndex]);
      }
    } else if (event.key === 'Escape') {
      event.preventDefault();
      hide();
    } else if (event.key.toLowerCase() === 'c' && event.ctrlKey) {
      if (results.length > 0 && selectedIndex >= 0 && selectedIndex < results.length) {
        event.preventDefault();
        copyPathResult(results[selectedIndex]);
      }
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
                {renderFileIcon(result.name, result.isDirectory)}
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
          <span className="search-hint"><kbd>Ctrl+Enter</kbd> {t('search.openFolder')}</span>
          <span className="search-hint"><kbd>Ctrl+C</kbd> {t('search.copyPath')}</span>
          <span className="search-hint"><kbd>Ctrl+P</kbd> {t('search.pinImage')}</span>
          <span className="search-hint"><kbd>Esc</kbd> {t('search.close')}</span>
        </footer>
      </section>
    </main>
  );
}

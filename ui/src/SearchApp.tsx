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
  AppWindow,
  HelpCircle,
  X,
  Sparkles
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

interface CategoryFilter {
  id: string;
  label: string;
  prefix: string;
}

const CATEGORIES: CategoryFilter[] = [
  { id: 'all', label: '全部', prefix: '' },
  { id: 'doc', label: '文档', prefix: 'ext:doc;docx;xls;xlsx;ppt;pptx;pdf;txt;md ' },
  { id: 'image', label: '图片', prefix: 'ext:jpg;jpeg;png;webp;gif;bmp;svg ' },
  { id: 'video', label: '视频', prefix: 'ext:mp4;mkv;avi;mov;wmv;flv;webm ' },
  { id: 'audio', label: '音频', prefix: 'ext:mp3;wav;flac;aac;m4a;ogg ' },
  { id: 'archive', label: '压缩包', prefix: 'ext:zip;rar;7z;tar;gz ' },
  { id: 'code', label: '代码', prefix: 'ext:cpp;h;ts;tsx;js;py;rs;go;java;lua;json ' },
  { id: 'folder', label: '文件夹', prefix: 'folder: ' },
];

const SYNTAX_EXAMPLES = [
  { syntax: '*.txt', desc: '通配符匹配所有 txt 后缀文件' },
  { syntax: 'ext:jpg;png', desc: '多扩展名筛选' },
  { syntax: 'file: *.pdf', desc: '仅搜文件，排除文件夹' },
  { syntax: 'folder: project', desc: '仅搜文件夹目录' },
  { syntax: 'report !draft', desc: '包含 report 但排除包含 draft 的项' },
  { syntax: 'ext:jpg | ext:png', desc: '逻辑或 OR' },
  { syntax: '"Program Files"', desc: '双引号短语精确匹配' },
  { syntax: 'path:windows', desc: '在完整路径中搜索' },
  { syntax: 'c: *.dll', desc: '限定在 C 盘检索' },
  { syntax: 'regex:^app_\\d+\\.log$', desc: '正则表达式检索' },
  { syntax: 'case:EasyTools', desc: '区分大小写搜索' },
  { syntax: 'pinyin:wx', desc: '显式拼音首字母/全拼检索' },
];

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
  const [activeCategory, setActiveCategory] = useState('all');
  const [results, setResults] = useState<SearchResult[]>([]);
  const [selectedIndex, setSelectedIndex] = useState(0);
  const [loading, setLoading] = useState(false);
  const [serviceAvailable, setServiceAvailable] = useState(true);
  const [actionError, setActionError] = useState('');
  const [showSyntaxHelp, setShowSyntaxHelp] = useState(false);
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

  const updateQuery = (next: string) => {
    setQuery(next);
    setActionError('');
    if (!next.trim()) {
      setResults([]);
      setSelectedIndex(0);
      setLoading(false);
    }
  };

  const selectCategory = (cat: CategoryFilter) => {
    setActiveCategory(cat.id);
    if (!cat.prefix) {
      // Clear any category prefix
      let cleaned = query;
      CATEGORIES.forEach(c => {
        if (c.prefix && cleaned.startsWith(c.prefix)) {
          cleaned = cleaned.slice(c.prefix.length);
        }
      });
      updateQuery(cleaned.trimStart());
    } else {
      let cleaned = query;
      CATEGORIES.forEach(c => {
        if (c.prefix && cleaned.startsWith(c.prefix)) {
          cleaned = cleaned.slice(c.prefix.length);
        }
      });
      updateQuery(cat.prefix + cleaned.trimStart());
    }
    inputRef.current?.focus();
  };

  const applySyntaxExample = (syntax: string) => {
    updateQuery(syntax);
    setShowSyntaxHelp(false);
    inputRef.current?.focus();
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
      else setActionError(t('search.openFailed', '无法打开此结果'));
    } catch {
      setActionError(t('search.openFailed', '无法打开此结果'));
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
      else setActionError(t('search.openFailed', '无法定位文件夹'));
    } catch {
      setActionError(t('search.openFailed', '无法定位文件夹'));
    }
  }, [hide, t]);

  const copyPathResult = useCallback((result: SearchResult | undefined) => {
    if (!result) return;
    void navigator.clipboard.writeText(result.path);
    toast.success(t('search.copiedPath', '文件路径已复制到剪贴板'));
  }, [t]);

  const pinResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result || result.isDirectory || !IMAGE_EXTENSIONS.test(result.path)) return;
    setActionError('');
    try {
      const response = await bridgeRequest<{ success: boolean }>('capture.pinImageFile', {
        path: result.path,
      });
      if (response.success) hide();
      else setActionError(t('search.pinFailed', '无法贴出此图片'));
    } catch {
      setActionError(t('search.pinFailed', '无法贴出此图片'));
    }
  }, [hide, t]);

  const handleKeyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'F1') {
      event.preventDefault();
      setShowSyntaxHelp(prev => !prev);
      return;
    }
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
      if (showSyntaxHelp) {
        setShowSyntaxHelp(false);
      } else {
        hide();
      }
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
      <section className="search-container" aria-label={t('search.title', '快速文件搜索')}>
        {/* ── 搜索输入框主区域 ─────────────────────────────────────── */}
        <div className="search-input-wrapper">
          <Search className="search-icon" size={22} aria-hidden="true" />
          <input
            ref={inputRef}
            className="search-input"
            placeholder={t('search.placeholder', '搜索文件、通配符 (*.txt)、扩展名 (ext:png) 或拼音... [F1 语法]')}
            value={query}
            onChange={(event) => updateQuery(event.target.value)}
            onKeyDown={handleKeyDown}
            role="combobox"
            aria-expanded={results.length > 0}
            aria-controls="search-results"
            aria-activedescendant={results[selectedIndex] ? `search-result-${selectedIndex}` : undefined}
            spellCheck={false}
          />
          {loading && <span className="search-loading" aria-label={t('common.loading', '正在搜索...')} />}
          <button
            className={`search-help-btn ${showSyntaxHelp ? 'search-help-btn--active' : ''}`}
            onClick={() => setShowSyntaxHelp(prev => !prev)}
            title="搜索语法与表达式速查 (F1)"
            type="button"
          >
            <HelpCircle size={18} />
          </button>
        </div>

        {/* ── 分类筛选胶囊栏 ──────────────────────────────────────── */}
        <div className="search-categories">
          {CATEGORIES.map(cat => (
            <button
              key={cat.id}
              className={`category-pill ${activeCategory === cat.id ? 'category-pill--active' : ''}`}
              onClick={() => selectCategory(cat)}
              type="button"
            >
              {cat.label}
            </button>
          ))}
        </div>

        {/* ── 语法速查抽屉面板 ─────────────────────────────────────── */}
        {showSyntaxHelp && (
          <div className="search-syntax-drawer">
            <div className="syntax-drawer-header">
              <div className="syntax-drawer-title">
                <Sparkles size={16} />
                <span>Everything 级高级搜索语法速查</span>
              </div>
              <button
                className="syntax-drawer-close"
                onClick={() => setShowSyntaxHelp(false)}
                type="button"
              >
                <X size={16} />
              </button>
            </div>
            <div className="syntax-drawer-list">
              {SYNTAX_EXAMPLES.map((item, idx) => (
                <div
                  key={idx}
                  className="syntax-example-item"
                  onClick={() => applySyntaxExample(item.syntax)}
                  title="点击直接填入搜索框"
                >
                  <code className="syntax-code">{item.syntax}</code>
                  <span className="syntax-desc">{item.desc}</span>
                </div>
              ))}
            </div>
          </div>
        )}

        {!serviceAvailable && (
          <div className="search-status" role="status">
            <ServerOff size={18} aria-hidden="true" />
            <span>{t('search.serviceUnavailable', '文件索引服务暂不可用，正在尝试自动连接或静默拉起。')}</span>
          </div>
        )}

        {actionError && <div className="search-status search-status--error" role="alert">{actionError}</div>}

        {serviceAvailable && query.trim() && !loading && results.length === 0 && (
          <div className="search-status" role="status">{t('search.noResults', '没有匹配的文件')}</div>
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
          <span className="search-hint"><kbd>Enter</kbd> {t('search.open', '打开')}</span>
          <span className="search-hint"><kbd>Ctrl+Enter</kbd> {t('search.openFolder', '定位目录')}</span>
          <span className="search-hint"><kbd>Ctrl+C</kbd> {t('search.copyPath', '复制路径')}</span>
          <span className="search-hint"><kbd>Ctrl+P</kbd> {t('search.pinImage', '贴图')}</span>
          <span className="search-hint"><kbd>F1</kbd> 语法手册</span>
          <span className="search-hint"><kbd>Esc</kbd> {t('search.close', '关闭')}</span>
        </footer>
      </section>
    </main>
  );
}

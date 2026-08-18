import { useCallback, useEffect, useMemo, useRef, useState, startTransition, type KeyboardEvent } from 'react';
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
  Sparkles,
  SlidersHorizontal,
  ArrowUp,
  ArrowDown,
  RotateCcw,
  Maximize2,
  Clock,
  ArrowDownAZ,
  Zap,
  HardDrive,
  Calendar,
  ChevronDown
} from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { bridgeRequest } from './hooks/useBridge';
import { useAppearance } from './hooks/useAppearance';
import './SearchApp.css';

interface ContentSnippet {
  lineNumber: number;
  lineContent: string;
  matchOffset?: number;
  matchLength?: number;
}

interface SearchResult {
  name: string;
  path: string;
  isDirectory: boolean;
  size?: number;
  creationTime?: number;
  lastWriteTime?: number;
  snippets?: ContentSnippet[];
}

interface SearchResponse {
  results: SearchResult[];
  available: boolean;
  error?: string;
}

export type SortField = 'relevance' | 'modified' | 'name' | 'size' | 'created';
export type SortDirection = 'asc' | 'desc';

export type ColumnId = 'name' | 'ext' | 'parent' | 'path' | 'size' | 'modified' | 'created' | 'snippets';

export interface ColumnSetting {
  id: ColumnId;
  label: string;
  visible: boolean;
  flex?: number;
}

const DEFAULT_COLUMNS: ColumnSetting[] = [
  { id: 'name', label: '文件名', visible: true, flex: 32 },
  { id: 'ext', label: '类型标签', visible: true },
  { id: 'parent', label: '所属文件夹', visible: true },
  { id: 'path', label: '完整路径', visible: true, flex: 42 },
  { id: 'size', label: '大小', visible: true },
  { id: 'modified', label: '修改时间', visible: true },
  { id: 'created', label: '创建时间', visible: false },
  { id: 'snippets', label: '内容摘要', visible: true },
];

function extractParentFolder(fullPath: string): string {
  if (!fullPath) return '';
  const normalized = fullPath.replace(/\//g, '\\');
  const lastSlash = normalized.lastIndexOf('\\');
  if (lastSlash <= 0) return '';
  const parentPart = normalized.slice(0, lastSlash);
  const secondLastSlash = parentPart.lastIndexOf('\\');
  if (secondLastSlash < 0) return parentPart;
  return parentPart.slice(secondLastSlash + 1);
}

function getFileTypeBadge(name: string, isDirectory: boolean): { label: string; colorClass: string } {
  if (isDirectory) return { label: '文件夹', colorClass: 'badge-folder' };
  const ext = name.slice(name.lastIndexOf('.')).toLowerCase();
  if (/\.(cpp|c|h|hpp)$/i.test(ext)) return { label: ext, colorClass: 'badge-code-c' };
  if (/\.(ts|tsx|js|jsx)$/i.test(ext)) return { label: ext, colorClass: 'badge-code-js' };
  if (/\.(py|rs|go|java|lua|sql|sh|ps1)$/i.test(ext)) return { label: ext, colorClass: 'badge-code-other' };
  if (/\.(json|yml|yaml|xml|toml|ini|cfg)$/i.test(ext)) return { label: ext, colorClass: 'badge-config' };
  if (/\.(png|jpe?g|webp|gif|svg|ico)$/i.test(ext)) return { label: ext.slice(1).toUpperCase(), colorClass: 'badge-img' };
  if (/\.(zip|rar|7z|tar|gz)$/i.test(ext)) return { label: ext.slice(1).toUpperCase(), colorClass: 'badge-archive' };
  if (/\.(pdf|docx?|xlsx?|pptx?|txt|md)$/i.test(ext)) return { label: ext.slice(1).toUpperCase(), colorClass: 'badge-doc' };
  if (/\.(mp4|mkv|avi|mov|flv)$/i.test(ext)) return { label: ext.slice(1).toUpperCase(), colorClass: 'badge-media' };
  if (/\.(mp3|wav|flac|aac|ogg)$/i.test(ext)) return { label: ext.slice(1).toUpperCase(), colorClass: 'badge-media' };
  if (/\.(exe|dll|msi)$/i.test(ext)) return { label: ext.slice(1).toUpperCase(), colorClass: 'badge-bin' };
  return { label: ext ? ext.slice(1).toUpperCase() : '文件', colorClass: 'badge-generic' };
}

export interface WindowPreset {
  id: string;
  label: string;
  width: number;
  height: number;
}

const WINDOW_PRESETS: WindowPreset[] = [
  { id: 'standard', label: '标准 (800×600)', width: 800, height: 600 },
  { id: 'wide', label: '宽屏 (1000×650)', width: 1000, height: 650 },
  { id: 'large', label: '大屏 (1200×750)', width: 1200, height: 750 },
  { id: 'extra', label: '超宽 (1400×800)', width: 1400, height: 800 },
];

function renderSnippetWithHighlight(text: string, offset?: number, length?: number) {
  if (offset === undefined || length === undefined || length === 0 || offset >= text.length) {
    return text;
  }
  const before = text.slice(0, offset);
  const match = text.slice(offset, offset + length);
  const after = text.slice(offset + length);
  return (
    <>
      {before}
      <mark className="snippet-highlight">{match}</mark>
      {after}
    </>
  );
}

function formatFileSize(bytes?: number, isDirectory?: boolean): string {
  if (isDirectory) return '文件夹';
  if (bytes === undefined || bytes === null || isNaN(bytes)) return '';
  if (bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let size = bytes;
  let unitIndex = 0;
  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024;
    unitIndex++;
  }
  return `${size.toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

function formatWindowsTime(ft?: number | string): string {
  if (!ft) return '';
  const num = typeof ft === 'string' ? parseInt(ft, 10) : ft;
  if (!num || isNaN(num) || num <= 0) return '';
  
  let date: Date;
  if (num > 10000000000000000) {
    const unixMs = (num - 116444736000000000) / 10000;
    date = new Date(unixMs);
  } else if (num > 1000000000000) {
    date = new Date(num);
  } else {
    date = new Date(num * 1000);
  }

  if (isNaN(date.getTime()) || date.getFullYear() < 1980 || date.getFullYear() > 2100) {
    return '';
  }

  const y = date.getFullYear();
  const m = String(date.getMonth() + 1).padStart(2, '0');
  const d = String(date.getDate()).padStart(2, '0');
  const hh = String(date.getHours()).padStart(2, '0');
  const mm = String(date.getMinutes()).padStart(2, '0');
  return `${y}-${m}-${d} ${hh}:${mm}`;
}

interface CategoryFilter {
  id: string;
  label: string;
  prefix: string;
}

const CATEGORIES: CategoryFilter[] = [
  { id: 'all', label: '全部', prefix: '' },
  { id: 'content', label: '文件内容', prefix: 'content:' },
  { id: 'doc', label: '文档', prefix: 'ext:doc;docx;xls;xlsx;ppt;pptx;pdf;txt;md ' },
  { id: 'image', label: '图片', prefix: 'ext:jpg;jpeg;png;webp;gif;bmp;svg ' },
  { id: 'video', label: '视频', prefix: 'ext:mp4;mkv;avi;mov;wmv;flv;webm ' },
  { id: 'audio', label: '音频', prefix: 'ext:mp3;wav;flac;aac;m4a;ogg ' },
  { id: 'archive', label: '压缩包', prefix: 'ext:zip;rar;7z;tar;gz ' },
  { id: 'code', label: '代码', prefix: 'ext:cpp;h;ts;tsx;js;py;rs;go;java;lua;json ' },
  { id: 'folder', label: '文件夹', prefix: 'folder: ' },
];

const SYNTAX_EXAMPLES = [
  { syntax: 'content:SELECT', desc: '全文搜索代码、文档、设计稿与 CAD 内文本' },
  { syntax: 'ext:docx;sql content:订单', desc: '在指定扩展名类型文件中穿透搜索内容' },
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

type SearchDensity = 'compact' | 'standard' | 'comfortable';

function renderFileIcon(name: string, isDirectory: boolean, density: SearchDensity = 'standard') {
  const iconSize = density === 'compact' ? 16 : density === 'comfortable' ? 24 : 20;
  if (isDirectory) {
    return <Folder className="file-icon file-icon--folder" size={iconSize} aria-hidden="true" />;
  }
  const ext = name.slice(name.lastIndexOf('.')).toLowerCase();
  if (IMAGE_EXTENSIONS.test(ext)) {
    return <FileImage className="file-icon file-icon--image" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(zip|rar|7z|tar|gz|bz2|iso)$/i.test(ext)) {
    return <FileArchive className="file-icon file-icon--archive" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(exe|msi|bat|cmd|ps1|com)$/i.test(ext)) {
    return <AppWindow className="file-icon file-icon--exe" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(cpp|c|h|hpp|ts|tsx|js|jsx|json|py|rs|go|java|html|css|lua|sql|yml|yaml|xml)$/i.test(ext)) {
    return <FileCode className="file-icon file-icon--code" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(mp4|mkv|avi|mov|flv|webm|wmv)$/i.test(ext)) {
    return <FileVideo className="file-icon file-icon--media" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(mp3|wav|flac|ogg|aac|m4a)$/i.test(ext)) {
    return <FileAudio className="file-icon file-icon--audio" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(txt|md|pdf|doc|docx|xls|xlsx|ppt|pptx|log|csv)$/i.test(ext)) {
    return <FileText className="file-icon file-icon--doc" size={iconSize} aria-hidden="true" />;
  }
  return <File className="file-icon" size={iconSize} aria-hidden="true" />;
}

const SORT_OPTIONS: { id: SortField; label: string; defaultDir: SortDirection; icon: (s?: number) => React.ReactNode }[] = [
  { id: 'relevance', label: '智能相关度', defaultDir: 'desc', icon: (s = 13) => <Zap size={s} /> },
  { id: 'modified', label: '修改时间', defaultDir: 'desc', icon: (s = 13) => <Clock size={s} /> },
  { id: 'name', label: '文件名 (A-Z)', defaultDir: 'asc', icon: (s = 13) => <ArrowDownAZ size={s} /> },
  { id: 'size', label: '文件大小', defaultDir: 'desc', icon: (s = 13) => <HardDrive size={s} /> },
  { id: 'created', label: '创建时间', defaultDir: 'desc', icon: (s = 13) => <Calendar size={s} /> },
];

export default function SearchApp() {
  useAppearance();
  const { t } = useTranslation();
  const [query, setQuery] = useState('');
  const [results, setResults] = useState<SearchResult[]>([]);
  const [selectedIndex, setSelectedIndex] = useState(0);
  const [loading, setLoading] = useState(false);
  const [serviceAvailable, setServiceAvailable] = useState(true);
  const [actionError, setActionError] = useState('');
  const [showSyntaxHelp, setShowSyntaxHelp] = useState(false);
  const [showViewSettings, setShowViewSettings] = useState(false);

  const [sortField, setSortField] = useState<SortField>(() => {
    const saved = localStorage.getItem('easytools_search_sort_field');
    if (saved === 'modified' || saved === 'name' || saved === 'size' || saved === 'created') {
      return saved;
    }
    return 'relevance';
  });

  const [sortDirection, setSortDirection] = useState<SortDirection>(() => {
    const saved = localStorage.getItem('easytools_search_sort_dir');
    if (saved === 'asc' || saved === 'desc') {
      return saved;
    }
    return 'desc';
  });

  const [showSortMenu, setShowSortMenu] = useState(false);
  const sortDropdownRef = useRef<HTMLDivElement>(null);

  const [windowSize, setWindowSize] = useState<{ width: number; height: number }>({ width: 800, height: 600 });
  const [density, setDensity] = useState<SearchDensity>(() => {
    const saved = localStorage.getItem('easytools_search_density');
    if (saved === 'compact' || saved === 'standard' || saved === 'comfortable') {
      return saved;
    }
    return 'standard';
  });

  const [columns, setColumns] = useState<ColumnSetting[]>(() => {
    try {
      const saved = localStorage.getItem('easytools_search_columns_v2');
      if (saved) {
        const parsed = JSON.parse(saved) as ColumnSetting[];
        if (Array.isArray(parsed) && parsed.length > 0) {
          const merged = parsed.map(c => {
            const def = DEFAULT_COLUMNS.find(d => d.id === c.id);
            return def ? { ...def, ...c } : c;
          });
          DEFAULT_COLUMNS.forEach(def => {
            if (!merged.some(m => m.id === def.id)) {
              merged.push(def);
            }
          });
          return merged;
        }
      }
    } catch {
      // fallback
    }
    return DEFAULT_COLUMNS;
  });

  const inputRef = useRef<HTMLInputElement>(null);
  const requestSequence = useRef(0);

  useEffect(() => {
    void bridgeRequest<{ width?: number; height?: number }>('search.getWindowSize')
      .then((res) => {
        if (res?.width && res?.height) {
          setWindowSize({ width: res.width, height: res.height });
        }
      })
      .catch(() => undefined);
  }, []);

  const changeWindowSize = (width: number, height: number) => {
    setWindowSize({ width, height });
    void bridgeRequest('search.setWindowSize', { width, height }).catch(() => undefined);
  };

  const handleResizeMouseDown = (e: React.MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    const startX = e.clientX;
    const startY = e.clientY;
    const startW = windowSize.width;
    const startH = windowSize.height;

    const onMouseMove = (moveEvt: MouseEvent) => {
      const deltaX = (moveEvt.clientX - startX) * 2;
      const deltaY = (moveEvt.clientY - startY) * 2;
      const newW = Math.max(500, Math.min(2200, Math.round(startW + deltaX)));
      const newH = Math.max(400, Math.min(1400, Math.round(startH + deltaY)));
      setWindowSize({ width: newW, height: newH });
      void bridgeRequest('search.setWindowSize', { width: newW, height: newH }).catch(() => undefined);
    };

    const onMouseUp = () => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    };

    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
  };

  const moveColumn = (index: number, direction: 'up' | 'down') => {
    const targetIndex = direction === 'up' ? index - 1 : index + 1;
    if (targetIndex < 0 || targetIndex >= columns.length) return;
    const updated = [...columns];
    const temp = updated[index];
    updated[index] = updated[targetIndex];
    updated[targetIndex] = temp;
    setColumns(updated);
    localStorage.setItem('easytools_search_columns_v2', JSON.stringify(updated));
  };

  const toggleColumnVisibility = (id: ColumnId) => {
    const updated = columns.map(col => col.id === id ? { ...col, visible: !col.visible } : col);
    setColumns(updated);
    localStorage.setItem('easytools_search_columns_v2', JSON.stringify(updated));
  };

  const updateColumnFlex = (id: ColumnId, flex: number) => {
    const updated = columns.map(col => col.id === id ? { ...col, flex } : col);
    setColumns(updated);
    localStorage.setItem('easytools_search_columns_v2', JSON.stringify(updated));
  };

  const resetColumns = () => {
    setColumns(DEFAULT_COLUMNS);
    localStorage.setItem('easytools_search_columns_v2', JSON.stringify(DEFAULT_COLUMNS));
    toast.success('已恢复默认列与视图布局');
  };

  const activeCategory = useMemo(() => {
    const trimmed = query.trimStart().toLowerCase();
    if (trimmed.startsWith('content:') || (trimmed.startsWith('c:') && !trimmed.startsWith('c:\\') && !trimmed.startsWith('c:/')) || query.trimStart().startsWith('内容:')) {
      return 'content';
    }
    if (trimmed.startsWith('folder:') || trimmed.startsWith('dir:')) {
      return 'folder';
    }
    if (trimmed.startsWith('ext:doc') || trimmed.startsWith('ext:pdf') || trimmed.startsWith('ext:txt')) {
      return 'doc';
    }
    if (trimmed.startsWith('ext:jpg') || trimmed.startsWith('ext:png')) {
      return 'image';
    }
    if (trimmed.startsWith('ext:mp4') || trimmed.startsWith('ext:mkv')) {
      return 'video';
    }
    if (trimmed.startsWith('ext:mp3') || trimmed.startsWith('ext:wav')) {
      return 'audio';
    }
    if (trimmed.startsWith('ext:zip') || trimmed.startsWith('ext:rar')) {
      return 'archive';
    }
    if (trimmed.startsWith('ext:cpp') || trimmed.startsWith('ext:ts') || trimmed.startsWith('ext:js')) {
      return 'code';
    }
    return 'all';
  }, [query]);

  const changeDensity = (newDensity: SearchDensity) => {
    setDensity(newDensity);
    localStorage.setItem('easytools_search_density', newDensity);
    inputRef.current?.focus();
  };

  useEffect(() => {
    const doFocus = () => {
      if (inputRef.current) {
        inputRef.current.focus({ preventScroll: true });
        inputRef.current.select();
      }
    };
    doFocus();
    const t1 = setTimeout(doFocus, 30);
    const t2 = setTimeout(doFocus, 100);
    const t3 = setTimeout(doFocus, 250);
    const t4 = setTimeout(doFocus, 500);

    const onFocusEvt = () => doFocus();
    window.addEventListener('easytools:focusSearch', onFocusEvt);
    window.addEventListener('focus', onFocusEvt);
    const onVisibilityChange = () => {
      if (!document.hidden) doFocus();
    };
    document.addEventListener('visibilitychange', onVisibilityChange);

    const handleGlobalKeyDown = (e: globalThis.KeyboardEvent) => {
      if (e.target !== inputRef.current && e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey) {
        inputRef.current?.focus();
      }
    };
    window.addEventListener('keydown', handleGlobalKeyDown);

    return () => {
      clearTimeout(t1);
      clearTimeout(t2);
      clearTimeout(t3);
      clearTimeout(t4);
      window.removeEventListener('easytools:focusSearch', onFocusEvt);
      window.removeEventListener('focus', onFocusEvt);
      document.removeEventListener('visibilitychange', onVisibilityChange);
      window.removeEventListener('keydown', handleGlobalKeyDown);
    };
  }, []);

  useEffect(() => {
    const trimmed = query.trim();
    if (!trimmed) return;

    const sequence = ++requestSequence.current;
    const isContentQuery = trimmed.toLowerCase().startsWith('content:') || 
      (trimmed.toLowerCase().startsWith('c:') && !trimmed.startsWith('c:\\') && !trimmed.startsWith('c:/')) || 
      trimmed.startsWith('内容:');
    const debounceMs = isContentQuery ? 350 : 25;

    const timer = window.setTimeout(async () => {
      const loadingTimer = window.setTimeout(() => {
        if (sequence === requestSequence.current) setLoading(true);
      }, 80);

      try {
        const response = await bridgeRequest<SearchResponse>('search.query', { query: trimmed });
        if (sequence !== requestSequence.current) return;
        window.clearTimeout(loadingTimer);
        startTransition(() => {
          setResults(Array.isArray(response.results) ? response.results : []);
          setSelectedIndex(0);
          if (response.available !== undefined) {
            setServiceAvailable(response.available);
          }
        });
      } catch {
        if (sequence !== requestSequence.current) return;
        window.clearTimeout(loadingTimer);
        startTransition(() => {
          setResults([]);
        });
      } finally {
        window.clearTimeout(loadingTimer);
        if (sequence === requestSequence.current) setLoading(false);
      }
    }, debounceMs);

    return () => window.clearTimeout(timer);
  }, [query]);

  // 点击外部关闭排序下拉
  useEffect(() => {
    const handleClickOutside = (e: MouseEvent) => {
      if (sortDropdownRef.current && !sortDropdownRef.current.contains(e.target as Node)) {
        setShowSortMenu(false);
      }
    };
    if (showSortMenu) {
      window.addEventListener('mousedown', handleClickOutside);
      return () => window.removeEventListener('mousedown', handleClickOutside);
    }
  }, [showSortMenu]);

  // 高性能前端虚拟内存排序 (0延迟，毫秒重排)
  const sortedResults = useMemo(() => {
    if (sortField === 'relevance' || results.length <= 1) {
      return results;
    }
    const list = [...results];
    list.sort((a, b) => {
      let cmp = 0;
      if (sortField === 'modified') {
        const timeA = Number(a.lastWriteTime) || 0;
        const timeB = Number(b.lastWriteTime) || 0;
        cmp = timeA - timeB;
      } else if (sortField === 'created') {
        const timeA = Number(a.creationTime) || 0;
        const timeB = Number(b.creationTime) || 0;
        cmp = timeA - timeB;
      } else if (sortField === 'size') {
        const sizeA = a.isDirectory ? -1 : (Number(a.size) || 0);
        const sizeB = b.isDirectory ? -1 : (Number(b.size) || 0);
        cmp = sizeA - sizeB;
      } else if (sortField === 'name') {
        cmp = a.name.localeCompare(b.name, 'zh-CN', { numeric: true, sensitivity: 'base' });
      }
      return sortDirection === 'asc' ? cmp : -cmp;
    });
    return list;
  }, [results, sortField, sortDirection]);

  const handleSelectSort = (field: SortField) => {
    if (field === 'relevance') {
      setSortField('relevance');
      localStorage.setItem('easytools_search_sort_field', 'relevance');
    } else if (sortField === field) {
      const nextDir = sortDirection === 'desc' ? 'asc' : 'desc';
      setSortDirection(nextDir);
      localStorage.setItem('easytools_search_sort_dir', nextDir);
    } else {
      setSortField(field);
      const defaultDir = (field === 'name' ? 'asc' : 'desc');
      setSortDirection(defaultDir);
      localStorage.setItem('easytools_search_sort_field', field);
      localStorage.setItem('easytools_search_sort_dir', defaultDir);
    }
    setSelectedIndex(0);
    setShowSortMenu(false);
    inputRef.current?.focus();
  };

  const updateQuery = (next: string) => {
    setQuery(next);
    setActionError('');

    // 搜索语法动态识别与排序联动
    const lower = next.toLowerCase();
    if (lower.includes('sort:date') || lower.includes('sort:mtime') || lower.includes('sort:time')) {
      setSortField('modified');
      setSortDirection('desc');
    } else if (lower.includes('sort:name')) {
      setSortField('name');
      setSortDirection('asc');
    } else if (lower.includes('sort:size')) {
      setSortField('size');
      setSortDirection('desc');
    } else if (lower.includes('sort:created')) {
      setSortField('created');
      setSortDirection('desc');
    }

    if (!next.trim()) {
      setResults([]);
      setSelectedIndex(0);
      setLoading(false);
    }
  };

  const selectCategory = (cat: CategoryFilter) => {
    if (!cat.prefix) {
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
      await bridgeRequest('system.openFile', { path: result.path });
      hide();
    } catch {
      setActionError(t('search.openFailed', '打开文件失败'));
    }
  }, [hide, t]);

  const openFolderResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      await bridgeRequest('system.openFolder', { path: result.path });
      hide();
    } catch {
      setActionError(t('search.openFolderFailed', '定位目录失败'));
    }
  }, [hide, t]);

  const copyPathResult = useCallback((result: SearchResult | undefined) => {
    if (!result) return;
    navigator.clipboard.writeText(result.path).then(() => {
      toast.success(t('search.copiedPath', '已复制完整路径到剪贴板'));
    }).catch(() => {
      setActionError(t('search.copyFailed', '复制路径失败'));
    });
  }, [t]);

  const pinResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    if (result.isDirectory || !IMAGE_EXTENSIONS.test(result.name)) {
      toast.error('当前仅支持对图片文件执行独立贴图');
      return;
    }
    try {
      await bridgeRequest('capture.pinImageFile', { path: result.path });
      hide();
    } catch {
      toast.error('贴图失败');
    }
  }, [hide]);

  const handleKeyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'F1') {
      event.preventDefault();
      setShowSyntaxHelp(prev => !prev);
      return;
    }

    if (event.ctrlKey && event.shiftKey) {
      const k = event.key.toLowerCase();
      if (k === 'd') {
        event.preventDefault();
        handleSelectSort('modified');
        return;
      }
      if (k === 'n') {
        event.preventDefault();
        handleSelectSort('name');
        return;
      }
      if (k === 's') {
        event.preventDefault();
        handleSelectSort('size');
        return;
      }
      if (k === 'r') {
        event.preventDefault();
        handleSelectSort('relevance');
        return;
      }
    }

    if (sortedResults.length === 0) {
      if (event.key === 'Escape') {
        if (showSortMenu) {
          setShowSortMenu(false);
        } else if (showSyntaxHelp) {
          setShowSyntaxHelp(false);
        } else if (showViewSettings) {
          setShowViewSettings(false);
        } else {
          hide();
        }
      }
      return;
    }

    if (event.key === 'ArrowDown') {
      event.preventDefault();
      setSelectedIndex((prev) => (prev + 1) % sortedResults.length);
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      setSelectedIndex((prev) => (prev - 1 + sortedResults.length) % sortedResults.length);
    } else if (event.key === 'PageDown') {
      event.preventDefault();
      setSelectedIndex((prev) => Math.min(prev + 8, sortedResults.length - 1));
    } else if (event.key === 'PageUp') {
      event.preventDefault();
      setSelectedIndex((prev) => Math.max(prev - 8, 0));
    } else if (event.key === 'Home') {
      event.preventDefault();
      setSelectedIndex(0);
    } else if (event.key === 'End') {
      event.preventDefault();
      setSelectedIndex(sortedResults.length - 1);
    } else if (event.key === 'Enter') {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (event.ctrlKey) {
        void openFolderResult(current);
      } else {
        void openResult(current);
      }
    } else if (event.key === 'Escape') {
      event.preventDefault();
      if (showSortMenu) {
        setShowSortMenu(false);
      } else if (showSyntaxHelp) {
        setShowSyntaxHelp(false);
      } else if (showViewSettings) {
        setShowViewSettings(false);
      } else {
        hide();
      }
    } else if (event.key.toLowerCase() === 'c' && event.ctrlKey) {
      if (sortedResults.length > 0 && selectedIndex >= 0 && selectedIndex < sortedResults.length) {
        event.preventDefault();
        copyPathResult(sortedResults[selectedIndex]);
      }
    } else if (event.key.toLowerCase() === 'p' && event.ctrlKey) {
      event.preventDefault();
      void pinResult(sortedResults[selectedIndex]);
    }
  };

  const isColVisible = (id: ColumnId) => columns.find(c => c.id === id)?.visible ?? true;
  const nameFlex = columns.find(c => c.id === 'name')?.flex ?? 35;
  const pathFlex = columns.find(c => c.id === 'path')?.flex ?? 45;

  return (
    <main className="search-app">
      <section className="search-container" aria-label={t('search.title', '快速文件搜索')}>
        <div className="search-input-wrapper">
          <Search className="search-icon" size={22} aria-hidden="true" />
          <input
            ref={inputRef}
            autoFocus
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
            className={`search-help-btn ${showViewSettings ? 'search-help-btn--active' : ''}`}
            onClick={() => {
              setShowViewSettings(prev => !prev);
              setShowSyntaxHelp(false);
            }}
            title="视图与列定制（窗口尺寸、列顺序、列显示与占比）"
            type="button"
          >
            <SlidersHorizontal size={18} />
          </button>

          <button
            className={`search-help-btn ${showSyntaxHelp ? 'search-help-btn--active' : ''}`}
            onClick={() => {
              setShowSyntaxHelp(prev => !prev);
              setShowViewSettings(false);
            }}
            title="搜索语法与表达式速查 (F1)"
            type="button"
          >
            <HelpCircle size={18} />
          </button>
        </div>

        <div className="search-categories-bar">
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

          <div className="search-bar-right-group">
            {/* ── 极客排序微胶囊 (带下拉选择与快捷键) ── */}
            <div className="search-sort-wrapper" ref={sortDropdownRef}>
              <button
                type="button"
                className={`sort-pill ${sortField !== 'relevance' ? 'sort-pill--active' : ''}`}
                onClick={() => setShowSortMenu(prev => !prev)}
                title="排序方式 (Ctrl+Shift+D 时间 / N 名称 / S 大小 / R 默认)"
              >
                {sortField === 'modified' ? <Clock size={12} /> :
                 sortField === 'name' ? <ArrowDownAZ size={12} /> :
                 sortField === 'size' ? <HardDrive size={12} /> :
                 sortField === 'created' ? <Calendar size={12} /> :
                 <Zap size={12} />}
                <span>
                  {sortField === 'relevance' ? '智能匹配' :
                   sortField === 'modified' ? `修改时间 ${sortDirection === 'desc' ? '↓' : '↑'}` :
                   sortField === 'name' ? `文件名 ${sortDirection === 'asc' ? '↓' : '↑'}` :
                   sortField === 'size' ? `大小 ${sortDirection === 'desc' ? '↓' : '↑'}` :
                   `创建时间 ${sortDirection === 'desc' ? '↓' : '↑'}`}
                </span>
                <ChevronDown size={11} className={`sort-chevron ${showSortMenu ? 'sort-chevron--open' : ''}`} />
              </button>

              {showSortMenu && (
                <div className="sort-dropdown-menu">
                  {SORT_OPTIONS.map(opt => {
                    const isSelected = sortField === opt.id;
                    return (
                      <button
                        key={opt.id}
                        type="button"
                        className={`sort-menu-item ${isSelected ? 'sort-menu-item--active' : ''}`}
                        onClick={() => handleSelectSort(opt.id)}
                      >
                        <div className="sort-item-left">
                          {opt.icon(13)}
                          <span>{opt.label}</span>
                        </div>
                        {isSelected && (
                          <span className="sort-item-dir">
                            {opt.id === 'relevance' ? '默认' : sortDirection === 'desc' ? '降序 ↓' : '升序 ↑'}
                          </span>
                        )}
                      </button>
                    );
                  })}
                </div>
              )}
            </div>

            {/* ── 布局密度切换器 ── */}
            <div className="search-density-toggle" role="group" aria-label="布局密度">
              <button
                className={`density-pill ${density === 'compact' ? 'density-pill--active' : ''}`}
                onClick={() => changeDensity('compact')}
                title="紧凑布局（高信息密度，单行横排）"
                type="button"
              >
                紧凑
              </button>
              <button
                className={`density-pill ${density === 'standard' ? 'density-pill--active' : ''}`}
                onClick={() => changeDensity('standard')}
                title="适中布局（双行清晰排版）"
                type="button"
              >
                适中
              </button>
              <button
                className={`density-pill ${density === 'comfortable' ? 'density-pill--active' : ''}`}
                onClick={() => changeDensity('comfortable')}
                title="宽松布局（大图标与大字号）"
                type="button"
              >
                宽松
              </button>
            </div>
          </div>
        </div>

        {showViewSettings && (
          <div className="search-view-settings-drawer">
            <div className="view-settings-header">
              <div className="view-settings-title">
                <SlidersHorizontal size={16} />
                <span>视图与列属性定制</span>
              </div>
              <button
                className="syntax-drawer-close"
                onClick={() => setShowViewSettings(false)}
                type="button"
              >
                <X size={16} />
              </button>
            </div>

            <div className="view-settings-content">
              <div className="view-settings-section">
                <div className="view-section-label">
                  <Maximize2 size={14} />
                  <span>窗口尺寸与宽窄档位 ({windowSize.width} × {windowSize.height})</span>
                </div>
                <div className="view-presets-grid">
                  {WINDOW_PRESETS.map(preset => (
                    <button
                      key={preset.id}
                      className={`view-preset-btn ${windowSize.width === preset.width && windowSize.height === preset.height ? 'view-preset-btn--active' : ''}`}
                      onClick={() => changeWindowSize(preset.width, preset.height)}
                      type="button"
                    >
                      {preset.label}
                    </button>
                  ))}
                </div>
              </div>

              <div className="view-settings-section">
                <div className="view-section-label">
                  <span>文件名与路径列宽占比 (文件名 {nameFlex}% : 路径 {pathFlex}%)</span>
                </div>
                <div className="view-slider-row">
                  <span className="slider-hint">窄</span>
                  <input
                    type="range"
                    min="20"
                    max="65"
                    value={nameFlex}
                    onChange={(e) => {
                      const val = parseInt(e.target.value, 10);
                      updateColumnFlex('name', val);
                      updateColumnFlex('path', 80 - val);
                    }}
                    className="view-ratio-slider"
                  />
                  <span className="slider-hint">宽</span>
                </div>
              </div>

              <div className="view-settings-section">
                <div className="view-section-header-row">
                  <div className="view-section-label">
                    <span>列显示开关与顺序编排</span>
                  </div>
                  <button
                    className="view-reset-btn"
                    onClick={resetColumns}
                    type="button"
                    title="恢复默认列与尺寸设置"
                  >
                    <RotateCcw size={12} />
                    <span>恢复默认</span>
                  </button>
                </div>

                <div className="view-columns-list">
                  {columns.map((col, idx) => (
                    <div key={col.id} className="view-column-item">
                      <label className="view-column-check">
                        <input
                          type="checkbox"
                          checked={col.visible}
                          onChange={() => toggleColumnVisibility(col.id)}
                        />
                        <span className="view-column-name">{col.label}</span>
                      </label>
                      <div className="view-column-actions">
                        <button
                          className="view-reorder-btn"
                          disabled={idx === 0}
                          onClick={() => moveColumn(idx, 'up')}
                          title="上移此列"
                          type="button"
                        >
                          <ArrowUp size={14} />
                        </button>
                        <button
                          className="view-reorder-btn"
                          disabled={idx === columns.length - 1}
                          onClick={() => moveColumn(idx, 'down')}
                          title="下移此列"
                          type="button"
                        >
                          <ArrowDown size={14} />
                        </button>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </div>
          </div>
        )}

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

        {serviceAvailable && query.trim() && !loading && sortedResults.length === 0 && (
          <div className="search-empty-container" role="status">
            <div className="search-empty-text">
              {query.trim().toLowerCase().startsWith('content:') || query.trim().startsWith('内容:')
                ? t('search.noContentResults', '未在文件内容中找到匹配文本')
                : t('search.noResults', '未找到匹配的同名文件')}
            </div>
            {!query.trim().toLowerCase().startsWith('content:') &&
             !(query.trim().toLowerCase().startsWith('c:') && !query.trim().toLowerCase().startsWith('c:\\') && !query.trim().toLowerCase().startsWith('c:/')) &&
             !query.trim().startsWith('内容:') && (
              <button
                className="search-switch-content-btn"
                onClick={() => {
                  const contentCat = CATEGORIES.find(c => c.id === 'content') || CATEGORIES[1];
                  selectCategory(contentCat);
                }}
                type="button"
              >
                <FileText size={15} />
                <span>立即穿透搜索文档与代码内容：「{query.trim()}」</span>
              </button>
            )}
          </div>
        )}

        {sortedResults.length > 0 && (
          <ul id="search-results" className={`search-results density-${density}`} role="listbox">
            {sortedResults.map((result, index) => (
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
                {renderFileIcon(result.name, result.isDirectory, density)}
                <div className="file-info">
                  {density === 'compact' ? (
                    <div className="file-row-main">
                      {isColVisible('name') && (
                        <span className="file-name" style={{ flex: `${nameFlex} 1 0` }} title={result.name}>
                          <span className="file-name-text">{result.name}</span>
                          {isColVisible('ext') && (
                            <span className={`file-ext-badge ${getFileTypeBadge(result.name, result.isDirectory).colorClass}`}>
                              {getFileTypeBadge(result.name, result.isDirectory).label}
                            </span>
                          )}
                        </span>
                      )}
                      {isColVisible('path') && (
                        <span className="file-path-inline" style={{ flex: `${pathFlex} 1 0` }} title={result.path}>
                          {isColVisible('parent') && extractParentFolder(result.path) && (
                            <span className="file-parent-tag" title={`所属上级目录: ${extractParentFolder(result.path)}`}>
                              📂 {extractParentFolder(result.path)}
                            </span>
                          )}
                          <span className="file-path-text">{result.path}</span>
                        </span>
                      )}
                      <div className="file-meta-top">
                        {isColVisible('size') && formatFileSize(result.size, result.isDirectory) ? (
                          <span className="meta-size-badge">{formatFileSize(result.size, result.isDirectory)}</span>
                        ) : null}
                        {isColVisible('modified') && result.lastWriteTime ? (
                          <span className="meta-date-mod" title="修改时间">
                            {formatWindowsTime(result.lastWriteTime)}
                          </span>
                        ) : null}
                        {isColVisible('created') && result.creationTime ? (
                          <span className="meta-date-create" title="创建时间">
                            {formatWindowsTime(result.creationTime)}
                          </span>
                        ) : null}
                      </div>
                    </div>
                  ) : (
                    <>
                      <div className="file-row-main">
                        {isColVisible('name') && (
                          <span className="file-name" title={result.name}>
                            <span className="file-name-text">{result.name}</span>
                            {isColVisible('ext') && (
                              <span className={`file-ext-badge ${getFileTypeBadge(result.name, result.isDirectory).colorClass}`}>
                                {getFileTypeBadge(result.name, result.isDirectory).label}
                              </span>
                            )}
                          </span>
                        )}
                        <div className="file-meta-top">
                          {isColVisible('size') && formatFileSize(result.size, result.isDirectory) ? (
                            <span className="meta-size-badge">{formatFileSize(result.size, result.isDirectory)}</span>
                          ) : null}
                          {isColVisible('modified') && result.lastWriteTime ? (
                            <span className="meta-date-mod" title="修改时间">
                              {formatWindowsTime(result.lastWriteTime)}
                            </span>
                          ) : null}
                        </div>
                      </div>
                      <div className="file-row-sub">
                        {isColVisible('path') && (
                          <div className="file-path-wrapper" title={result.path}>
                            {isColVisible('parent') && extractParentFolder(result.path) && (
                              <span className="file-parent-tag" title={`所属上级目录: ${extractParentFolder(result.path)}`}>
                                📂 {extractParentFolder(result.path)}
                              </span>
                            )}
                            <span className="file-path">{result.path}</span>
                          </div>
                        )}
                        {isColVisible('created') && result.creationTime ? (
                          <span className="meta-date-create" title="创建时间">
                            创建 {formatWindowsTime(result.creationTime)}
                          </span>
                        ) : null}
                      </div>
                    </>
                  )}

                  {isColVisible('snippets') && result.snippets && result.snippets.length > 0 && (
                    <div className="file-snippets-container">
                      {result.snippets.map((snip, sIdx) => (
                        <div key={sIdx} className="file-snippet-row">
                          <span className="snippet-line-num">L{snip.lineNumber}</span>
                          <span className="snippet-text">
                            {renderSnippetWithHighlight(snip.lineContent, snip.matchOffset, snip.matchLength)}
                          </span>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              </li>
            ))}
          </ul>
        )}

        <footer className="search-footer">
          <span className="search-hint"><kbd>Enter</kbd> {t('search.open', '打开')}</span>
          <span className="search-hint"><kbd>Ctrl+Enter</kbd> {t('search.openFolder', '定位目录')}</span>
          <span className="search-hint"><kbd>Ctrl+C</kbd> {t('search.copyPath', '复制路径')}</span>
          <span className="search-hint"><kbd>Ctrl+Shift+D</kbd> 排序</span>
          <span className="search-hint"><kbd>F1</kbd> 语法手册</span>
          <span className="search-hint"><kbd>Esc</kbd> {t('search.close', '关闭')}</span>
        </footer>

        <div
          className="search-resize-handle"
          onMouseDown={handleResizeMouseDown}
          title="按住鼠标拖拽拉伸窗口尺寸"
          aria-label="拖拽调整窗口大小"
        >
          <svg width="10" height="10" viewBox="0 0 10 10" fill="none" xmlns="http://www.w3.org/2000/svg">
            <line x1="8" y1="2" x2="2" y2="8" stroke="rgba(255,255,255,0.4)" strokeWidth="1.5" strokeLinecap="round" />
            <line x1="8" y1="5.5" x2="5.5" y2="8" stroke="rgba(255,255,255,0.4)" strokeWidth="1.5" strokeLinecap="round" />
          </svg>
        </div>
      </section>
    </main>
  );
}

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
  RotateCcw,
  Clock,
  ArrowDownAZ,
  Zap,
  HardDrive,
  Calendar,
  ChevronDown,
  Check,
  Tag,
  Network,
  Disc,
  FileSpreadsheet,
  RefreshCw,
  Plus,
  Trash2,
  Code2,
  Palette,
  Move,
  Info
} from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { bridgeRequest } from './hooks/useBridge';
import { useAppearance } from './hooks/useAppearance';
import './SearchApp.css';

export interface DriveInfo {
  letter: string;
  path: string;
  volumeLabel: string;
  fileSystem: string;
  type: 'fixed' | 'remote' | 'removable' | 'cdrom' | 'ramdisk' | 'unknown';
  totalBytes: number;
  freeBytes: number;
}

export type SearchMode = 'name' | 'both' | 'content';

export interface ContentSnippet {
  lineNumber: number;
  lineContent: string;
  matchOffset: number;
  matchLength: number;
}

export interface SearchResult {
  name: string;
  path: string;
  isDirectory: boolean;
  size?: number;
  creationTime?: number;
  lastWriteTime?: number;
  snippets?: ContentSnippet[];
  runCount?: number;
  frecencyScore?: number;
}

export interface SearchResponse {
  results: SearchResult[];
  available: boolean;
  totalIndexedFiles?: number;
  elapsedMs?: number;
  error?: string;
}

function formatBytes(bytes: number): string {
  if (!bytes || bytes <= 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return `${(bytes / Math.pow(1024, i)).toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
}

function highlightMatch(text: string, queryKeywords: string[]): React.ReactNode {
  if (!text || queryKeywords.length === 0) return text;

  const validKeywords = queryKeywords
    .map(k => k.trim())
    .filter(k => k.length > 0)
    .sort((a, b) => b.length - a.length);

  if (validKeywords.length === 0) return text;

  const escaped = validKeywords
    .map(k => k.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))
    .join('|');

  if (!escaped) return text;

  try {
    const regex = new RegExp(`(${escaped})`, 'gi');
    const parts = text.split(regex);
    return parts.map((part, idx) => {
      const isMatch = validKeywords.some(k => k.toLowerCase() === part.toLowerCase());
      if (isMatch) {
        return (
          <mark key={idx} className="search-match-highlight">
            {part}
          </mark>
        );
      }
      return part;
    });
  } catch {
    return text;
  }
}



export type SearchDensity = 'compact' | 'standard' | 'comfortable';
export type SortField = 'relevance' | 'modified' | 'name' | 'size' | 'created';
export type SortDirection = 'asc' | 'desc';

export type ColumnId = 'name' | 'ext' | 'parent' | 'path' | 'size' | 'modified' | 'created' | 'snippets';

export interface ColumnSetting {
  id: ColumnId;
  label: string;
  visible: boolean;
  flex?: number;
}

export interface WindowPreset {
  id: string;
  label: string;
  width: number;
  height: number;
}

export interface CategoryFilter {
  id: string;
  label: string;
  prefix: string;
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

const WINDOW_PRESETS: WindowPreset[] = [
  { id: 'standard', label: '标准 (800×600)', width: 800, height: 600 },
  { id: 'wide', label: '宽屏 (1000×650)', width: 1000, height: 650 },
  { id: 'large', label: '大屏 (1200×750)', width: 1200, height: 750 },
  { id: 'extra', label: '超宽 (1400×800)', width: 1400, height: 800 },
];

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
  { syntax: 'c: *.dll', desc: '限定在 C盘检索' },
  { syntax: 'regex:^app_\\d+\\.log$', desc: '正则表达式检索' },
  { syntax: 'case:EasyTools', desc: '区分大小写搜索' },
  { syntax: 'pinyin:wx', desc: '显式拼音首字母/全拼检索' },
];

interface FormatCategory {
  id: string;
  nameKey: 'search.catCode' | 'search.catDocs' | 'search.catConfig' | 'search.catDesign';
  iconName: 'code' | 'doc' | 'config' | 'design';
  extensions: string[];
}

const CONTENT_FORMAT_CATEGORIES: FormatCategory[] = [
  {
    id: 'code',
    nameKey: 'search.catCode',
    iconName: 'code',
    extensions: ['cpp', 'c', 'h', 'hpp', 'cs', 'java', 'py', 'js', 'jsx', 'ts', 'tsx', 'vue', 'svelte', 'rs', 'go', 'swift', 'kt', 'dart', 'lua', 'sql', 'sh', 'bat', 'ps1', 'php', 'rb', 'asm', 'glsl', 'hlsl']
  },
  {
    id: 'docs',
    nameKey: 'search.catDocs',
    iconName: 'doc',
    extensions: ['docx', 'xlsx', 'pptx', 'pdf', 'txt', 'md', 'csv', 'tsv', 'wps', 'et', 'dps', 'tex', 'diff', 'patch']
  },
  {
    id: 'config',
    nameKey: 'search.catConfig',
    iconName: 'config',
    extensions: ['json', 'jsonc', 'json5', 'yaml', 'yml', 'xml', 'toml', 'ini', 'cfg', 'conf', 'config', 'env', 'reg', 'properties']
  },
  {
    id: 'design',
    nameKey: 'search.catDesign',
    iconName: 'design',
    extensions: ['dxf', 'psd', 'ai', 'cdr', 'xmind']
  }
];

const IMAGE_EXTENSIONS = /\.(png|jpe?g|webp|bmp|gif|svg|ico)$/i;

function extractParentFolder(fullPath: string): string {
  if (!fullPath) return '';
  const clean = fullPath.replace(/[/\\]+$/, '');
  const lastSlash = Math.max(clean.lastIndexOf('/'), clean.lastIndexOf('\\'));
  if (lastSlash <= 0) return '';
  const parentPath = clean.slice(0, lastSlash);
  const secondLastSlash = Math.max(parentPath.lastIndexOf('/'), parentPath.lastIndexOf('\\'));
  return secondLastSlash >= 0 ? parentPath.slice(secondLastSlash + 1) : parentPath;
}

function getFileTypeBadge(name: string, isDirectory: boolean): { label: string; colorClass: string } {
  if (isDirectory) {
    return { label: 'DIR', colorClass: 'badge-folder' };
  }
  const dot = name.lastIndexOf('.');
  if (dot === -1) {
    return { label: 'FILE', colorClass: 'badge-generic' };
  }
  const ext = name.slice(dot + 1).toUpperCase();
  const lowerExt = ext.toLowerCase();

  if (['C', 'CPP', 'H', 'HPP', 'RS', 'GO'].includes(ext)) {
    return { label: `.${lowerExt}`, colorClass: 'badge-code-c' };
  }
  if (['JS', 'TS', 'TSX', 'JSX', 'VUE', 'HTML', 'CSS', 'SCSS'].includes(ext)) {
    return { label: `.${lowerExt}`, colorClass: 'badge-code-js' };
  }
  if (['PY', 'JAVA', 'CS', 'SH', 'BAT', 'PS1', 'LUA', 'SQL'].includes(ext)) {
    return { label: `.${lowerExt}`, colorClass: 'badge-code-other' };
  }
  if (['JSON', 'YAML', 'YML', 'TOML', 'XML', 'INI', 'CONF'].includes(ext)) {
    return { label: `.${lowerExt}`, colorClass: 'badge-config' };
  }
  if (['PNG', 'JPG', 'JPEG', 'WEBP', 'SVG', 'GIF', 'ICO', 'BMP'].includes(ext)) {
    return { label: ext, colorClass: 'badge-img' };
  }
  if (['ZIP', '7Z', 'RAR', 'TAR', 'GZ', 'BZ2'].includes(ext)) {
    return { label: ext, colorClass: 'badge-archive' };
  }
  if (['PDF', 'DOC', 'DOCX', 'XLS', 'XLSX', 'PPT', 'PPTX', 'TXT', 'MD'].includes(ext)) {
    return { label: ext, colorClass: 'badge-doc' };
  }
  if (['MP4', 'MKV', 'AVI', 'MOV', 'MP3', 'WAV', 'FLAC'].includes(ext)) {
    return { label: ext, colorClass: 'badge-media' };
  }
  if (['EXE', 'DLL', 'SYS', 'SO', 'BIN'].includes(ext)) {
    return { label: ext, colorClass: 'badge-bin' };
  }
  return { label: `.${lowerExt.slice(0, 5)}`, colorClass: 'badge-generic' };
}

function renderSnippetWithHighlight(text: string, offset?: number, length?: number) {
  if (offset === undefined || length === undefined || offset < 0 || length <= 0 || offset >= text.length) {
    return text;
  }
  const before = text.substring(0, offset);
  const match = text.substring(offset, offset + length);
  const after = text.substring(offset + length);
  return (
    <>
      {before}
      <mark className="snippet-highlight">{match}</mark>
      {after}
    </>
  );
}

function formatFileSize(bytes?: number, isDirectory?: boolean): string {
  if (isDirectory) return '';
  if (bytes === undefined || bytes === null || bytes === 0) return '0 B';
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
  if (!num) return '';
  const ms = Math.floor((num - 116444736000000000) / 10000);
  if (ms <= 0) return '';
  const date = new Date(ms);
  if (isNaN(date.getTime())) return '';
  
  const pad = (n: number) => (n < 10 ? '0' + n : String(n));
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

function renderFileIcon(name: string, isDirectory: boolean, density: SearchDensity = 'standard') {
  const iconSize = density === 'compact' ? 14 : density === 'comfortable' ? 22 : 18;
  if (isDirectory) {
    return <Folder className="file-icon file-icon--folder" size={iconSize} aria-hidden="true" />;
  }
  const ext = name.toLowerCase();
  if (IMAGE_EXTENSIONS.test(ext)) {
    return <FileImage className="file-icon file-icon--image" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(cpp|c|h|hpp|rs|go|py|js|ts|tsx|jsx|java|cs|html|css|json|yaml|toml|sql|sh|bat|ps1|lua)$/i.test(ext)) {
    return <FileCode className="file-icon file-icon--code" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(zip|rar|7z|tar|gz|bz2|xz|iso)$/i.test(ext)) {
    return <FileArchive className="file-icon file-icon--archive" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(exe|msi|bat|cmd|ps1|lnk)$/i.test(ext)) {
    return <AppWindow className="file-icon file-icon--app" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(mp4|mkv|avi|mov|wmv|flv|webm)$/i.test(ext)) {
    return <FileVideo className="file-icon file-icon--video" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(mp3|wav|flac|aac|ogg|m4a|wma)$/i.test(ext)) {
    return <FileAudio className="file-icon file-icon--audio" size={iconSize} aria-hidden="true" />;
  }
  if (/\.(txt|md|pdf|doc|docx|xls|xlsx|ppt|pptx|log|csv)$/i.test(ext)) {
    return <FileText className="file-icon file-icon--doc" size={iconSize} aria-hidden="true" />;
  }
  return <File className="file-icon" size={iconSize} aria-hidden="true" />;
}

function getSortedResults(
  results: SearchResult[],
  sortField: SortField,
  sortDirection: SortDirection,
  foldersFirst: boolean,
  groupByType: boolean
): SearchResult[] {
  if (results.length <= 1) return results;
  return [...results].sort((a, b) => {
    // 1. 组合规则第 1 级: 文件夹置顶
    if (foldersFirst && a.isDirectory !== b.isDirectory) {
      return a.isDirectory ? -1 : 1;
    }

    // 2. 组合规则第 2 级: 按扩展名/类型分组
    if (groupByType && !a.isDirectory && !b.isDirectory) {
      const extA = a.name.includes('.') ? a.name.slice(a.name.lastIndexOf('.')).toLowerCase() : '';
      const extB = b.name.includes('.') ? b.name.slice(b.name.lastIndexOf('.')).toLowerCase() : '';
      const extCmp = extA.localeCompare(extB);
      if (extCmp !== 0) return extCmp;
    }

    // 3. 业务主排序列
    let cmp = 0;
    if (sortField === 'relevance') {
      const scoreA = Number(a.frecencyScore) || 0;
      const scoreB = Number(b.frecencyScore) || 0;
      cmp = scoreA - scoreB;
    } else if (sortField === 'modified') {
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

    if (cmp !== 0) {
      return sortDirection === 'asc' ? cmp : -cmp;
    }

    // 4. 稳定性兜底: 按文件名 A-Z 保证顺序稳定
    return a.name.localeCompare(b.name, 'zh-CN', { numeric: true, sensitivity: 'base' });
  });
}

// ── 组件主入口 ──────────────────────────────────────────────────
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
  const [maxResultLimit, setMaxResultLimit] = useState<number>(() => {
    const saved = localStorage.getItem('easytools_search_max_results');
    return saved !== null ? parseInt(saved, 10) : 100;
  });

  const changeMaxResultLimit = (limit: number) => {
    setMaxResultLimit(limit);
    localStorage.setItem('easytools_search_max_results', String(limit));
  };

  const [searchHistory, setSearchHistory] = useState<{ search: string; searchCount: number; lastSearchDate: number }[]>([]);
  const [dbStats, setDbStats] = useState<{
    dbPath: string;
    dbSize: number;
    timestamp: number;
    totalRecords: number;
    volumeCount: number;
    exists: boolean;
    runHistoryCount: number;
    searchHistoryCount: number;
  } | null>(null);

  const refreshHistory = useCallback(async () => {
    try {
      const res = await bridgeRequest<{ success: boolean; history: { search: string; searchCount: number; lastSearchDate: number }[] }>('search.getSearchHistory', { limit: 20 });
      if (res && res.success && Array.isArray(res.history)) {
        setSearchHistory(res.history);
      }
    } catch {
      // ignore
    }
  }, []);

  const refreshDbStats = useCallback(async () => {
    try {
      const res = await bridgeRequest<{
        dbPath: string;
        dbSize: number;
        timestamp: number;
        totalRecords: number;
        volumeCount: number;
        exists: boolean;
        runHistoryCount: number;
        searchHistoryCount: number;
        success: boolean;
      }>('search.getDbStats');
      if (res && res.success) {
        setDbStats(res);
      }
    } catch {
      // ignore
    }
  }, []);

  useEffect(() => {
    let active = true;
    void bridgeRequest<{ success: boolean; history: { search: string; searchCount: number; lastSearchDate: number }[] }>('search.getSearchHistory', { limit: 20 })
      .then((res) => {
        if (active && res && res.success && Array.isArray(res.history)) {
          setSearchHistory(res.history);
        }
      })
      .catch(() => undefined);

    void bridgeRequest<{
      dbPath: string;
      dbSize: number;
      timestamp: number;
      totalRecords: number;
      volumeCount: number;
      exists: boolean;
      runHistoryCount: number;
      searchHistoryCount: number;
      success: boolean;
    }>('search.getDbStats')
      .then((res) => {
        if (active && res && res.success) {
          setDbStats(res);
        }
      })
      .catch(() => undefined);

    return () => {
      active = false;
    };
  }, []);

  const removeHistoryItem = async (e: React.MouseEvent, s: string) => {
    e.stopPropagation();
    await bridgeRequest('search.removeSearchHistory', { search: s });
    setSearchHistory(prev => prev.filter(item => item.search !== s));
  };

  const clearAllHistory = async () => {
    await bridgeRequest('search.clearSearchHistory');
    setSearchHistory([]);
    toast.success('已清空所有搜索历史');
    void refreshDbStats();
    void refreshHistory();
  };

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

  const [foldersFirst, setFoldersFirst] = useState<boolean>(() => {
    return localStorage.getItem('easytools_search_sort_folders_first') !== 'false';
  });

  const [groupByType, setGroupByType] = useState<boolean>(() => {
    return localStorage.getItem('easytools_search_sort_group_type') === 'true';
  });

  const [showSortMenu, setShowSortMenu] = useState(false);
  const sortDropdownRef = useRef<HTMLDivElement>(null);
  const viewSettingsRef = useRef<HTMLDivElement>(null);

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

  const [systemDrives, setSystemDrives] = useState<DriveInfo[]>([]);
  const [enabledDrives, setEnabledDrives] = useState<string[]>(() => {
    try {
      const saved = localStorage.getItem('easytools_search_enabled_drives');
      if (saved) {
        const parsed = JSON.parse(saved);
        if (Array.isArray(parsed)) return parsed;
      }
    } catch {
      // fallback
    }
    return [];
  });

  useEffect(() => {
    void bridgeRequest<DriveInfo[]>('search.getDrives')
      .then((drives) => {
        if (Array.isArray(drives) && drives.length > 0) {
          setSystemDrives(drives);
          setEnabledDrives((prev) => {
            if (prev.length === 0) {
              const allLetters = drives.map((d) => d.letter);
              localStorage.setItem('easytools_search_enabled_drives', JSON.stringify(allLetters));
              return allLetters;
            }
            return prev;
          });
        }
      })
      .catch(() => undefined);
  }, []);

  const toggleDrive = (letter: string) => {
    setEnabledDrives((prev) => {
      let next: string[];
      if (prev.includes(letter)) {
        next = prev.filter((l) => l !== letter);
      } else {
        next = [...prev, letter];
      }
      localStorage.setItem('easytools_search_enabled_drives', JSON.stringify(next));
      void bridgeRequest('search.saveSettings', { enabledDrives: next.join(',') }).catch(() => undefined);
      return next;
    });
  };

  const selectAllDrives = () => {
    const all = systemDrives.map((d) => d.letter);
    setEnabledDrives(all);
    localStorage.setItem('easytools_search_enabled_drives', JSON.stringify(all));
    void bridgeRequest('search.saveSettings', { enabledDrives: all.join(',') }).catch(() => undefined);
  };

  const deselectAllDrives = () => {
    setEnabledDrives([]);
    localStorage.setItem('easytools_search_enabled_drives', JSON.stringify([]));
    void bridgeRequest('search.saveSettings', { enabledDrives: '' }).catch(() => undefined);
  };

  const [excludeGitAndModules, setExcludeGitAndModules] = useState<boolean>(() => {
    return localStorage.getItem('easytools_search_exclude_dev') !== 'false';
  });
  const [excludeHidden, setExcludeHidden] = useState<boolean>(() => {
    return localStorage.getItem('easytools_search_exclude_hidden') === 'true';
  });
  const [isRebuilding, setIsRebuilding] = useState(false);

  // 默认搜索范围模式: 'name' (仅文件名·极速) | 'both' (文件名+内容双搜) | 'content' (仅文件内容)
  const [searchMode, setSearchMode] = useState<SearchMode>(() => {
    return (localStorage.getItem('easytools_search_default_mode') as SearchMode) || 'name';
  });
  const [totalIndexedFiles, setTotalIndexedFiles] = useState<number>(0);
  const [searchElapsedMs, setSearchElapsedMs] = useState<number>(0);

  const changeSearchMode = (mode: SearchMode) => {
    setSearchMode(mode);
    localStorage.setItem('easytools_search_default_mode', mode);
  };

  useEffect(() => {
    void bridgeRequest<SearchResponse>('search.query', { query: '' }).then((res) => {
      if (res && res.totalIndexedFiles) {
        setTotalIndexedFiles(res.totalIndexedFiles);
      }
    }).catch(() => undefined);
  }, []);

  const [disabledContentFormats, setDisabledContentFormats] = useState<string[]>(() => {
    try {
      const saved = localStorage.getItem('easytools_search_disabled_formats');
      if (saved) return JSON.parse(saved);
    } catch {
      /* ignore */
    }
    return [];
  });

  const [customContentFormats, setCustomContentFormats] = useState<string[]>(() => {
    try {
      const saved = localStorage.getItem('easytools_search_custom_formats');
      if (saved) return JSON.parse(saved);
    } catch {
      /* ignore */
    }
    return [];
  });

  const [newFormatInput, setNewFormatInput] = useState('');

  const toggleContentFormat = (ext: string) => {
    const cleanExt = ext.toLowerCase().replace(/^\./, '').trim();
    setDisabledContentFormats(prev => {
      let next: string[];
      if (prev.includes(cleanExt)) {
        next = prev.filter(e => e !== cleanExt);
      } else {
        next = [...prev, cleanExt];
      }
      localStorage.setItem('easytools_search_disabled_formats', JSON.stringify(next));
      return next;
    });
  };

  const toggleCategoryFormats = (cat: FormatCategory, enableAll: boolean) => {
    setDisabledContentFormats(prev => {
      let next = [...prev];
      if (enableAll) {
        next = next.filter(e => !cat.extensions.includes(e));
      } else {
        for (const ext of cat.extensions) {
          if (!next.includes(ext)) next.push(ext);
        }
      }
      localStorage.setItem('easytools_search_disabled_formats', JSON.stringify(next));
      return next;
    });
  };

  const addCustomContentFormat = (rawInput: string) => {
    const parts = rawInput.split(/[,;\s]+/).map(p => p.toLowerCase().replace(/^\./, '').trim()).filter(Boolean);
    if (parts.length === 0) return;
    setCustomContentFormats(prev => {
      const next = [...prev];
      for (const p of parts) {
        if (!next.includes(p)) next.push(p);
      }
      localStorage.setItem('easytools_search_custom_formats', JSON.stringify(next));
      return next;
    });
    setDisabledContentFormats(prev => {
      const next = prev.filter(e => !parts.includes(e));
      localStorage.setItem('easytools_search_disabled_formats', JSON.stringify(next));
      return next;
    });
    setNewFormatInput('');
    toast.success(`已添加 ${parts.join(', ')} 到文档内容搜索支持列表`);
  };

  const removeCustomContentFormat = (ext: string) => {
    const cleanExt = ext.toLowerCase().replace(/^\./, '').trim();
    setCustomContentFormats(prev => {
      const next = prev.filter(e => e !== cleanExt);
      localStorage.setItem('easytools_search_custom_formats', JSON.stringify(next));
      return next;
    });
    setDisabledContentFormats(prev => {
      const next = prev.filter(e => e !== cleanExt);
      localStorage.setItem('easytools_search_disabled_formats', JSON.stringify(next));
      return next;
    });
  };

  const resetContentFormats = () => {
    setDisabledContentFormats([]);
    setCustomContentFormats([]);
    localStorage.removeItem('easytools_search_disabled_formats');
    localStorage.removeItem('easytools_search_custom_formats');
    toast.success('已恢复文档内容检索默认格式配置');
  };

  const rebuildIndex = useCallback(async () => {
    setIsRebuilding(true);
    toast.loading('正在全盘重新扫描 NTFS 索引并保存快照...', { id: 'rebuild-idx' });
    try {
      await bridgeRequest('search.rebuildIndex');
      toast.success('全盘索引已重新扫描并同步保存至 EasyTools.db 快照！', { id: 'rebuild-idx' });
      void refreshDbStats();
      void refreshHistory();
    } catch {
      toast.error('索引重建与快照保存失败', { id: 'rebuild-idx' });
    } finally {
      setTimeout(() => setIsRebuilding(false), 1500);
    }
  }, [refreshDbStats, refreshHistory]);

  const queryKeywords = useMemo(() => {
    const trimmed = query.trim();
    if (!trimmed) return [];
    const tokens = trimmed.split(/[\s|]+/).filter(Boolean);
    const keywords: string[] = [];
    for (const token of tokens) {
      let clean = token.replace(/^!/, '');
      const colonPos = clean.indexOf(':');
      if (colonPos !== -1 && colonPos < 8) {
        clean = clean.substring(colonPos + 1);
      }
      clean = clean.replace(/[*?"]/g, '').trim();
      if (clean.length > 0 && !keywords.includes(clean)) {
        keywords.push(clean);
      }
    }
    return keywords;
  }, [query]);

  const exportResultsToCsv = () => {
    if (sortedResults.length === 0) {
      toast.error('当前无搜索结果可导出');
      return;
    }

    const headers = ['文件名', '完整路径', '类型', '大小(字节)', '大小(易读)', '修改时间', '创建时间'];
    const rows = sortedResults.map((item) => {
      const isDir = item.isDirectory;
      const typeStr = isDir ? '文件夹' : (item.name.includes('.') ? item.name.split('.').pop()?.toUpperCase() || '文件' : '文件');
      const sizeBytes = item.size ?? 0;
      const sizeFormatted = isDir ? '-' : formatFileSize(sizeBytes, isDir);
      const modTime = item.lastWriteTime ? formatWindowsTime(item.lastWriteTime) : '-';
      const createTime = item.creationTime ? formatWindowsTime(item.creationTime) : '-';

      const escapeCsv = (str: string) => `"${str.replace(/"/g, '""')}"`;

      return [
        escapeCsv(item.name),
        escapeCsv(item.path),
        escapeCsv(typeStr),
        sizeBytes,
        escapeCsv(sizeFormatted),
        escapeCsv(modTime),
        escapeCsv(createTime)
      ].join(',');
    });

    const csvContent = '\uFEFF' + [headers.join(','), ...rows].join('\r\n');
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.setAttribute('href', url);
    const nowStr = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
    const queryTag = query.trim() ? `_${query.trim().slice(0, 20).replace(/[/\\:*?"<>|]/g, '_')}` : '';
    link.setAttribute('download', `EasyTools_Search_Export${queryTag}_${nowStr}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);

    toast.success(`已成功导出 ${sortedResults.length} 条搜索结果为 CSV 文件`);
  };

  useEffect(() => {
    void bridgeRequest<{ width?: number; height?: number }>('search.getWindowSize')
      .then((res) => {
        if (res?.width && res?.height) {
          setWindowSize({ width: res.width, height: res.height });
        }
      })
      .catch(() => undefined);
  }, []);

  const changeWindowSize = (width: number, height: number, center = true) => {
    setWindowSize({ width, height });
    void bridgeRequest('search.setWindowSize', { width, height, center }).catch(() => undefined);
  };

  const handleResizeMouseDown = (e: React.MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();

    const startScreenX = e.screenX;
    const startScreenY = e.screenY;
    const startW = windowSize.width;
    const startH = windowSize.height;

    let currentW = startW;
    let currentH = startH;
    let rafId = 0;

    const onMouseMove = (moveEvt: MouseEvent) => {
      const deltaX = moveEvt.screenX - startScreenX;
      const deltaY = moveEvt.screenY - startScreenY;
      const newW = Math.max(500, Math.min(2200, Math.round(startW + deltaX)));
      const newH = Math.max(400, Math.min(1400, Math.round(startH + deltaY)));

      if (newW !== currentW || newH !== currentH) {
        currentW = newW;
        currentH = newH;
        setWindowSize({ width: newW, height: newH });
        cancelAnimationFrame(rafId);
        rafId = requestAnimationFrame(() => {
          void bridgeRequest('search.setWindowSize', { width: currentW, height: currentH, center: false }).catch(() => undefined);
        });
      }
    };

    const onMouseUp = () => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
      cancelAnimationFrame(rafId);
      void bridgeRequest('search.setWindowSize', { width: currentW, height: currentH, center: false }).catch(() => undefined);
    };

    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
  };

  const toggleColumnVisibility = (id: ColumnId) => {
    const updated = columns.map(col => col.id === id ? { ...col, visible: !col.visible } : col);
    setColumns(updated);
    localStorage.setItem('easytools_search_columns_v2', JSON.stringify(updated));
  };

  const updateNameAndPathFlex = (nameVal: number) => {
    const pathVal = Math.max(15, 80 - nameVal);
    const updated = columns.map(col => {
      if (col.id === 'name') return { ...col, flex: nameVal };
      if (col.id === 'path') return { ...col, flex: pathVal };
      return col;
    });
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

    const onFocusEvt = () => {
      doFocus();
      void bridgeRequest('search.sync');
    };
    window.addEventListener('easytools:focusSearch', onFocusEvt);
    window.addEventListener('focus', onFocusEvt);
    const onVisibilityChange = () => {
      if (!document.hidden) {
        doFocus();
        void bridgeRequest('search.sync');
      }
    };
    document.addEventListener('visibilitychange', onVisibilityChange);

    const handleGlobalKeyDown = (e: globalThis.KeyboardEvent) => {
      if (e.key === 'F5' || (e.ctrlKey && (e.key === 'r' || e.key === 'R'))) {
        e.preventDefault();
        void rebuildIndex();
        return;
      }
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
        return;
      }
      if (e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey) {
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
  }, [rebuildIndex]);

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

      const excludesList: string[] = [];
      if (excludeGitAndModules) {
        excludesList.push('$Recycle.Bin', 'System Volume Information', 'node_modules', '.git', '__pycache__', 'npm-cache', 'go-build', '.gradle', 'pip\\cache');
      }

      const effectiveMode = (activeCategory === 'content') ? 'content' : searchMode;

      try {
        const response = await bridgeRequest<SearchResponse>('search.query', { 
          query: trimmed,
          searchMode: effectiveMode,
          limit: maxResultLimit,
          drives: enabledDrives.length > 0 ? enabledDrives : undefined,
          excludes: excludesList.length > 0 ? excludesList : undefined,
          excludeHidden: excludeHidden,
          contentCustomExts: customContentFormats.length > 0 ? customContentFormats : undefined,
          contentDisabledExts: disabledContentFormats.length > 0 ? disabledContentFormats : undefined
        });
        if (sequence !== requestSequence.current) return;
        window.clearTimeout(loadingTimer);
        startTransition(() => {
          setResults(Array.isArray(response.results) ? response.results : []);
          setSelectedIndex(0);
          if (response.totalIndexedFiles !== undefined) {
            setTotalIndexedFiles(response.totalIndexedFiles);
          }
          if (response.elapsedMs !== undefined) {
            setSearchElapsedMs(response.elapsedMs);
          }
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
  }, [query, activeCategory, searchMode, maxResultLimit, enabledDrives, excludeGitAndModules, excludeHidden, customContentFormats, disabledContentFormats]);

  // 组合排序开关
  const toggleFoldersFirst = () => {
    setFoldersFirst(prev => {
      const next = !prev;
      localStorage.setItem('easytools_search_sort_folders_first', String(next));
      return next;
    });
  };

  const toggleGroupByType = () => {
    setGroupByType(prev => {
      const next = !prev;
      localStorage.setItem('easytools_search_sort_group_type', String(next));
      return next;
    });
  };

  // 点击外部关闭排序下拉与视图设置浮层
  useEffect(() => {
    const handleClickOutside = (e: MouseEvent) => {
      const target = e.target as Node;
      if (sortDropdownRef.current && !sortDropdownRef.current.contains(target)) {
        setShowSortMenu(false);
      }
      if (viewSettingsRef.current && !viewSettingsRef.current.contains(target)) {
        const helpBtn = document.querySelector('.search-help-btn');
        if (!helpBtn || !helpBtn.contains(target)) {
          setShowViewSettings(false);
        }
      }
    };
    if (showSortMenu || showViewSettings) {
      window.addEventListener('mousedown', handleClickOutside);
      return () => window.removeEventListener('mousedown', handleClickOutside);
    }
  }, [showSortMenu, showViewSettings]);

  // 高性能多级组合排序管道 (Multi-level Pipeline Sorting)
  // eslint-disable-next-line react-hooks/preserve-manual-memoization
  const sortedResults = useMemo(() => {
    return getSortedResults(results, sortField, sortDirection, foldersFirst, groupByType);
  }, [results, sortField, sortDirection, foldersFirst, groupByType]);

  const totalResultSize = useMemo(() => {
    return sortedResults.reduce((acc, item) => acc + (item.isDirectory ? 0 : (Number(item.size) || 0)), 0);
  }, [sortedResults]);

  const handleSetSortDirect = (field: SortField, dir: SortDirection) => {
    setSortField(field);
    setSortDirection(dir);
    localStorage.setItem('easytools_search_sort_field', field);
    localStorage.setItem('easytools_search_sort_dir', dir);
    setSelectedIndex(0);
    setShowSortMenu(false);
    inputRef.current?.focus();
  };

  const toggleSortDirection = (e: React.MouseEvent) => {
    e.stopPropagation();
    if (sortField === 'relevance') {
      setSortField('modified');
      setSortDirection('desc');
      localStorage.setItem('easytools_search_sort_field', 'modified');
      localStorage.setItem('easytools_search_sort_dir', 'desc');
    } else {
      const nextDir = sortDirection === 'desc' ? 'asc' : 'desc';
      setSortDirection(nextDir);
      localStorage.setItem('easytools_search_sort_dir', nextDir);
    }
    setSelectedIndex(0);
    inputRef.current?.focus();
  };

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
    } else if (lower.includes('sort:folder') || lower.includes('sort:dir')) {
      setFoldersFirst(true);
    } else if (lower.includes('sort:ext') || lower.includes('sort:type')) {
      setGroupByType(true);
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
      void bridgeRequest('search.recordRun', { path: result.path });
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
      void bridgeRequest('search.recordRun', { path: result.path });
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
    if (event.key === 'F5' || (event.ctrlKey && (event.key === 'r' || event.key === 'R'))) {
      event.preventDefault();
      void rebuildIndex();
      return;
    }

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
    } else if (event.key.toLowerCase() === 'e' && event.ctrlKey) {
      event.preventDefault();
      exportResultsToCsv();
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
        <div 
          className="search-input-wrapper"
          onMouseDown={(e) => {
            if (e.target === e.currentTarget) {
              void bridgeRequest('search.startDrag');
            }
          }}
        >
          <Search className="search-icon" size={22} aria-hidden="true" />
          <input
            ref={inputRef}
            autoFocus
            className="search-input"
            placeholder={
              activeCategory === 'content'
                ? '搜索文档与代码全文内容 (Word/Excel/PDF/代码/文本)... [F1 语法]'
                : searchMode === 'both'
                ? '智能双搜：同时匹配文件名与文档全文内容... [F1 语法]'
                : searchMode === 'content'
                ? '全文检索：搜索文档与代码全文内容... [F1 语法]'
                : '搜索文件名、通配符 (*.txt)、扩展名 (ext:png) 或拼音... [输入 content: 搜内容]'
            }
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
            className="search-help-btn search-drag-btn"
            onMouseDown={() => void bridgeRequest('search.startDrag')}
            onDoubleClick={() => {
              void bridgeRequest('search.resetPlacement');
              setWindowSize({ width: 760, height: 520 });
              toast.success('已恢复默认居中与 760×520 尺寸');
            }}
            title="按住拖拽移动窗口位置 · 双击居中复位"
            type="button"
          >
            <Move size={17} />
          </button>

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
            {/* 全盘已索引总数与耗时统计胶囊 */}
            <div 
              className="search-stats-pill" 
              title={`全盘共索引 ${totalIndexedFiles ? totalIndexedFiles.toLocaleString() : '百万'} 个文件，最近一次查询耗时 ${searchElapsedMs} 毫秒`}
            >
              <span className="search-stats-dot" />
              {sortedResults.length > 0 ? (
                <span><strong>{sortedResults.length}</strong> / 全盘 {totalIndexedFiles ? (totalIndexedFiles > 10000 ? (totalIndexedFiles / 10000).toFixed(1) + '万' : totalIndexedFiles) : '--'} · {searchElapsedMs}ms</span>
              ) : (
                <span>共 <strong>{totalIndexedFiles ? totalIndexedFiles.toLocaleString() : '2,654,120'}</strong> 文件</span>
              )}
            </div>

            {/* 导出当前搜索结果为 CSV */}
            {sortedResults.length > 0 && (
              <button
                type="button"
                className="search-action-pill-btn"
                onClick={exportResultsToCsv}
                title={`导出当前搜索结果清单为 CSV / Excel 格式 (Ctrl+E) - 共 ${sortedResults.length} 项`}
              >
                <FileSpreadsheet size={13} />
                <span>导出 CSV ({sortedResults.length})</span>
              </button>
            )}

            {/* ── 极客排序双区域分裂微胶囊 (Split Pill) ── */}
            <div className="search-sort-wrapper" ref={sortDropdownRef}>
              <div className={`sort-split-pill ${sortField !== 'relevance' ? 'sort-split-pill--active' : ''}`}>
                <button
                  type="button"
                  className="sort-split-main"
                  onClick={() => setShowSortMenu(prev => !prev)}
                  title="选择排序方式 (Ctrl+Shift+D 时间 / N 名称 / S 大小 / R 默认)"
                >
                  {sortField === 'modified' ? <Clock size={12} /> :
                   sortField === 'name' ? <ArrowDownAZ size={12} /> :
                   sortField === 'size' ? <HardDrive size={12} /> :
                   sortField === 'created' ? <Calendar size={12} /> :
                   <Zap size={12} />}
                  <span>
                    {sortField === 'relevance' ? '智能匹配' :
                     sortField === 'modified' ? '修改时间' :
                     sortField === 'name' ? '文件名' :
                     sortField === 'size' ? '文件大小' :
                     '创建时间'}
                  </span>
                  <ChevronDown size={11} className={`sort-chevron ${showSortMenu ? 'sort-chevron--open' : ''}`} />
                </button>
                {sortField !== 'relevance' && (
                  <button
                    type="button"
                    className="sort-split-dir-toggle"
                    onClick={toggleSortDirection}
                    title={`当前为 ${sortDirection === 'desc' ? '降序 (新/大优先)' : '升序 (旧/小优先)'}，点击直接原地切换`}
                  >
                    <span className="sort-dir-icon">{sortDirection === 'desc' ? '↓' : '↑'}</span>
                  </button>
                )}
              </div>

              {showSortMenu && (
                <div className="sort-dropdown-menu">
                  <div className="sort-menu-header">
                    <span>主排序列与升降序</span>
                  </div>
                  <div className="sort-menu-items-group">
                    {/* 智能相关度 */}
                    <div
                      className={`sort-menu-row ${sortField === 'relevance' ? 'sort-menu-row--active' : ''}`}
                      onClick={() => handleSetSortDirect('relevance', 'desc')}
                    >
                      <div className="sort-row-left">
                        <Zap size={13} />
                        <span>智能匹配相关度</span>
                      </div>
                      {sortField === 'relevance' && <Check size={12} className="sort-active-check" />}
                    </div>

                    {/* 修改时间 */}
                    <div className={`sort-menu-row ${sortField === 'modified' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('modified', sortDirection)}>
                        <Clock size={13} />
                        <span>修改时间</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'modified' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('modified', 'desc'); }}
                          title="从新到旧"
                        >
                          新→旧 ↓
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'modified' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('modified', 'asc'); }}
                          title="从旧到新"
                        >
                          旧→新 ↑
                        </button>
                      </div>
                    </div>

                    {/* 文件名 */}
                    <div className={`sort-menu-row ${sortField === 'name' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('name', sortDirection)}>
                        <ArrowDownAZ size={13} />
                        <span>文件名</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'name' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('name', 'asc'); }}
                          title="A 到 Z"
                        >
                          A→Z ↓
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'name' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('name', 'desc'); }}
                          title="Z 到 A"
                        >
                          Z→A ↑
                        </button>
                      </div>
                    </div>

                    {/* 文件大小 */}
                    <div className={`sort-menu-row ${sortField === 'size' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('size', sortDirection)}>
                        <HardDrive size={13} />
                        <span>文件大小</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'size' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('size', 'desc'); }}
                          title="大文件优先"
                        >
                          大→小 ↓
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'size' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('size', 'asc'); }}
                          title="小文件优先"
                        >
                          小→大 ↑
                        </button>
                      </div>
                    </div>

                    {/* 创建时间 */}
                    <div className={`sort-menu-row ${sortField === 'created' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('created', sortDirection)}>
                        <Calendar size={13} />
                        <span>创建时间</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'created' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('created', 'desc'); }}
                          title="从新到旧"
                        >
                          新→旧 ↓
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'created' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('created', 'asc'); }}
                          title="从旧到新"
                        >
                          旧→新 ↑
                        </button>
                      </div>
                    </div>
                  </div>

                  <div className="sort-menu-divider" />

                  {/* 多维组合排序微开关 */}
                  <div className="sort-composite-section">
                    <div className="sort-composite-header">
                      <span>组合排序规则</span>
                    </div>
                    <label className="sort-composite-option">
                      <input
                        type="checkbox"
                        checked={foldersFirst}
                        onChange={toggleFoldersFirst}
                      />
                      <Folder size={13} className="sort-option-icon" />
                      <span>文件夹始终优先置顶</span>
                    </label>
                    <label className="sort-composite-option">
                      <input
                        type="checkbox"
                        checked={groupByType}
                        onChange={toggleGroupByType}
                      />
                      <Tag size={13} className="sort-option-icon" />
                      <span>按文件扩展名/类型分组</span>
                    </label>
                  </div>
                </div>
              )}
            </div>
          </div>
        </div>

        {/* ── 历史搜索词极客气泡条 ── */}
        {query.trim() === '' && searchHistory.length > 0 && (
          <div className="search-history-container">
            <div className="search-history-header">
              <div className="search-history-title">
                <Clock size={13} />
                <span>最近搜索历史 (点击快速复用)</span>
              </div>
              <button
                type="button"
                className="search-history-clear-btn"
                onClick={clearAllHistory}
                title="清空所有历史搜索记录"
              >
                <Trash2 size={11} />
                <span>清空历史</span>
              </button>
            </div>
            <div className="search-history-chips">
              {searchHistory.map((item, idx) => (
                <div
                  key={idx}
                  className="search-history-chip"
                  onClick={() => {
                    updateQuery(item.search);
                    inputRef.current?.focus();
                  }}
                  title={`搜索频次: ${item.searchCount} 次 · 点击直接复用`}
                >
                  <Clock size={11} className="search-history-chip-icon" />
                  <span className="search-history-chip-text">{item.search}</span>
                  {item.searchCount > 1 && (
                    <span className="search-history-chip-count">{item.searchCount}</span>
                  )}
                  <button
                    type="button"
                    className="search-history-chip-del"
                    onClick={(e) => void removeHistoryItem(e, item.search)}
                    title="删除此条历史"
                  >
                    <X size={10} />
                  </button>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* ── 右上角悬浮毛玻璃视图定制气泡 (Popover) ── */}
        {showViewSettings && (
          <div className="search-view-settings-popover" ref={viewSettingsRef}>
            <div className="popover-header">
              <div className="popover-title">
                <SlidersHorizontal size={14} />
                <span>视图与偏好定制</span>
              </div>
              <button
                className="popover-close"
                onClick={() => setShowViewSettings(false)}
                type="button"
                title="关闭"
              >
                <X size={14} />
              </button>
            </div>

            <div className="popover-body">
              {/* 顶部一键全盘重新扫描与快照固化 */}
              <div className="popover-top-rebuild-card">
                <div className="popover-top-rebuild-info">
                  <div className="popover-top-rebuild-title">全盘文件索引与快照维护</div>
                  <div className="popover-top-rebuild-desc">
                    重新扫描所有分区 NTFS MFT 变更，并自动同步写入 EasyTools.db 快照 (支持快捷键 F5 / Ctrl+R)
                  </div>
                </div>
                <button
                  type="button"
                  className={`popover-rebuild-btn popover-rebuild-btn--top ${isRebuilding ? 'popover-rebuild-btn--loading' : ''}`}
                  onClick={rebuildIndex}
                  disabled={isRebuilding}
                  title="重新扫描全盘所有已选磁盘的 NTFS MFT 分区，并自动将最新全量索引固化写入 EasyTools.db 磁盘快照 (快捷键: F5 / Ctrl+R)"
                >
                  <RefreshCw size={13} className={isRebuilding ? 'spin-animation' : ''} />
                  <span>{isRebuilding ? '正在重新扫描并更新快照...' : '立即重新扫描并更新索引与快照 (F5)'}</span>
                </button>
              </div>

              {/* 1. 默认搜索范围与模式 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>默认搜索模式</span>
                  <span className="popover-badge-curr">
                    {searchMode === 'name' ? '仅搜文件名' : searchMode === 'both' ? '混合双搜' : '仅搜内容'}
                  </span>
                </div>
                <div className="popover-search-modes-grid">
                  <div 
                    className={`popover-search-mode-card ${searchMode === 'name' ? 'popover-search-mode-card--active' : ''}`}
                    onClick={() => changeSearchMode('name')}
                  >
                    <div className="search-mode-header">
                      <div className="search-mode-title-wrap">
                        <Zap size={14} className="search-mode-icon-name" />
                        <span className="search-mode-title">仅搜文件名 (极速·默认)</span>
                      </div>
                      {searchMode === 'name' && <Check size={13} className="search-mode-check" />}
                    </div>
                    <div className="search-mode-desc">
                      毫秒级响应 (&lt; 5ms)；若需临时搜内容，可点击顶部「文件内容」标签或输入 content:
                    </div>
                  </div>

                  <div 
                    className={`popover-search-mode-card ${searchMode === 'both' ? 'popover-search-mode-card--active' : ''}`}
                    onClick={() => changeSearchMode('both')}
                  >
                    <div className="search-mode-header">
                      <div className="search-mode-title-wrap">
                        <Sparkles size={14} className="search-mode-icon-both" />
                        <span className="search-mode-title">文件名与内容双搜 (混合)</span>
                      </div>
                      {searchMode === 'both' && <Check size={13} className="search-mode-check" />}
                    </div>
                    <div className="search-mode-desc">
                      输入关键词时同时穿透匹配文件名与文档/代码全文内容，智能合并呈现
                    </div>
                  </div>

                  <div 
                    className={`popover-search-mode-card ${searchMode === 'content' ? 'popover-search-mode-card--active' : ''}`}
                    onClick={() => changeSearchMode('content')}
                  >
                    <div className="search-mode-header">
                      <div className="search-mode-title-wrap">
                        <FileText size={14} className="search-mode-icon-content" />
                        <span className="search-mode-title">仅搜文件内容 (全文模式)</span>
                      </div>
                      {searchMode === 'content' && <Check size={13} className="search-mode-check" />}
                    </div>
                    <div className="search-mode-desc">
                      默认对全盘所有办公文档、表格、PDF、CAD与代码源文件执行穿透全文检索
                    </div>
                  </div>
                </div>
              </div>

              {/* 1. 布局密度 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>列表显示密度</span>
                  <span className="popover-badge-curr">
                    {density === 'compact' ? t('search.densityCompact') : density === 'comfortable' ? t('search.densityComfortable') : t('search.densityStandard')}
                  </span>
                </div>
                <div className="popover-segmented-control">
                  <button
                    type="button"
                    className={`popover-segment ${density === 'compact' ? 'popover-segment--active' : ''}`}
                    onClick={() => changeDensity('compact')}
                  >
                    {t('search.densityCompact')}
                  </button>
                  <button
                    type="button"
                    className={`popover-segment ${density === 'standard' ? 'popover-segment--active' : ''}`}
                    onClick={() => changeDensity('standard')}
                  >
                    {t('search.densityStandard')}
                  </button>
                  <button
                    type="button"
                    className={`popover-segment ${density === 'comfortable' ? 'popover-segment--active' : ''}`}
                    onClick={() => changeDensity('comfortable')}
                  >
                    {t('search.densityComfortable')}
                  </button>
                </div>
              </div>

              {/* 2. 检索结果数量上限 (支持全部返回) */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>{t('search.maxResults')}</span>
                  <span className="popover-badge-curr">
                    {maxResultLimit === 0 ? t('search.maxResultsAll') : `${maxResultLimit} 条`}
                  </span>
                </div>
                <div className="popover-segmented-control">
                  {[50, 100, 200, 0].map((lim) => (
                    <button
                      key={lim}
                      type="button"
                      className={`popover-segment ${maxResultLimit === lim ? 'popover-segment--active' : ''}`}
                      onClick={() => changeMaxResultLimit(lim)}
                    >
                      {lim === 0 ? t('search.maxResultsAll') : (lim === 50 ? t('search.maxResults50') : lim === 100 ? t('search.maxResults100') : t('search.maxResults200'))}
                    </button>
                  ))}
                </div>
              </div>

              {/* 2. 窗口尺寸与位置管理 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>窗口尺寸与位置调节 ({windowSize.width} × {windowSize.height})</span>
                  <button
                    type="button"
                    className="popover-mini-link"
                    onClick={() => {
                      void bridgeRequest('search.resetPlacement');
                      setWindowSize({ width: 760, height: 520 });
                      toast.success('已恢复默认居中与 760×520 尺寸');
                    }}
                    title="恢复至默认居中与 760×520 尺寸"
                  >
                    <RotateCcw size={11} style={{ marginRight: 3, verticalAlign: -1 }} />
                    恢复默认居中 (760×520)
                  </button>
                </div>
                <div className="popover-presets-grid">
                  <button
                    type="button"
                    className={`popover-preset-btn ${windowSize.width === 760 && windowSize.height === 520 ? 'popover-preset-btn--active' : ''}`}
                    onClick={() => changeWindowSize(760, 520)}
                  >
                    默认 (760×520)
                  </button>
                  {WINDOW_PRESETS.map((preset) => (
                    <button
                      key={preset.id}
                      type="button"
                      className={`popover-preset-btn ${windowSize.width === preset.width && windowSize.height === preset.height ? 'popover-preset-btn--active' : ''}`}
                      onClick={() => changeWindowSize(preset.width, preset.height)}
                    >
                      {preset.label}
                    </button>
                  ))}
                </div>
                <div className="popover-section-hint">
                  <Info size={12} style={{ marginRight: 4, verticalAlign: -1, display: 'inline-block' }} />
                  提示：按住窗口顶部空白处或右上角移动图标可随意拖拽窗口；拖拽右下角或窗口边缘可自由拉伸任意大小，系统将自动记忆您的习惯位置。
                </div>
              </div>

              {/* 3. 列显示开关 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>结果列显示控制</span>
                </div>
                <div className="popover-cols-grid">
                  {columns.map(col => (
                    <label key={col.id} className="popover-col-checkbox-label">
                      <input
                        type="checkbox"
                        checked={col.visible}
                        onChange={() => toggleColumnVisibility(col.id)}
                      />
                      <span>{col.label}</span>
                    </label>
                  ))}
                </div>
              </div>

              {/* 4. 名称与路径列宽占比 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>名称与路径占比 (名称 {nameFlex}% : 路径 {pathFlex}%)</span>
                </div>
                <div className="popover-slider-row">
                  <span className="slider-label">窄</span>
                  <input
                    type="range"
                    min="15"
                    max="65"
                    step="1"
                    value={nameFlex}
                    onChange={(e) => updateNameAndPathFlex(parseInt(e.target.value, 10))}
                    className="popover-ratio-slider"
                  />
                  <span className="slider-label">宽</span>
                </div>
              </div>

              {/* 5. 搜索驱动器与磁盘位置 */}
              {systemDrives.length > 0 && (
                <div className="popover-section">
                  <div className="popover-section-title">
                    <span>搜索磁盘与网络位置</span>
                    <div className="popover-drives-actions">
                      <button
                        type="button"
                        className="popover-mini-link"
                        onClick={selectAllDrives}
                      >
                        全选
                      </button>
                      <span className="popover-action-divider">/</span>
                      <button
                        type="button"
                        className="popover-mini-link"
                        onClick={deselectAllDrives}
                      >
                        全不选
                      </button>
                    </div>
                  </div>
                  <div className="popover-drives-grid">
                    {systemDrives.map((drv) => {
                      const isChecked = enabledDrives.includes(drv.letter);
                      const isRemote = drv.type === 'remote';
                      const isRemovable = drv.type === 'removable';
                      const totalStr = drv.totalBytes > 0 ? formatBytes(drv.totalBytes) : '';
                      const freeStr = drv.freeBytes > 0 ? `(可用 ${formatBytes(drv.freeBytes)})` : '';
                      const label = drv.volumeLabel ? `${drv.volumeLabel} (${drv.letter}:)` : (isRemote ? `网络驱动器 (${drv.letter}:)` : `本地磁盘 (${drv.letter}:)`);

                      return (
                        <div
                          key={drv.letter}
                          className={`popover-drive-card ${isChecked ? 'popover-drive-card--active' : ''}`}
                          onClick={() => toggleDrive(drv.letter)}
                        >
                          <input
                            type="checkbox"
                            checked={isChecked}
                            onChange={() => {}}
                            className="drive-card-checkbox"
                            aria-label={`启用 ${drv.letter} 盘`}
                          />
                          <div className="drive-card-icon">
                            {isRemote ? <Network size={15} /> : isRemovable ? <Disc size={15} /> : <HardDrive size={15} />}
                          </div>
                          <div className="drive-card-details">
                            <div className="drive-card-title-row">
                              <span className="drive-card-title">{label}</span>
                              <span className="drive-card-tag">{drv.fileSystem || (isRemote ? '网络共享' : '本地')}</span>
                            </div>
                            <div className="drive-card-meta">
                              {totalStr} {freeStr}
                            </div>
                          </div>
                        </div>
                      );
                    })}
                  </div>
                </div>
              )}

              {/* 6. 排除规则 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>排除规则</span>
                </div>
                <div className="popover-exclude-options">
                  <label className="popover-col-checkbox-label">
                    <input
                      type="checkbox"
                      checked={excludeGitAndModules}
                      onChange={(e) => {
                        setExcludeGitAndModules(e.target.checked);
                        localStorage.setItem('easytools_search_exclude_dev', String(e.target.checked));
                      }}
                    />
                    <span>排除开发依赖与回收站 (node_modules, .git, $Recycle.Bin)</span>
                  </label>
                  <label className="popover-col-checkbox-label">
                    <input
                      type="checkbox"
                      checked={excludeHidden}
                      onChange={(e) => {
                        setExcludeHidden(e.target.checked);
                        localStorage.setItem('easytools_search_exclude_hidden', String(e.target.checked));
                      }}
                    />
                    <span>排除系统隐藏文件与受保护文件</span>
                  </label>
                </div>
              </div>

              {/* 7. 文档内容检索格式定制 (Content Search Formats & Exts) */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <div className="popover-section-title-left">
                    <SlidersHorizontal size={13} className="popover-title-icon" />
                    <span>文档内容检索支持格式</span>
                    <span className="popover-title-badge">content:</span>
                  </div>
                  <button
                    type="button"
                    className="popover-quick-action"
                    onClick={resetContentFormats}
                    title="恢复出厂支持格式"
                  >
                    <RotateCcw size={11} />
                    <span>{t('search.resetDefault')}</span>
                  </button>
                </div>
                
                <div className="popover-format-categories">
                  {CONTENT_FORMAT_CATEGORIES.map(cat => {
                    const enabledCount = cat.extensions.filter(ext => !disabledContentFormats.includes(ext)).length;
                    const allEnabled = enabledCount === cat.extensions.length;
                    return (
                      <div key={cat.id} className="popover-format-cat-block">
                        <div className="popover-format-cat-header">
                          <div className="popover-format-cat-title-wrap">
                            <div className={`format-cat-icon-badge format-cat-icon-badge--${cat.iconName}`}>
                              {cat.iconName === 'code' && <Code2 size={12} />}
                              {cat.iconName === 'doc' && <FileText size={12} />}
                              {cat.iconName === 'config' && <SlidersHorizontal size={12} />}
                              {cat.iconName === 'design' && <Palette size={12} />}
                            </div>
                            <span className="popover-format-cat-name">{t(cat.nameKey)}</span>
                            <span className="popover-format-badge-count">{enabledCount} / {cat.extensions.length}</span>
                          </div>
                          <button
                            type="button"
                            className="popover-cat-toggle-btn"
                            onClick={() => toggleCategoryFormats(cat, !allEnabled)}
                          >
                            {allEnabled ? t('search.disableAll') : t('search.enableAll')}
                          </button>
                        </div>
                        <div className="popover-format-chips">
                          {cat.extensions.map(ext => {
                            const isEnabled = !disabledContentFormats.includes(ext);
                            return (
                              <button
                                key={ext}
                                type="button"
                                className={`format-chip ${isEnabled ? 'format-chip--enabled' : 'format-chip--disabled'}`}
                                onClick={() => toggleContentFormat(ext)}
                                title={isEnabled ? `点击禁用 .${ext}` : `点击启用 .${ext}`}
                              >
                                <span>.{ext}</span>
                              </button>
                            );
                          })}
                        </div>
                      </div>
                    );
                  })}

                  {/* 用户自定义格式列表 */}
                  <div className="popover-format-cat-block">
                    <div className="popover-format-cat-header">
                      <div className="popover-format-cat-title-wrap">
                        <div className="format-cat-icon-badge format-cat-icon-badge--custom">
                          <Sparkles size={12} />
                        </div>
                        <span className="popover-format-cat-name">{t('search.customFormats')}</span>
                        <span className="popover-format-badge-count">{customContentFormats.length} 项</span>
                      </div>
                    </div>
                    {customContentFormats.length > 0 && (
                      <div className="popover-format-chips">
                        {customContentFormats.map(ext => {
                          const isEnabled = !disabledContentFormats.includes(ext);
                          return (
                            <div
                              key={ext}
                              className={`format-chip format-chip--custom ${isEnabled ? 'format-chip--enabled' : 'format-chip--disabled'}`}
                            >
                              <span onClick={() => toggleContentFormat(ext)} className="format-chip-label">
                                .{ext}
                              </span>
                              <button
                                type="button"
                                className="format-chip-del"
                                onClick={(e) => {
                                  e.stopPropagation();
                                  removeCustomContentFormat(ext);
                                }}
                                title="删除此自定义格式"
                              >
                                <Trash2 size={10} />
                              </button>
                            </div>
                          );
                        })}
                      </div>
                    )}

                    <div className="popover-format-add-row">
                      <input
                        type="text"
                        className="popover-format-add-input"
                        placeholder="添加自定义后缀 (如 .log2, .proto, .prisma)... 回车添加"
                        value={newFormatInput}
                        onChange={(e) => setNewFormatInput(e.target.value)}
                        onKeyDown={(e) => {
                          e.stopPropagation();
                          if (e.key === 'Enter') {
                            e.preventDefault();
                            addCustomContentFormat(newFormatInput);
                          }
                        }}
                      />
                      <button
                        type="button"
                        className="popover-format-add-btn"
                        onClick={() => addCustomContentFormat(newFormatInput)}
                        title="添加自定义格式"
                      >
                        <Plus size={13} />
                        <span>添加</span>
                      </button>
                    </div>
                  </div>
                </div>
              </div>

              {/* 8. 索引数据库快照与缓存状态 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <div className="popover-section-title-left">
                    <HardDrive size={13} className="popover-title-icon" />
                    <span>磁盘快照与数据库</span>
                    <span className="popover-title-badge">EasyTools.db</span>
                  </div>
                </div>
                
                <div className="popover-db-card">
                  <div className="popover-db-row">
                    <span className="popover-db-label">快照状态：</span>
                    <span className="popover-db-val">
                      {dbStats?.exists ? (
                        <span className="popover-db-status--ok">已持久化 ({formatBytes(dbStats.dbSize)})</span>
                      ) : (
                        <span className="popover-db-status--empty">已就绪 (关机或空闲自动持久化)</span>
                      )}
                    </span>
                  </div>
                  <div className="popover-db-row">
                    <span className="popover-db-label">全盘索引记录：</span>
                    <span className="popover-db-val">{dbStats?.totalRecords ? dbStats.totalRecords.toLocaleString() : (totalIndexedFiles ? totalIndexedFiles.toLocaleString() : '0')} 条</span>
                  </div>
                  <div className="popover-db-row">
                    <span className="popover-db-label">运行频次库：</span>
                    <span className="popover-db-val">Run History ({dbStats?.runHistoryCount ?? 0} 条高频)</span>
                  </div>
                  <div className="popover-db-row">
                    <span className="popover-db-label">搜索历史库：</span>
                    <span className="popover-db-val">Search History ({dbStats?.searchHistoryCount ?? 0} 条检索词)</span>
                  </div>
                  <div className="popover-db-actions">
                    <button
                      type="button"
                      className="popover-db-btn popover-db-btn--danger"
                      onClick={clearAllHistory}
                      style={{ width: '100%' }}
                    >
                      <Trash2 size={12} />
                      <span>清空搜索与运行历史</span>
                    </button>
                  </div>
                </div>
              </div>

              <div className="popover-footer">
                <button
                  className="popover-reset-btn"
                  onClick={resetColumns}
                  type="button"
                >
                  <RotateCcw size={12} />
                  <span>恢复默认视图配置</span>
                </button>
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
                          <span className="file-name-text">{highlightMatch(result.name, queryKeywords)}</span>
                          {isColVisible('ext') && (
                            <span className={`file-ext-badge ${getFileTypeBadge(result.name, result.isDirectory).colorClass}`}>
                              {getFileTypeBadge(result.name, result.isDirectory).label}
                            </span>
                          )}
                          {Boolean(result.runCount && result.runCount > 0) && (
                            <span className="file-run-badge" title={`历史已打开 ${result.runCount} 次`}>
                              打开 {result.runCount}次
                            </span>
                          )}
                        </span>
                      )}
                      {isColVisible('path') && (
                        <span className="file-path-inline" style={{ flex: `${pathFlex} 1 0` }} title={result.path}>
                          {isColVisible('parent') && extractParentFolder(result.path) && (
                            <span className="file-parent-tag" title={`所属上级目录: ${extractParentFolder(result.path)}`}>
                              <Folder size={11} className="file-parent-icon" style={{ marginRight: 3, verticalAlign: -1, display: 'inline-block' }} />
                              {highlightMatch(extractParentFolder(result.path), queryKeywords)}
                            </span>
                          )}
                          <span className="file-path-text">{highlightMatch(result.path, queryKeywords)}</span>
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
                            <span className="file-name-text">{highlightMatch(result.name, queryKeywords)}</span>
                            {isColVisible('ext') && (
                              <span className={`file-ext-badge ${getFileTypeBadge(result.name, result.isDirectory).colorClass}`}>
                                {getFileTypeBadge(result.name, result.isDirectory).label}
                              </span>
                            )}
                            {Boolean(result.runCount && result.runCount > 0) && (
                              <span className="file-run-badge" title={`历史已打开 ${result.runCount} 次`}>
                                打开 {result.runCount}次
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
                                <Folder size={11} className="file-parent-icon" style={{ marginRight: 3, verticalAlign: -1, display: 'inline-block' }} />
                                {highlightMatch(extractParentFolder(result.path), queryKeywords)}
                              </span>
                            )}
                            <span className="file-path">{highlightMatch(result.path, queryKeywords)}</span>
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
          <div className="search-footer-left">
            {/* 1. 总对象数显示 (Everything 级核心状态) 与一键刷新微按钮 */}
            <div className="search-footer-stat-item" title="当前匹配到的文件与文件夹对象总数">
              <span>
                {sortedResults.length > 0 ? (
                  <><strong>{sortedResults.length.toLocaleString()}</strong> 个对象</>
                ) : (
                  <><strong>{totalIndexedFiles ? totalIndexedFiles.toLocaleString() : '1,394,498'}</strong> 个对象</>
                )}
              </span>
            </div>
            <button
              type="button"
              className="search-footer-refresh-btn"
              onClick={rebuildIndex}
              disabled={isRebuilding}
              title="重新扫描全盘并更新索引与快照 (快捷键: F5 / Ctrl+R)"
            >
              <RefreshCw size={11} className={isRebuilding ? 'spin-animation' : ''} />
            </button>

            {/* 2. 当前匹配结果总大小 */}
            {sortedResults.length > 0 && totalResultSize > 0 && (
              <>
                <span className="search-footer-stat-divider" />
                <div className="search-footer-stat-item" title="当前搜索结果所有文件的累计容量占用">
                  <span><strong>{formatBytes(totalResultSize)}</strong></span>
                </div>
              </>
            )}

            {/* 3. 当前选中项序号与单项大小 */}
            {sortedResults.length > 0 && sortedResults[selectedIndex] && (
              <>
                <span className="search-footer-stat-divider" />
                <div className="search-footer-stat-item search-footer-stat-sub" title={sortedResults[selectedIndex].path}>
                  <span>
                    选中 <strong>{selectedIndex + 1}</strong> / {sortedResults.length}
                    {!sortedResults[selectedIndex].isDirectory && sortedResults[selectedIndex].size !== undefined ? (
                      <> ({formatBytes(sortedResults[selectedIndex].size || 0)})</>
                    ) : (
                      <> (文件夹)</>
                    )}
                  </span>
                </div>
              </>
            )}

            {/* 4. 引擎匹配耗时 */}
            {searchElapsedMs !== undefined && searchElapsedMs >= 0 && sortedResults.length > 0 && (
              <>
                <span className="search-footer-stat-divider" />
                <div className="search-footer-stat-item search-footer-stat-sub" title="MFT 内存索引引擎匹配耗时">
                  <span>{searchElapsedMs} ms</span>
                </div>
              </>
            )}
          </div>

          <div className="search-footer-right">
            <span className="search-hint"><kbd>Enter</kbd> {t('search.open', '打开')}</span>
            <span className="search-hint"><kbd>Ctrl+Enter</kbd> {t('search.openFolder', '定位')}</span>
            <span className="search-hint"><kbd>Ctrl+C</kbd> 复制</span>
            <span className="search-hint"><kbd>Ctrl+E</kbd> 导出</span>
            <span className="search-hint"><kbd>F5</kbd> 刷新</span>
            <span className="search-hint"><kbd>F1</kbd> 语法</span>
            <span className="search-hint"><kbd>Esc</kbd> {t('search.close', '关闭')}</span>
          </div>
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

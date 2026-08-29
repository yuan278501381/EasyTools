import { memo, useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, startTransition, type CSSProperties, type KeyboardEvent, type MouseEvent as ReactMouseEvent, type RefCallback } from 'react';
import { 
  File, 
  Folder, 
  Search, 
  SearchX,
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
  Info,
  FolderOpen,
  ShieldAlert,
  Copy,
  Pencil,
  Lightbulb,
  Pin,
  type LucideIcon
} from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { bridgeRequest } from './hooks/useBridge';
import { useAppearance } from './hooks/useAppearance';
import { DynamicRowLayout, isSelectedOutsideVirtualRange } from './searchVirtualization';
import { isEmptyContentSyntax, nextQueryId, resolveDebounceMs } from './searchScheduling';
import { WindowResizeHandles } from './components/WindowResizeHandles';
import { Toggle } from './components/UIKit';
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
  status?: 'ready' | 'starting' | 'unavailable';
  totalIndexedFiles?: number;
  elapsedMs?: number;
  error?: string;
  /** 服务端在计算期间收到了更新的查询，本次结果应当忽略。 */
  cancelled?: boolean;
  queryId?: number;
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
  label?: string;
  labelKey?: string;
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
  labelKey?: string;
  defaultLabel?: string;
  prefix: string;
  icon?: LucideIcon;
}

const DEFAULT_COLUMNS: ColumnSetting[] = [
  { id: 'name', labelKey: 'search.colName', visible: true, flex: 32 },
  { id: 'ext', labelKey: 'search.colExt', visible: true },
  { id: 'parent', labelKey: 'search.colParent', visible: true },
  { id: 'path', labelKey: 'search.colPath', visible: true, flex: 42 },
  { id: 'size', labelKey: 'search.colSize', visible: true },
  { id: 'modified', labelKey: 'search.colModified', visible: true },
  { id: 'created', labelKey: 'search.colCreated', visible: false },
  { id: 'snippets', labelKey: 'search.colSnippets', visible: true },
];

const WINDOW_PRESETS: WindowPreset[] = [
  { id: 'standard', label: 'Standard (800×600)', width: 800, height: 600 },
  { id: 'wide', label: 'Widescreen (1000×650)', width: 1000, height: 650 },
  { id: 'large', label: 'Large (1200×750)', width: 1200, height: 750 },
  { id: 'extra', label: 'Ultra-Wide (1400×800)', width: 1400, height: 800 },
];

const CATEGORY_DEFS: { id: string; labelKey: string; defaultLabel: string; prefix: string; icon?: LucideIcon }[] = [
  { id: 'all', labelKey: 'search.catAll', defaultLabel: 'All', prefix: '', icon: Sparkles },
  { id: 'content', labelKey: 'search.catContent', defaultLabel: 'Content', prefix: 'content:', icon: FileText },
  { id: 'doc', labelKey: 'search.catDocs', defaultLabel: 'Documents', prefix: 'ext:doc;docx;xls;xlsx;ppt;pptx;pdf;txt;md ', icon: FileSpreadsheet },
  { id: 'image', labelKey: 'search.catImages', defaultLabel: 'Images', prefix: 'ext:jpg;jpeg;png;webp;gif;bmp;svg ', icon: FileImage },
  { id: 'video', labelKey: 'search.catVideos', defaultLabel: 'Videos', prefix: 'ext:mp4;mkv;avi;mov;wmv;flv;webm ', icon: FileVideo },
  { id: 'audio', labelKey: 'search.catAudio', defaultLabel: 'Audio', prefix: 'ext:mp3;wav;flac;aac;m4a;ogg ', icon: FileAudio },
  { id: 'archive', labelKey: 'search.catArchives', defaultLabel: 'Archives', prefix: 'ext:zip;rar;7z;tar;gz ', icon: FileArchive },
  { id: 'code', labelKey: 'search.catCode', defaultLabel: 'Code', prefix: 'ext:cpp;h;ts;tsx;js;py;rs;go;java;lua;json ', icon: FileCode },
  { id: 'folder', labelKey: 'search.catFolders', defaultLabel: 'Folders', prefix: 'folder: ', icon: Folder },
];

export interface SyntaxExampleItem {
  category: 'content' | 'path' | 'ext' | 'logic';
  syntax: string;
  descKey: string;
  defaultDesc: string;
  highlight?: boolean;
}

const SYNTAX_CATEGORIES: { id: string; labelKey: string; defaultLabel: string }[] = [
  { id: 'all', labelKey: 'search.syntaxCatAll', defaultLabel: 'All Syntax' },
  { id: 'content', labelKey: 'search.syntaxCatContent', defaultLabel: 'Content Search' },
  { id: 'path', labelKey: 'search.syntaxCatPath', defaultLabel: 'Path & Drive' },
  { id: 'ext', labelKey: 'search.syntaxCatExt', defaultLabel: 'Wildcards & Exts' },
  { id: 'logic', labelKey: 'search.syntaxCatLogic', defaultLabel: 'Logic & Regex' },
];

const SYNTAX_EXAMPLES: SyntaxExampleItem[] = [
  // 1. Content search
  { category: 'content', syntax: 'content:SELECT', descKey: 'search.syntaxExContent1', defaultDesc: 'Full-text search inside code, documents, designs, and CAD', highlight: true },
  { category: 'content', syntax: 'ext:docx;sql content:order', descKey: 'search.syntaxExContent2', defaultDesc: 'Penetrate search in files of specified extension types' },
  { category: 'content', syntax: 'c:\\ content:report', descKey: 'search.syntaxExContent3', defaultDesc: 'Limit full-text search strictly under directory path' },

  // 2. Path and drive
  { category: 'path', syntax: 'c:\\', descKey: 'search.syntaxExPath1', defaultDesc: 'Limit search scope strictly within C: root and directory' },
  { category: 'path', syntax: 'c:\\repo\\ *.ts', descKey: 'search.syntaxExPath2', defaultDesc: 'Search specific file types within given subdirectory' },
  { category: 'path', syntax: 'path:windows', descKey: 'search.syntaxExPath3', defaultDesc: 'Match keyword in complete file path' },
  { category: 'path', syntax: 'folder: project', descKey: 'search.syntaxExPath4', defaultDesc: 'Match folders and directories only, exclude files' },

  // 3. Wildcards and extensions
  { category: 'ext', syntax: '*.txt', descKey: 'search.syntaxExExt1', defaultDesc: 'Wildcard match all files with txt extension' },
  { category: 'ext', syntax: 'ext:jpg;png;webp', descKey: 'search.syntaxExExt2', defaultDesc: 'Filter multiple extensions at once (semicolon separated)' },
  { category: 'ext', syntax: 'file: *.pdf', descKey: 'search.syntaxExExt3', defaultDesc: 'Search files only, exclude folders' },
  { category: 'ext', syntax: '"Program Files"', descKey: 'search.syntaxExExt4', defaultDesc: 'Double quotes phrase exact match (handles paths with spaces)' },

  // 4. Logic & Regex
  { category: 'logic', syntax: 'report !draft', descKey: 'search.syntaxExLogic1', defaultDesc: 'Contains report but excludes items containing draft (NOT)' },
  { category: 'logic', syntax: 'ext:jpg | ext:png', descKey: 'search.syntaxExLogic2', defaultDesc: 'Logical OR condition combined search' },
  { category: 'logic', syntax: 'regex:^app_\\d+\\.log$', descKey: 'search.syntaxExLogic3', defaultDesc: 'Regex search (starts with app_digits)' },
  { category: 'logic', syntax: 'case:EasyTools', descKey: 'search.syntaxExLogic4', defaultDesc: 'Case-sensitive exact match' },
  { category: 'logic', syntax: 'pinyin:wx', descKey: 'search.syntaxExLogic5', defaultDesc: 'Explicit Pinyin initials / full Pinyin search (e.g. wx for WeChat)' },
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
    extensions: [
      'cpp', 'c', 'h', 'hpp', 'hxx', 'inl', 'cs',
      'rs', 'go', 'zig', 'v', 'nim', 'odin', 'd',
      'java', 'kt', 'kts', 'scala', 'groovy', 'dart', 'swift', 'm', 'mm',
      'ts', 'tsx', 'js', 'jsx', 'mjs', 'cjs', 'vue', 'svelte', 'astro',
      'html', 'htm', 'css', 'scss', 'sass', 'less',
      'py', 'pyw', 'rb', 'php', 'pl', 'pm', 'lua',
      'sh', 'bash', 'zsh', 'ps1', 'psm1', 'bat', 'cmd', 'vbs', 'ahk', 'au3',
      'sql', 'prc', 'fnc', 'trg', 'pks', 'pkb', 'pls', 'ch', 'pld',
      'asm', 's', 'glsl', 'hlsl', 'vert', 'frag', 'geom', 'comp', 'shader', 'wgsl'
    ]
  },
  {
    id: 'docs',
    nameKey: 'search.catDocs',
    iconName: 'doc',
    extensions: [
      'docx', 'docm', 'dotx', 'wps', 'wpt', 'rtf',
      'xlsx', 'xlsm', 'xltx', 'et', 'ett', 'csv', 'tsv',
      'pptx', 'pptm', 'potx', 'dps', 'dpt',
      'pdf', 'txt', 'md', 'markdown', 'log', 'tex', 'bib', 'rst', 'adoc', 'epub',
      'diff', 'patch', 'org'
    ]
  },
  {
    id: 'config',
    nameKey: 'search.catConfig',
    iconName: 'config',
    extensions: [
      'json', 'jsonc', 'json5', 'yaml', 'yml', 'toml', 'xml', 'xaml',
      'ini', 'cfg', 'conf', 'config', 'env', 'reg', 'properties',
      'proto', 'graphql', 'gql', 'thrift', 'prisma', 'schema', 'avsc', 'dbml',
      'lock', 'plist', 'prefs'
    ]
  },
  {
    id: 'design',
    nameKey: 'search.catDesign',
    iconName: 'design',
    extensions: [
      'dxf', 'dwg', 'psd', 'ai', 'cdr', 'xmind', 'svg'
    ]
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

/** 列可见性与宽度的快照。合成一个稳定对象，避免每行各自去查 columns 数组。 */
export interface ColumnLayout {
  name: boolean;
  ext: boolean;
  parent: boolean;
  path: boolean;
  size: boolean;
  modified: boolean;
  created: boolean;
  snippets: boolean;
  nameFlex: number;
  pathFlex: number;
}

interface SearchResultRowProps {
  result: SearchResult;
  index: number;
  selected: boolean;
  density: SearchDensity;
  columns: ColumnLayout;
  queryKeywords: string[];
  onHover: (index: number) => void;
  onSelect: (index: number) => void;
  onOpen: (result: SearchResult) => void;
  onContextMenu: (event: ReactMouseEvent, index: number, result: SearchResult) => void;
  measureRef?: RefCallback<HTMLLIElement>;
  style?: CSSProperties;
  setSize?: number;
}

/**
 * 单条搜索结果。
 *
 * 鼠标划过列表会改变选中项，此前这会让整个结果列表重新渲染 —— 100 条结果意味
 * 着几千个节点为了一条高亮而重建。把行拆成 memo 组件后，一次 hover 只影响移出
 * 和移入的两行。
 */
const SearchResultRow = memo(function SearchResultRow({
  result,
  index,
  selected,
  density,
  columns,
  queryKeywords,
  onHover,
  onSelect,
  onOpen,
  onContextMenu,
  measureRef,
  style,
  setSize,
}: SearchResultRowProps) {
  const { t } = useTranslation();
  const parentFolder = columns.parent ? extractParentFolder(result.path) : '';
  const sizeText = columns.size ? formatFileSize(result.size, result.isDirectory) : '';
  const badge = columns.ext ? getFileTypeBadge(result.name, result.isDirectory) : null;
  const modifiedText = columns.modified ? formatWindowsTime(result.lastWriteTime) : '';
  const createdText = columns.created ? formatWindowsTime(result.creationTime) : '';

  return (
    <li
      id={`search-result-${index}`}
      data-virtual-index={index}
      className={`search-result-item ${selected ? 'selected' : ''}`}
      role="option"
      aria-selected={selected}
      aria-posinset={index + 1}
      aria-setsize={setSize}
      ref={measureRef}
      style={style}
      onMouseEnter={() => onHover(index)}
      onMouseDown={(event) => event.preventDefault()}
      onClick={() => onSelect(index)}
      onDoubleClick={() => onOpen(result)}
      onContextMenu={(event) => onContextMenu(event, index, result)}
    >
      {renderFileIcon(result.name, result.isDirectory, density)}
      <div className="file-info">
        {density === 'compact' ? (
          <div className="file-row-main">
            {columns.name && (
              <span className="file-name" style={{ flex: `${columns.nameFlex} 1 0` }} title={result.name}>
                <span className="file-name-text">{highlightMatch(result.name, queryKeywords)}</span>
                {Boolean(result.runCount && result.runCount > 0) && (
                  <span className="file-run-badge" title={t('search.runCountTip', 'Opened {{count}} times in history', { count: result.runCount })}>
                    {t('search.runCountBadge', '{{count}} runs', { count: result.runCount })}
                  </span>
                )}
              </span>
            )}
            {badge && <span className={`file-ext-badge ${badge.colorClass}`}>{badge.label}</span>}
            {columns.parent && (
              <span className="file-parent-column" title={t('search.parentFolderTip', 'Folder: {{folder}}', { folder: parentFolder || '—' }).replace('{{folder}}', parentFolder || '—')}>
                <Folder size={11} aria-hidden="true" />
                {parentFolder ? highlightMatch(parentFolder, queryKeywords) : '—'}
              </span>
            )}
            {columns.path && (
              <span className="file-path-inline" style={{ flex: `${columns.pathFlex} 1 0` }} title={result.path}>
                <span className="file-path-text">{highlightMatch(result.path, queryKeywords)}</span>
              </span>
            )}
            <div className="file-meta-top">
              {columns.size && <span className="meta-size-badge" title={t('search.colSize', 'Size')}>{sizeText || '—'}</span>}
              {columns.modified && (
                <span className="meta-date-mod" title={t('search.colModified', 'Modified')}>
                  <span className="meta-field-label">{t('search.colModifiedShort', 'Mod')}</span>{modifiedText || '—'}
                </span>
              )}
              {columns.created && (
                <span className="meta-date-create" title={t('search.colCreated', 'Created')}>
                  <span className="meta-field-label">{t('search.colCreatedShort', 'Cre')}</span>{createdText || '—'}
                </span>
              )}
            </div>
          </div>
        ) : (
          <>
            <div className="file-row-main">
              {columns.name && (
                <span className="file-name" title={result.name}>
                  <span className="file-name-text">{highlightMatch(result.name, queryKeywords)}</span>
                  {Boolean(result.runCount && result.runCount > 0) && (
                    <span className="file-run-badge" title={t('search.runCountTip', 'Opened {{count}} times in history', { count: result.runCount })}>
                      {t('search.runCountBadge', '{{count}} runs', { count: result.runCount })}
                    </span>
                  )}
                </span>
              )}
              {badge && <span className={`file-ext-badge ${badge.colorClass}`}>{badge.label}</span>}
              <div className="file-meta-top">
                {columns.size && <span className="meta-size-badge" title={t('search.colSize', 'Size')}>{sizeText || '—'}</span>}
                {columns.modified && (
                  <span className="meta-date-mod" title={t('search.colModified', 'Modified')}>
                    <span className="meta-field-label">{t('search.colModifiedShort', 'Mod')}</span>{modifiedText || '—'}
                  </span>
                )}
                {columns.created && (
                  <span className="meta-date-create" title={t('search.colCreated', 'Created')}>
                    <span className="meta-field-label">{t('search.colCreatedShort', 'Cre')}</span>{createdText || '—'}
                  </span>
                )}
              </div>
            </div>
            <div className="file-row-sub">
              {columns.parent && (
                <span className="file-parent-column" title={t('search.parentFolderTip', 'Folder: {{folder}}', { folder: parentFolder || '—' }).replace('{{folder}}', parentFolder || '—')}>
                  <Folder size={11} aria-hidden="true" />
                  {parentFolder ? highlightMatch(parentFolder, queryKeywords) : '—'}
                </span>
              )}
              {columns.path && (
                <div className="file-path-wrapper" title={result.path}>
                  <span className="file-path">{highlightMatch(result.path, queryKeywords)}</span>
                </div>
              )}
            </div>
          </>
        )}

        {columns.snippets && result.snippets && result.snippets.length > 0 && (
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
  );
});

interface VirtualSearchResultsProps {
  results: SearchResult[];
  selectedIndex: number;
  density: SearchDensity;
  columns: ColumnLayout;
  queryKeywords: string[];
  onHover: (index: number) => void;
  onSelect: (index: number) => void;
  onOpen: (result: SearchResult) => void;
  onContextMenu: (event: ReactMouseEvent, index: number, result: SearchResult) => void;
}

/**
 * 动态高度虚拟列表。内容命中摘要会令行高不同，不能用固定高度的传统虚拟滚动；
 * ResizeObserver 只测量视口附近真实行，其余行使用密度估算值并在滚动时逐步校准。
 */
export function VirtualSearchResults({
  results, selectedIndex, density, columns, queryKeywords, onHover, onSelect, onOpen, onContextMenu,
}: VirtualSearchResultsProps) {
  const listRef = useRef<HTMLUListElement>(null);
  const observerRef = useRef<ResizeObserver | null>(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [viewportHeight, setViewportHeight] = useState(480);
  const [layoutRevision, setLayoutRevision] = useState(0);
  const estimatedHeight = density === 'compact' ? 44 : density === 'comfortable' ? 76 : 60;
  // A new result set or density invalidates old measurements. This also drops
  // the typed buffers immediately instead of retaining heights from searches.
  const layout = useMemo(
    () => new DynamicRowLayout(results.length, estimatedHeight),
    [results, estimatedHeight],
  );

  const findIndexAtOffset = useCallback(
    // The generation parameter makes range recalculation explicit after a
    // ResizeObserver writes measured heights into the stable layout object.
    (offset: number, layoutGeneration: number) => {
      void layoutGeneration; // generation is an explicit memo invalidation token.
      return layout.indexAtOffset(offset);
    },
    [layout],
  );

  const range = useMemo(() => {
    const overscan = Math.max(360, viewportHeight * 0.75);
    return {
      start: Math.max(0, findIndexAtOffset(Math.max(0, scrollTop - overscan), layoutRevision)),
      end: Math.min(results.length, findIndexAtOffset(scrollTop + viewportHeight + overscan, layoutRevision) + 1),
    };
  }, [findIndexAtOffset, layoutRevision, results.length, scrollTop, viewportHeight]);
  // 键盘从首项跳到末项时不能把两者之间所有行都挂载。视口外的选中项单独
  // 保留一个 option，layout effect 滚动完成后它会回到正常的可视渲染窗口。
  const selectedOutsideRange = isSelectedOutsideVirtualRange(results.length, selectedIndex, range);

  const updateViewport = useCallback(() => {
    if (!listRef.current) return;
    setViewportHeight(listRef.current.clientHeight);
  }, []);

  useLayoutEffect(() => {
    updateViewport();
    const observer = new ResizeObserver(updateViewport);
    if (listRef.current) observer.observe(listRef.current);
    return () => observer.disconnect();
  }, [updateViewport]);

  useLayoutEffect(() => {
    let active = true;
    const observer = new ResizeObserver((entries) => {
      if (!active) return;
      let changed = false;
      for (const entry of entries) {
        const index = Number((entry.target as HTMLElement).dataset.virtualIndex);
        const height = Math.ceil(entry.borderBoxSize[0]?.blockSize ?? entry.contentRect.height);
        changed = layout.updateHeight(index, height) || changed;
      }
      if (changed) setLayoutRevision((previous) => previous + 1);
    });
    observerRef.current = observer;
    listRef.current?.querySelectorAll<HTMLLIElement>('[data-virtual-index]').forEach((element) => observer.observe(element));
    return () => {
      active = false;
      observer.disconnect();
      observerRef.current = null;
    };
  }, [layout]);

  const measureRef = useCallback((element: HTMLLIElement | null) => {
    const observer = observerRef.current;
    if (!element || !observer) return;
    observer.observe(element);
    return () => observer.unobserve(element);
  }, []);

  useLayoutEffect(() => {
    const list = listRef.current;
    if (!list || selectedIndex < 0 || selectedIndex >= results.length) return;
    const itemTop = layout.offsetOf(selectedIndex);
    const itemBottom = layout.offsetOf(selectedIndex + 1);
    const visibleBottom = list.scrollTop + list.clientHeight;
    if (itemTop < list.scrollTop) list.scrollTop = itemTop;
    else if (itemBottom > visibleBottom) list.scrollTop = itemBottom - list.clientHeight;
  }, [layout, layoutRevision, results.length, selectedIndex]);

  return (
    <ul
      id="search-results"
      ref={listRef}
      className={`search-results search-results--virtualized density-${density}`}
      role="listbox"
      onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
    >
      <li aria-hidden="true" role="presentation" style={{ height: layout.totalHeight() }} />
      {results.slice(range.start, range.end).map((result, relativeIndex) => {
        const index = range.start + relativeIndex;
        return (
          <SearchResultRow
            key={result.path}
            result={result}
            index={index}
            selected={index === selectedIndex}
            density={density}
            columns={columns}
            queryKeywords={queryKeywords}
            onHover={onHover}
            onSelect={onSelect}
            onOpen={onOpen}
            onContextMenu={onContextMenu}
            measureRef={measureRef}
            setSize={results.length}
            style={{ position: 'absolute', top: layout.offsetOf(index), left: 0, right: 0 }}
          />
        );
      })}
      {selectedOutsideRange && (
        <SearchResultRow
          key={`selected-${results[selectedIndex].path}`}
          result={results[selectedIndex]}
          index={selectedIndex}
          selected
          density={density}
          columns={columns}
          queryKeywords={queryKeywords}
          onHover={onHover}
          onSelect={onSelect}
          onOpen={onOpen}
          onContextMenu={onContextMenu}
          measureRef={measureRef}
          setSize={results.length}
          style={{ position: 'absolute', top: layout.offsetOf(selectedIndex), left: 0, right: 0 }}
        />
      )}
    </ul>
  );
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
  const [syntaxCat, setSyntaxCat] = useState<string>('all');
  const [showViewSettings, setShowViewSettings] = useState(false);
  const [contextMenu, setContextMenu] = useState<{
    visible: boolean;
    x: number;
    y: number;
    result?: SearchResult;
  }>({ visible: false, x: 0, y: 0 });
  const [renameTarget, setRenameTarget] = useState<{
    visible: boolean;
    result?: SearchResult;
    newName: string;
  }>({ visible: false, newName: '' });
  const renameInputRef = useRef<HTMLInputElement | null>(null);
  const [isPinned, setIsPinned] = useState(false);

  useEffect(() => {
    bridgeRequest<{ pinned: boolean }>('search.isPinned')
      .then(res => {
        if (res && typeof res.pinned === 'boolean') {
          setIsPinned(res.pinned);
        }
      })
      .catch(() => {});
  }, []);

  const togglePin = useCallback(async () => {
    const next = !isPinned;
    setIsPinned(next);
    try {
      await bridgeRequest('search.setPinned', { pinned: next });
      if (next) {
        toast.success(t('search.pinnedToast', 'Search window pinned (Keeps open when clicking outside)'));
      } else {
        toast.info(t('search.unpinnedToast', 'Search window unpinned (Auto-hides on blur)'));
      }
    } catch {
      setIsPinned(!next);
    }
  }, [isPinned, t]);

  const [totalIndexedFiles, setTotalIndexedFiles] = useState<number>(0);

  const filteredSyntaxExamples = useMemo(() => {
    if (syntaxCat === 'all') return SYNTAX_EXAMPLES;
    return SYNTAX_EXAMPLES.filter(item => item.category === syntaxCat);
  }, [syntaxCat]);

  const categories: CategoryFilter[] = useMemo(() => {
    return CATEGORY_DEFS.map(def => ({
      id: def.id,
      label: t(def.labelKey as unknown as 'search.catAll', def.defaultLabel),
      labelKey: def.labelKey,
      defaultLabel: def.defaultLabel,
      prefix: def.prefix,
      icon: def.icon
    }));
  }, [t]);

  const [maxResultLimit, setMaxResultLimit] = useState<number>(() => {
    const saved = localStorage.getItem('easytools_search_max_results');
    return saved !== null ? parseInt(saved, 10) : 0;
  });

  const changeMaxResultLimit = (limit: number) => {
    setMaxResultLimit(limit);
    localStorage.setItem('easytools_search_max_results', String(limit));
  };

  const [searchHistory, setSearchHistory] = useState<{ search: string; searchCount: number; lastSearchDate: number }[]>([]);
  const [isInitialIndexing, setIsInitialIndexing] = useState(false);
  const isInitialIndexingRef = useRef(false);
  const [isServiceStarting, setIsServiceStarting] = useState(false);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<number | null>(null);
  const [dbStats, setDbStats] = useState<{
    dbPath: string;
    dbSize: number;
    timestamp: number;
    totalRecords: number;
    volumeCount: number;
    exists: boolean;
    indexing?: boolean;
    runHistoryCount: number;
    searchHistoryCount: number;
  } | null>(null);
  
  // ── 深度全文搜索冷启动全息感知与会话韧性缓存池 (World-Class Deep Scan Session Cache) ──
  const [lastColdScanDurationSec, setLastColdScanDurationSec] = useState<number>(() => {
    const saved = localStorage.getItem('easytools_last_cold_content_scan_sec');
    return saved ? Math.max(3.0, parseFloat(saved)) : 14.5;
  });
  const [scanElapsedSeconds, setScanElapsedSeconds] = useState<number>(0);
  const scanTimerHandleRef = useRef<{ sequence: number; intervalId: number; startTime: number } | null>(null);

  const stopScanStopwatch = useCallback((targetSeq?: number) => {
    if (scanTimerHandleRef.current) {
      if (targetSeq === undefined || scanTimerHandleRef.current.sequence === targetSeq) {
        window.clearInterval(scanTimerHandleRef.current.intervalId);
        scanTimerHandleRef.current = null;
      }
    }
  }, []);

  const startScanStopwatch = useCallback((seq: number) => {
    stopScanStopwatch();
    setScanElapsedSeconds(0);
    const startTime = Date.now();
    const intervalId = window.setInterval(() => {
      if (scanTimerHandleRef.current?.sequence === seq) {
        setScanElapsedSeconds(+((Date.now() - startTime) / 1000).toFixed(1));
      }
    }, 100);
    scanTimerHandleRef.current = { sequence: seq, intervalId, startTime };
  }, [stopScanStopwatch]);

  const heavyQueryCacheRef = useRef<Map<string, {
    results: SearchResult[];
    elapsedMs: number;
    totalIndexedFiles?: number;
    timestamp: number;
  }>>(new Map());

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

  const checkInitialIndex = useCallback(async (): Promise<boolean> => {
    try {
      const res = await bridgeRequest<{
        dbPath: string;
        dbSize: number;
        timestamp: number;
        totalRecords: number;
        volumeCount: number;
        exists: boolean;
        indexing?: boolean;
        runHistoryCount: number;
        searchHistoryCount: number;
        success: boolean;
      }>('search.getDbStats');
      if (res && res.success) {
        setDbStats(res);
        const isIndexing = res.indexing === true || (res.totalRecords === 0 && res.exists === false);
        if (isIndexing) {
          setIsInitialIndexing(true);
          isInitialIndexingRef.current = true;
          return true;
        } else {
          if (isInitialIndexingRef.current) {
            isInitialIndexingRef.current = false;
            setIsInitialIndexing(false);
            setServiceAvailable(true);
            toast.success(t('search.indexReadyToast', 'Full disk index built and ready!'));
          } else {
            setIsInitialIndexing(false);
            setServiceAvailable(true);
          }
          return false;
        }
      }
    } catch {
      // ignore
    }
    return false;
  }, [t]);

  const refreshDbStats = useCallback(async () => {
    await checkInitialIndex();
  }, [checkInitialIndex]);

  useEffect(() => {
    let active = true;
    let pollTimer: number | undefined;

    void bridgeRequest<{ success: boolean; history: { search: string; searchCount: number; lastSearchDate: number }[] }>('search.getSearchHistory', { limit: 20 })
      .then((res) => {
        if (active && res && res.success && Array.isArray(res.history)) {
          setSearchHistory(res.history);
        }
      })
      .catch(() => undefined);

    const runPoll = async () => {
      const isBusy = await checkInitialIndex();
      if (!active) return;
      if (isBusy) {
        pollTimer = window.setTimeout(runPoll, 500);
      }
    };

    let lastFocusSyncTick = 0;
    const onFocusEvt = () => {
      const now = Date.now();
      if (now - lastFocusSyncTick > 3000) {
        lastFocusSyncTick = now;
        void bridgeRequest('search.warmup').then(() => {
          void bridgeRequest('search.sync').catch(() => {});
          void bridgeRequest<SearchResponse>('search.query', { query: '' }).then((res) => {
            if (res && res.totalIndexedFiles) {
              setTotalIndexedFiles(res.totalIndexedFiles);
            }
          }).catch(() => {});
        }).catch(() => {});
      }
      void runPoll();
    };
    window.addEventListener('easytools:focusSearch', onFocusEvt);

    return () => {
      active = false;
      if (pollTimer) window.clearTimeout(pollTimer);
      window.removeEventListener('easytools:focusSearch', onFocusEvt);
    };
  }, [checkInitialIndex]);

  const removeHistoryItem = async (e: React.MouseEvent, s: string) => {
    e.stopPropagation();
    await bridgeRequest('search.removeSearchHistory', { search: s });
    setSearchHistory(prev => prev.filter(item => item.search !== s));
  };

  const clearAllHistory = async () => {
    await bridgeRequest('search.clearSearchHistory');
    setSearchHistory([]);
    toast.success(t('search.historyCleared', 'Search history cleared'));
    void refreshDbStats();
    void refreshHistory();
  };

  const [sortField, setSortField] = useState<SortField>(() => {
    const saved = localStorage.getItem('easytools_search_sort_field');
    if (saved === 'modified' || saved === 'name' || saved === 'size' || saved === 'created' || saved === 'relevance') {
      return saved;
    }
    return 'modified';
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
  // 输入法组字状态。组字期间 input 的 value 会随每个字母变化，但那些中间态
  // 并不是用户想搜的内容。
  const [isComposing, setIsComposing] = useState(false);

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
  const [searchElapsedMs, setSearchElapsedMs] = useState<number>(0);

  const changeSearchMode = (mode: SearchMode) => {
    setSearchMode(mode);
    localStorage.setItem('easytools_search_default_mode', mode);
  };

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
      heavyQueryCacheRef.current.clear();
      // 如果是禁用该格式，立即从当前可见列表中剔除对应文件，0 毫秒即时响应
      if (next.includes(cleanExt)) {
        startTransition(() => {
          setResults(currentResults => currentResults.filter(item => {
            if (!item.path) return true;
            const itemExt = item.path.split('.').pop()?.toLowerCase() || '';
            return itemExt !== cleanExt;
          }));
        });
      }
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
      heavyQueryCacheRef.current.clear();
      // 如果是关闭该大类，立即从当前可见列表中剔除该分类下的全部文件，0 毫秒即时响应
      if (!enableAll) {
        const catExtSet = new Set(cat.extensions.map(e => e.toLowerCase()));
        startTransition(() => {
          setResults(currentResults => currentResults.filter(item => {
            if (!item.path) return true;
            const itemExt = item.path.split('.').pop()?.toLowerCase() || '';
            return !catExtSet.has(itemExt);
          }));
        });
      }
      return next;
    });
  };

  const addCustomContentFormat = (rawInput: string) => {
    const parts = rawInput
      .split(/[,;\s，；、|]+/)
      .map(p => p.toLowerCase().replace(/^\./, '').trim())
      .filter(Boolean);
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
    heavyQueryCacheRef.current.clear();
    setNewFormatInput('');
    toast.success(t('search.customExtAddedToast', 'Added {{exts}} to content search support list', { exts: parts.map(p => '.' + p).join(', ') }));
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
    heavyQueryCacheRef.current.clear();
  };

  const resetContentFormats = () => {
    setDisabledContentFormats([]);
    setCustomContentFormats([]);
    localStorage.removeItem('easytools_search_disabled_formats');
    localStorage.removeItem('easytools_search_custom_formats');
    toast.success(t('search.customExtResetToast', 'Content search format configuration reset to default'));
  };

  const rebuildIndex = useCallback(async () => {
    setIsRebuilding(true);
    toast.loading(t('search.rebuildingToast', 'Rescanning NTFS index and saving snapshot...'), { id: 'rebuild-idx' });
    try {
      await bridgeRequest('search.rebuildIndex');
      toast.success(t('search.rebuildSuccessToast', 'Full-disk index rescanned and saved to snapshot!'), { id: 'rebuild-idx' });
      void refreshDbStats();
      void refreshHistory();
    } catch {
      toast.error(t('search.rebuildError', 'Index rebuild and snapshot save failed'), { id: 'rebuild-idx' });
    } finally {
      setTimeout(() => setIsRebuilding(false), 1500);
    }
  }, [refreshDbStats, refreshHistory, t]);

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
    if (e.button !== 0) return;
    e.preventDefault();
    e.stopPropagation();
    void bridgeRequest('search.startResize', { edge: 'bottom_right', direction: 'bottom_right' }).catch(() => undefined);
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
    toast.success(t('search.toastResetLayout', 'Default columns and layout restored'));
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
    };
    window.addEventListener('easytools:focusSearch', onFocusEvt);

    const handleGlobalClick = (e: MouseEvent) => {
      const target = e.target as HTMLElement | null;
      if (!target?.closest('.search-context-menu')) {
        setContextMenu({ visible: false, x: 0, y: 0 });
      }
    };
    window.addEventListener('click', handleGlobalClick);
    window.addEventListener('pointerdown', handleGlobalClick);

    return () => {
      clearTimeout(t1);
      clearTimeout(t2);
      clearTimeout(t3);
      clearTimeout(t4);
      window.removeEventListener('easytools:focusSearch', onFocusEvt);
      window.removeEventListener('click', handleGlobalClick);
      window.removeEventListener('pointerdown', handleGlobalClick);
    };
  }, []);

  useEffect(() => {
    const trimmed = query.trim();
    if (!trimmed) {
      if (retryTimerRef.current) {
        window.clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
      return;
    }
    // 输入法组字期间 value 会随每个字母变化，此时发出的查询没有任何意义。
    if (isComposing) return;

    const sequence = ++requestSequence.current;
    const debounceMs = resolveDebounceMs({ query: trimmed, activeCategory, searchMode });

    const runQuery = async () => {
      if (sequence !== requestSequence.current) return;
      if (isEmptyContentSyntax(trimmed)) {
        startTransition(() => {
          setResults([]);
        });
        return;
      }

      const effectiveMode = (activeCategory === 'content') ? 'content' : searchMode;
      const isContentSearch = effectiveMode === 'content' || trimmed.toLowerCase().startsWith('content:') || trimmed.startsWith('内容:');
      const cacheKey = `${effectiveMode}:${trimmed}:${enabledDrives.slice().sort().join(',')}:${maxResultLimit}:${excludeGitAndModules ? 'exDev' : 'all'}:${disabledContentFormats.slice().sort().join(',')}:${customContentFormats.slice().sort().join(',')}`;

      // ── 1. 0ms 瞬间命中会话韧性缓存 (防止用户关开窗口丢失 14s 扫描结果) ──
      const cached = heavyQueryCacheRef.current.get(cacheKey);
      if (cached && (Date.now() - cached.timestamp < 45000)) {
        startTransition(() => {
          setResults(cached.results);
          setSelectedIndex(0);
          setSearchElapsedMs(cached.elapsedMs);
          if (cached.totalIndexedFiles !== undefined) {
            setTotalIndexedFiles(cached.totalIndexedFiles);
          }
          setLoading(false);
        });
        return;
      }

      const loadingTimer = window.setTimeout(() => {
        if (sequence === requestSequence.current) setLoading(true);
      }, 80);

      // ── 2. 深度扫描实时秒表计时器与陈旧列表清空 (杜绝搜内容却显示旧文件名) ──
      if (isContentSearch) {
        startTransition(() => {
          setResults([]);
          setSelectedIndex(0);
        });
        startScanStopwatch(sequence);
      } else {
        stopScanStopwatch(sequence);
        setScanElapsedSeconds(0);
      }

      const excludesList: string[] = [];
      if (excludeGitAndModules) {
        excludesList.push('$Recycle.Bin', 'System Volume Information', 'node_modules', '.git', '__pycache__', 'npm-cache', 'go-build', '.gradle', 'pip\\cache');
      }

      try {
        const response = await bridgeRequest<SearchResponse>('search.query', { 
          query: trimmed,
          queryId: nextQueryId(),
          searchMode: effectiveMode,
          limit: maxResultLimit,
          drives: enabledDrives.length > 0 ? enabledDrives : undefined,
          excludes: excludesList.length > 0 ? excludesList : undefined,
          excludeHidden: excludeHidden,
          contentCustomExts: customContentFormats,
          contentDisabledExts: disabledContentFormats
        });

        // ── 3. 结果强制入缓存池 (即使 sequence 已过时，下次呼出也能瞬间命中) ──
        if (response && Array.isArray(response.results) && response.results.length > 0) {
          heavyQueryCacheRef.current.set(cacheKey, {
            results: response.results,
            elapsedMs: response.elapsedMs ?? 0,
            totalIndexedFiles: response.totalIndexedFiles,
            timestamp: Date.now(),
          });
        }

        // ── 4. 自适应学习用户真实冷扫描耗时基准 ──
        if (response && response.elapsedMs && response.elapsedMs >= 3500) {
          const actualColdSec = +(response.elapsedMs / 1000).toFixed(1);
          setLastColdScanDurationSec(actualColdSec);
          localStorage.setItem('easytools_last_cold_content_scan_sec', String(actualColdSec));
        }

        if (sequence !== requestSequence.current) return;
        window.clearTimeout(loadingTimer);

        // 如果服务正在启动/就绪中，展示优雅等待并自动触发自愈重试
        if (response.status === 'starting' || (!response.available && response.status !== 'unavailable')) {
          startTransition(() => {
            setIsServiceStarting(true);
            setServiceAvailable(true);
          });
          if (retryCountRef.current < 25) {
            retryCountRef.current += 1;
            const nextDelay = Math.min(250 + retryCountRef.current * 100, 1000);
            retryTimerRef.current = window.setTimeout(() => {
              void runQuery();
            }, nextDelay);
          } else {
            stopScanStopwatch(sequence);
            startTransition(() => {
              setIsServiceStarting(false);
              setServiceAvailable(false);
            });
          }
          return;
        }

        // 收到最终就绪结果，安全停止当前 sequence 的秒表
        stopScanStopwatch(sequence);

        // 服务正常就绪并返回数据
        retryCountRef.current = 0;
        startTransition(() => {
          setIsServiceStarting(false);
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
        stopScanStopwatch(sequence);
        window.clearTimeout(loadingTimer);
        startTransition(() => {
          setResults([]);
        });
      } finally {
        window.clearTimeout(loadingTimer);
        if (sequence === requestSequence.current) {
          stopScanStopwatch(sequence);
          setLoading(false);
        }
      }
    };

    retryCountRef.current = 0;
    if (retryTimerRef.current) {
      window.clearTimeout(retryTimerRef.current);
      retryTimerRef.current = null;
    }

    const timer = window.setTimeout(() => {
      void runQuery();
    }, debounceMs);

    return () => {
      window.clearTimeout(timer);
      if (retryTimerRef.current) {
        window.clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
    };
  }, [
    query,
    isComposing,
    activeCategory,
    searchMode,
    enabledDrives,
    maxResultLimit,
    excludeGitAndModules,
    excludeHidden,
    customContentFormats,
    disabledContentFormats,
    startScanStopwatch,
    stopScanStopwatch
  ]);

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
  const sortedResults = useMemo(() => {
    return getSortedResults(results, sortField, sortDirection, foldersFirst, groupByType);
  }, [results, sortField, sortDirection, foldersFirst, groupByType]);

  const totalResultSize = useMemo(() => {
    return sortedResults.reduce((acc, item) => acc + (item.isDirectory ? 0 : (Number(item.size) || 0)), 0);
  }, [sortedResults]);

  const exportResultsToCsv = useCallback(() => {
    if (sortedResults.length === 0) {
      toast.error(t('search.toastNoExport', 'No search results to export'));
      return;
    }

    const headers = [t('search.csvName', 'File Name'), t('search.csvPath', 'Full Path'), t('search.csvType', 'Type'), t('search.csvSizeBytes', 'Size (Bytes)'), t('search.csvSizeHuman', 'Size (Readable)'), t('search.csvModified', 'Modified Time'), t('search.csvCreated', 'Created Time')];
    const rows = sortedResults.map((item) => {
      const isDir = item.isDirectory;
      const typeStr = isDir ? t('search.folderType', 'Folder') : (item.name.includes('.') ? item.name.split('.').pop()?.toUpperCase() || t('search.fileType', 'File') : t('search.fileType', 'File'));
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

    toast.success(t('search.toastExportSuccess', { count: sortedResults.length, defaultValue: `Successfully exported ${sortedResults.length} search results to CSV file` }));
  }, [sortedResults, query, t]);

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

  const handleSelectSort = useCallback((field: SortField) => {
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
  }, [sortField, sortDirection]);

  const updateQuery = useCallback((next: string) => {
    setQuery(next);
    setActionError('');
    if (!next.trim()) {
      setIsServiceStarting(false);
    }

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
  }, []);

  const selectCategory = useCallback((cat: CategoryFilter) => {
    if (cat.id === 'content') {
      startTransition(() => {
        setResults([]);
        setSelectedIndex(0);
      });
    }
    if (!cat.prefix) {
      let cleaned = query;
      CATEGORY_DEFS.forEach(c => {
        if (c.prefix && cleaned.startsWith(c.prefix)) {
          cleaned = cleaned.slice(c.prefix.length);
        }
      });
      updateQuery(cleaned.trimStart());
    } else {
      let cleaned = query;
      CATEGORY_DEFS.forEach(c => {
        if (c.prefix && cleaned.startsWith(c.prefix)) {
          cleaned = cleaned.slice(c.prefix.length);
        }
      });
      updateQuery(cat.prefix + cleaned.trimStart());
    }
    inputRef.current?.focus();
  }, [query, updateQuery]);

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
      await bridgeRequest('search.openFile', { filepath: result.path, path: result.path });
      if (!isPinned) hide();
    } catch {
      try {
        await bridgeRequest('system.openFile', { path: result.path, filepath: result.path });
        if (!isPinned) hide();
      } catch {
        setActionError(t('search.openFailed', 'Could not open this result'));
      }
    }
  }, [hide, isPinned, t]);

  const openFolderResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      void bridgeRequest('search.recordRun', { path: result.path });
      await bridgeRequest('search.openFolder', { filepath: result.path, path: result.path });
      if (!isPinned) hide();
    } catch {
      try {
        await bridgeRequest('system.openFolder', { path: result.path, filepath: result.path });
        if (!isPinned) hide();
      } catch {
        setActionError(t('search.openFolderFailed'));
      }
    }
  }, [hide, isPinned, t]);

  const copyPathResult = useCallback((result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    const doNativeCopy = async () => {
      try {
        await bridgeRequest('system.copyText', { text: result.path });
        toast.success(t('search.copiedPath', 'File path copied to clipboard'));
      } catch {
        try {
          const textarea = document.createElement('textarea');
          textarea.value = result.path;
          textarea.style.position = 'fixed';
          textarea.style.opacity = '0';
          document.body.appendChild(textarea);
          textarea.select();
          document.execCommand('copy');
          document.body.removeChild(textarea);
          toast.success(t('search.copiedPath', 'File path copied to clipboard'));
        } catch {
          setActionError(t('search.copyFailed'));
        }
      }
    };

    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(result.path).then(() => {
        toast.success(t('search.copiedPath', 'File path copied to clipboard'));
      }).catch(() => {
        void doNativeCopy();
      });
    } else {
      void doNativeCopy();
    }
  }, [t]);

  const pinResult = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    if (result.isDirectory || !IMAGE_EXTENSIONS.test(result.name)) {
      toast.error(t('search.toastImagePinOnly', 'Only image files support desktop pinning'));
      return;
    }
    try {
      await bridgeRequest('capture.pinImageFile', { path: result.path });
      if (!isPinned) hide();
    } catch {
      toast.error(t('search.toastPinFail', 'Pin failed'));
    }
  }, [hide, isPinned, t]);

  const openResultAsAdmin = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      void bridgeRequest('search.recordRun', { path: result.path });
      await bridgeRequest('search.openFileAsAdmin', { filepath: result.path, path: result.path });
      if (!isPinned) hide();
    } catch {
      try {
        await bridgeRequest('system.openFileAsAdmin', { path: result.path, filepath: result.path });
        if (!isPinned) hide();
      } catch {
        setActionError(t('search.errRunAsAdmin', 'Failed to run as administrator'));
      }
    }
  }, [hide, isPinned, t]);

  const showFileProperties = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      await bridgeRequest('search.showFileProperties', { filepath: result.path, path: result.path });
    } catch {
      try {
        await bridgeRequest('system.showFileProperties', { path: result.path, filepath: result.path });
      } catch {
        setActionError(t('search.errFileProperties', 'Unable to open file properties'));
      }
    }
  }, [t]);

  const copyText = useCallback((text: string) => {
    if (!text) return;
    const doNative = async () => {
      try {
        await bridgeRequest('system.copyText', { text });
        toast.success(t('search.toastCopied', 'Copied to clipboard'));
      } catch {
        try {
          const textarea = document.createElement('textarea');
          textarea.value = text;
          textarea.style.position = 'fixed';
          textarea.style.opacity = '0';
          document.body.appendChild(textarea);
          textarea.select();
          document.execCommand('copy');
          document.body.removeChild(textarea);
          toast.success(t('search.toastCopied', 'Copied to clipboard'));
        } catch {
          toast.error(t('search.toastCopyFail', 'Failed to copy to clipboard'));
        }
      }
    };

    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(() => {
        toast.success(t('search.toastCopied', 'Copied to clipboard'));
      }).catch(() => {
        void doNative();
      });
    } else {
      void doNative();
    }
  }, [t]);

  const openWithNotepad = useCallback(async (result: SearchResult | undefined) => {
    if (!result) return;
    setActionError('');
    try {
      void bridgeRequest('search.recordRun', { path: result.path });
      await bridgeRequest('search.openWithNotepad', { filepath: result.path, path: result.path });
      if (!isPinned) hide();
    } catch {
      try {
        await bridgeRequest('system.openWithNotepad', { path: result.path, filepath: result.path });
        if (!isPinned) hide();
      } catch {
        setActionError(t('search.errOpenNotepad', 'Unable to open file with Notepad'));
      }
    }
  }, [hide, isPinned, t]);

  const startRename = useCallback((result: SearchResult | undefined) => {
    if (!result) return;
    setContextMenu({ visible: false, x: 0, y: 0 });
    setRenameTarget({
      visible: true,
      result,
      newName: result.name
    });
    setTimeout(() => {
      if (renameInputRef.current) {
        renameInputRef.current.focus();
        const dotIdx = result.isDirectory ? -1 : result.name.lastIndexOf('.');
        if (dotIdx > 0) {
          renameInputRef.current.setSelectionRange(0, dotIdx);
        } else {
          renameInputRef.current.select();
        }
      }
    }, 50);
  }, []);

  const confirmRename = useCallback(async () => {
    if (!renameTarget.result || !renameTarget.newName.trim()) return;
    const oldName = renameTarget.result.name;
    const newName = renameTarget.newName.trim();
    if (newName === oldName) {
      setRenameTarget({ visible: false, newName: '' });
      return;
    }
    if (/[\\/:*?"<>|]/.test(newName)) {
      toast.error(t('search.toastRenameInvalidChar', 'Filename cannot contain \\ / : * ? " < > |'));
      return;
    }
    try {
      const res = await bridgeRequest<{ success: boolean; newPath?: string; error?: string }>('search.renamePath', {
        oldPath: renameTarget.result.path,
        path: renameTarget.result.path,
        newName
      });
      if (res?.success && res.newPath) {
        const newP = res.newPath;
        setResults(prev => prev.map(item => item.path === renameTarget.result?.path ? { ...item, name: newName, path: newP } : item));
        toast.success(t('search.toastRenameSuccess', { name: newName, defaultValue: `Renamed to "${newName}"` }));
        setRenameTarget({ visible: false, newName: '' });
      } else {
        toast.error(res?.error || t('search.toastRenameFail', 'Rename failed, please check if the file is in use'));
      }
    } catch {
      try {
        const res = await bridgeRequest<{ success: boolean; newPath?: string; error?: string }>('system.renamePath', {
          oldPath: renameTarget.result.path,
          path: renameTarget.result.path,
          newName
        });
        if (res?.success && res.newPath) {
          const newP = res.newPath;
          setResults(prev => prev.map(item => item.path === renameTarget.result?.path ? { ...item, name: newName, path: newP } : item));
          toast.success(t('search.toastRenameSuccess', { name: newName, defaultValue: `Renamed to "${newName}"` }));
          setRenameTarget({ visible: false, newName: '' });
        } else {
          toast.error(res?.error || t('search.toastRenameFail', 'Rename failed, please check if the file is in use'));
        }
      } catch {
        toast.error(t('search.toastRenameFail', 'Rename failed, please check if the file is in use'));
      }
    }
  }, [renameTarget, t]);

  const handleUnifiedKeyDown = useCallback((event: KeyboardEvent<HTMLElement> | globalThis.KeyboardEvent) => {
    const target = event.target as HTMLElement | null;
    const isSearchInput = target === inputRef.current;
    const isOtherInput = target && target !== inputRef.current && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable);

    // 1. Escape 拥有最高全局优先级（逐层退出浮层 -> 最终退出搜索窗口）
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      if (renameTarget.visible) {
        setRenameTarget({ visible: false, newName: '' });
        inputRef.current?.focus();
      } else if (contextMenu.visible) {
        setContextMenu({ visible: false, x: 0, y: 0 });
        inputRef.current?.focus();
      } else if (showSortMenu) {
        setShowSortMenu(false);
        inputRef.current?.focus();
      } else if (showSyntaxHelp) {
        setShowSyntaxHelp(false);
        inputRef.current?.focus();
      } else if (showViewSettings) {
        setShowViewSettings(false);
        inputRef.current?.focus();
      } else {
        hide();
      }
      return;
    }

    // 2. F5 / Ctrl+R 全局重新构建全盘索引
    if (event.key === 'F5' || (event.ctrlKey && (event.key === 'r' || event.key === 'R'))) {
      event.preventDefault();
      void rebuildIndex();
      return;
    }

    // 3. F1 全局语法帮助
    if (event.key === 'F1') {
      event.preventDefault();
      setShowSyntaxHelp(prev => !prev);
      return;
    }

    // 4. Ctrl+P 全局切换窗口图钉固定状态 (Pin/Unpin)
    if ((event.ctrlKey || event.metaKey) && (event.key === 'p' || event.key === 'P')) {
      event.preventDefault();
      void togglePin();
      return;
    }

    // 若当前焦点在重命名输入框或自定义格式输入框中，允许其独立输入与光标移动
    if (isOtherInput) {
      return;
    }

    // 4. F2 全局重命名当前选中项
    if (event.key === 'F2') {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) startRename(current);
      return;
    }

    // 5. Ctrl+Shift+... 排序快捷键
    if (event.ctrlKey && event.shiftKey) {
      const k = event.key.toLowerCase();
      if (k === 'd') { event.preventDefault(); handleSelectSort('modified'); return; }
      if (k === 'n') { event.preventDefault(); handleSelectSort('name'); return; }
      if (k === 's') { event.preventDefault(); handleSelectSort('size'); return; }
      if (k === 'r') { event.preventDefault(); handleSelectSort('relevance'); return; }
    }

    // 6. 结果列表上下导航 (无论是焦点在搜索框还是在浮窗任意位置)
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => (prev + 1) % sortedResults.length);
      }
      return;
    }
    if (event.key === 'ArrowUp') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => (prev - 1 + sortedResults.length) % sortedResults.length);
      }
      return;
    }
    if (event.key === 'PageDown') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => Math.min(prev + 8, sortedResults.length - 1));
      }
      return;
    }
    if (event.key === 'PageUp') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => Math.max(prev - 8, 0));
      }
      return;
    }
    if (event.key === 'Home' && !isSearchInput) {
      event.preventDefault();
      if (sortedResults.length > 0) setSelectedIndex(0);
      return;
    }
    if (event.key === 'End' && !isSearchInput) {
      event.preventDefault();
      if (sortedResults.length > 0) setSelectedIndex(sortedResults.length - 1);
      return;
    }

    // 7. 回车执行 (Enter / Alt+Enter / Ctrl+Enter)
    if (event.key === 'Enter') {
      if (sortedResults.length === 0) {
        if (query.trim() && activeCategory === 'all') {
          event.preventDefault();
          const contentCat = categories.find(c => c.id === 'content') || categories[1];
          if (contentCat) selectCategory(contentCat);
        }
        return;
      }
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (!current) return;
      if (event.altKey) {
        void showFileProperties(current);
      } else if (event.ctrlKey) {
        void openFolderResult(current);
      } else {
        void openResult(current);
      }
      return;
    }

    // 8. 菜单 Shift+F10 / ContextMenu
    if ((event.shiftKey && event.key === 'F10') || event.key === 'ContextMenu') {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) {
        const itemEl = document.getElementById(`search-result-${selectedIndex}`);
        const rect = itemEl?.getBoundingClientRect();
        const x = rect ? rect.left + 120 : 120;
        const y = rect ? rect.bottom + 4 : 120;
        setContextMenu({
          visible: true,
          x: Math.min(x, window.innerWidth - 240),
          y: Math.min(y, window.innerHeight - 320),
          result: current
        });
      }
      return;
    }

    // 9. 快捷复制与导出
    if (event.key.toLowerCase() === 'c' && event.ctrlKey && !isSearchInput) {
      if (sortedResults.length > 0 && selectedIndex >= 0 && selectedIndex < sortedResults.length) {
        event.preventDefault();
        copyPathResult(sortedResults[selectedIndex]);
        return;
      }
    }
    if (event.key.toLowerCase() === 'e' && event.ctrlKey) {
      event.preventDefault();
      exportResultsToCsv();
      return;
    }
    if (event.key.toLowerCase() === 'p' && event.ctrlKey) {
      event.preventDefault();
      void pinResult(sortedResults[selectedIndex]);
      return;
    }

    // 10. 用户在浮窗空白处或列表处按下了普通输入字符，自动归位聚焦搜索输入框
    if (!isSearchInput && event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) {
      inputRef.current?.focus();
    }
  }, [
    hide, rebuildIndex, sortedResults, selectedIndex, query, activeCategory,
    renameTarget.visible, contextMenu.visible, showSortMenu, showSyntaxHelp, showViewSettings,
    startRename, openResult, openFolderResult, showFileProperties, pinResult, copyPathResult, exportResultsToCsv,
    handleSelectSort, selectCategory, categories, togglePin
  ]);

  useEffect(() => {
    const handleGlobal = (e: globalThis.KeyboardEvent) => {
      handleUnifiedKeyDown(e);
    };
    window.addEventListener('keydown', handleGlobal, true);
    return () => {
      window.removeEventListener('keydown', handleGlobal, true);
    };
  }, [handleUnifiedKeyDown]);

  const nameFlex = columns.find(c => c.id === 'name')?.flex ?? 35;
  const pathFlex = columns.find(c => c.id === 'path')?.flex ?? 45;

  // 结果行只依赖这一个稳定对象，列设置不变时 memo 就不会被打破。
  const columnLayout = useMemo<ColumnLayout>(() => {
    const visible = (id: ColumnId) => columns.find(c => c.id === id)?.visible ?? true;
    return {
      name: visible('name'),
      ext: visible('ext'),
      parent: visible('parent'),
      path: visible('path'),
      size: visible('size'),
      modified: visible('modified'),
      created: visible('created'),
      snippets: visible('snippets'),
      nameFlex: columns.find(c => c.id === 'name')?.flex ?? 35,
      pathFlex: columns.find(c => c.id === 'path')?.flex ?? 45,
    };
  }, [columns]);

  const handleRowHover = useCallback((index: number) => {
    setSelectedIndex(index);
  }, []);

  const handleRowSelect = useCallback((index: number) => {
    setSelectedIndex(index);
    setContextMenu((prev) => (prev.visible ? { visible: false, x: 0, y: 0 } : prev));
  }, []);

  const handleRowOpen = useCallback((result: SearchResult) => {
    void openResult(result);
  }, [openResult]);

  const handleRowContextMenu = useCallback((event: ReactMouseEvent, index: number, result: SearchResult) => {
    event.preventDefault();
    event.stopPropagation();
    setSelectedIndex(index);
    if (event.shiftKey) {
      const dpr = window.devicePixelRatio || 1;
      const sx = Math.round(event.screenX * dpr);
      const sy = Math.round(event.screenY * dpr);
      setTimeout(() => {
        void bridgeRequest('search.showShellContextMenu', {
          filepath: result.path,
          path: result.path,
          x: sx,
          y: sy,
        });
      }, 30);
      return;
    }
    const menuWidth = 240;
    const menuHeight = 310;
    const x = Math.min(event.clientX, window.innerWidth - menuWidth - 10);
    const y = Math.min(event.clientY, window.innerHeight - menuHeight - 10);
    setContextMenu({
      visible: true,
      x: Math.max(10, x),
      y: Math.max(10, y),
      result,
    });
  }, []);

  return (
    <main className={`search-app ${showViewSettings ? 'search-app--view-settings-open' : ''}`}>
      <section className="search-container" aria-label={t('search.title', 'Quick file search')}>
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
                ? t('search.placeholderContent', 'Search document and code full text (Word/Excel/PDF/Code/Text)... [F1 Syntax]')
                : searchMode === 'both'
                ? t('search.placeholderHybrid', 'Hybrid Search: Match both filename and document contents... [F1 Syntax]')
                : searchMode === 'content'
                ? t('search.placeholderContentOnly', 'Full-Text Search: Search inside documents and code... [F1 Syntax]')
                : t('search.placeholder', 'Search filename, wildcard (*.txt), ext:png, or keywords... [Use content: for full-text]')
            }
            value={query}
            onChange={(event) => updateQuery(event.target.value)}
            onCompositionStart={() => setIsComposing(true)}
            onCompositionEnd={(event) => {
              setIsComposing(false);
              updateQuery(event.currentTarget.value);
            }}
            role="combobox"
            aria-expanded={results.length > 0}
            aria-controls="search-results"
            aria-activedescendant={sortedResults[selectedIndex] ? `search-result-${selectedIndex}` : undefined}
            spellCheck={false}
          />
          {(loading || isInitialIndexing || isServiceStarting) && <span className="search-loading" aria-label={t('common.loading', 'Loading...')} />}
          
          <button
            className={`search-help-btn ${isPinned ? 'search-help-btn--pinned' : ''}`}
            onClick={() => void togglePin()}
            title={isPinned ? t('search.unpinTitle', 'Unpin Window (Ctrl+P · Restore auto-hide on blur)') : t('search.pinTitle', 'Pin Window (Ctrl+P · Keep always on top & never auto-hide)')}
            type="button"
          >
            <Pin size={17} className="search-pin-icon" />
          </button>

          <button
            className="search-help-btn search-drag-btn"
            onMouseDown={() => void bridgeRequest('search.startDrag')}
            onDoubleClick={() => {
              void bridgeRequest('search.resetPlacement');
              setWindowSize({ width: 760, height: 520 });
              toast.success(t('search.resetPlacementToast', 'Reset to default center and 760×520 size'));
            }}
            title={t('search.dragMoveTitle', 'Hold and drag to move window · Double click to reset center')}
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
            title={t('search.viewPrefTitle', 'View and column preferences (Window size, density, columns)')}
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
            title={t('search.syntaxGuideTitle', 'Search syntax and expression guide (F1)')}
            type="button"
          >
            <HelpCircle size={18} />
          </button>
        </div>

        <div className="search-categories-bar">
          <div className="search-categories">
            {categories.map(cat => (
              <button
                key={cat.id}
                className={`category-pill ${activeCategory === cat.id ? 'category-pill--active' : ''}`}
                onClick={() => selectCategory(cat)}
                type="button"
              >
                {cat.icon && <cat.icon size={13} className="category-pill-icon" />}
                <span>{cat.label}</span>
              </button>
            ))}
          </div>

          <div className="search-bar-right-group">
            {/* 全盘已索引总数与耗时统计胶囊 */}
            <div 
              className="search-stats-pill" 
              title={t('search.statsPillTip', 'Indexed {{total}} files across all disks, latest search elapsed {{ms}}ms', { total: totalIndexedFiles ? totalIndexedFiles.toLocaleString() : '1,000,000+', ms: searchElapsedMs })}
            >
              <span className="search-stats-dot" />
              {sortedResults.length > 0 ? (
                <span><strong>{sortedResults.length}</strong> / {t('search.statsFullDisk', 'Total {{total}} · {{ms}}ms', { total: totalIndexedFiles ? (totalIndexedFiles > 10000 ? (totalIndexedFiles / 10000).toFixed(1) + 'w' : totalIndexedFiles.toString()) : '--', ms: searchElapsedMs })}</span>
              ) : (
                <span>{t('search.totalFilesCount', 'Total {{count}} files', { count: totalIndexedFiles || 0 })}</span>
              )}
            </div>

            {/* 导出当前搜索结果为 CSV */}
            {sortedResults.length > 0 && (
              <button
                type="button"
                className="search-action-pill-btn"
                onClick={exportResultsToCsv}
                title={t('search.exportCsvTip', 'Export search results to CSV / Excel (Ctrl+E) - {{count}} items', { count: sortedResults.length })}
              >
                <FileSpreadsheet size={13} />
                <span>{t('search.exportCsv', 'Export CSV')} ({sortedResults.length})</span>
              </button>
            )}

            {/* ── 极客排序双区域分裂微胶囊 (Split Pill) ── */}
            <div className="search-sort-wrapper" ref={sortDropdownRef}>
              <div className={`sort-split-pill ${sortField !== 'relevance' ? 'sort-split-pill--active' : ''}`}>
                <button
                  type="button"
                  className="sort-split-main"
                  onClick={() => setShowSortMenu(prev => !prev)}
                  title={t('search.sortTitle', 'Sort By (Ctrl+Shift+D Date / N Name / S Size / R Relevance)')}
                >
                  {sortField === 'modified' ? <Clock size={12} /> :
                   sortField === 'name' ? <ArrowDownAZ size={12} /> :
                   sortField === 'size' ? <HardDrive size={12} /> :
                   sortField === 'created' ? <Calendar size={12} /> :
                   <Zap size={12} />}
                  <span>
                    {sortField === 'relevance' ? t('search.sortRelevance', 'Smart Relevance') :
                     sortField === 'modified' ? t('search.sortModified', 'Modified Time') :
                     sortField === 'name' ? t('search.sortName', 'File Name') :
                     sortField === 'size' ? t('search.sortSize', 'File Size') :
                     t('search.sortCreated', 'Created Time')}
                  </span>
                  <ChevronDown size={11} className={`sort-chevron ${showSortMenu ? 'sort-chevron--open' : ''}`} />
                </button>
                {sortField !== 'relevance' && (
                  <button
                    type="button"
                    className="sort-split-dir-toggle"
                    onClick={toggleSortDirection}
                    title={t('search.sortDirectionTip', 'Currently sorted in {{dir}}, click to toggle', { dir: sortDirection === 'desc' ? t('search.sortDescName', 'descending (newest / largest first)') : t('search.sortAscName', 'ascending (oldest / smallest first)') })}
                  >
                    <span className="sort-dir-icon">{sortDirection === 'desc' ? '↓' : '↑'}</span>
                  </button>
                )}
              </div>

              {showSortMenu && (
                <div className="sort-dropdown-menu">
                  <div className="sort-menu-header">
                    <span>{t('search.sortMenuTitle', 'Sort Column & Direction')}</span>
                  </div>
                  <div className="sort-menu-items-group">
                    {/* 智能相关度 */}
                    <div
                      className={`sort-menu-row ${sortField === 'relevance' ? 'sort-menu-row--active' : ''}`}
                      onClick={() => handleSetSortDirect('relevance', 'desc')}
                    >
                      <div className="sort-row-left">
                        <Zap size={13} />
                        <span>{t('search.sortRelevance', 'Relevance')}</span>
                      </div>
                      {sortField === 'relevance' && <Check size={12} className="sort-active-check" />}
                    </div>

                    {/* 修改时间 */}
                    <div className={`sort-menu-row ${sortField === 'modified' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('modified', sortDirection)}>
                        <Clock size={13} />
                        <span>{t('search.colModified', 'Modified Time')}</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'modified' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('modified', 'desc'); }}
                          title={t('search.sortNewest', 'Newest to oldest')}
                        >
                          {t('search.sortNewestFirst', 'Newest First ↓')}
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'modified' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('modified', 'asc'); }}
                          title={t('search.sortOldest', 'Oldest to newest')}
                        >
                          {t('search.sortOldestFirst', 'Oldest First ↑')}
                        </button>
                      </div>
                    </div>

                    {/* 文件名 */}
                    <div className={`sort-menu-row ${sortField === 'name' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('name', sortDirection)}>
                        <ArrowDownAZ size={13} />
                        <span>{t('search.sortName', 'Filename')}</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'name' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('name', 'asc'); }}
                          title={t('search.sortAz', 'A to Z')}
                        >
                          A→Z ↓
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'name' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('name', 'desc'); }}
                          title={t('search.sortZa', 'Z to A')}
                        >
                          Z→A ↑
                        </button>
                      </div>
                    </div>

                    {/* 文件大小 */}
                    <div className={`sort-menu-row ${sortField === 'size' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('size', sortDirection)}>
                        <HardDrive size={13} />
                        <span>{t('search.colSize', 'Size')}</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'size' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('size', 'desc'); }}
                          title={t('search.sortLargest', 'Largest first')}
                        >
                          {t('search.sortLargestFirst', 'Largest First ↓')}
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'size' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('size', 'asc'); }}
                          title={t('search.sortSmallest', 'Smallest first')}
                        >
                          {t('search.sortSmallestFirst', 'Smallest First ↑')}
                        </button>
                      </div>
                    </div>

                    {/* 创建时间 */}
                    <div className={`sort-menu-row ${sortField === 'created' ? 'sort-menu-row--active' : ''}`}>
                      <div className="sort-row-left" onClick={() => handleSetSortDirect('created', sortDirection)}>
                        <Calendar size={13} />
                        <span>{t('search.colCreated', 'Created Time')}</span>
                      </div>
                      <div className="sort-dir-subpills">
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'created' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('created', 'desc'); }}
                          title={t('search.sortNewest', 'Newest to oldest')}
                        >
                          {t('search.sortNewestFirst', 'Newest First ↓')}
                        </button>
                        <button
                          type="button"
                          className={`sort-dir-subpill ${sortField === 'created' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                          onClick={(e) => { e.stopPropagation(); handleSetSortDirect('created', 'asc'); }}
                          title={t('search.sortOldest', 'Oldest to newest')}
                        >
                          {t('search.sortOldestFirst', 'Oldest First ↑')}
                        </button>
                      </div>
                    </div>
                  </div>

                  <div className="sort-menu-divider" />

                  {/* 多维组合排序微开关 */}
                  <div className="sort-composite-section">
                    <div className="sort-composite-header">
                      <span>{t('search.sortComboRules', 'Combined Sorting Rules')}</span>
                    </div>
                    <label className="sort-composite-option">
                      <input
                        type="checkbox"
                        checked={foldersFirst}
                        onChange={toggleFoldersFirst}
                      />
                      <Folder size={13} className="sort-option-icon" />
                      <span>{t('search.foldersFirst', 'Folders Always On Top')}</span>
                    </label>
                    <label className="sort-composite-option">
                      <input
                        type="checkbox"
                        checked={groupByType}
                        onChange={toggleGroupByType}
                      />
                      <Tag size={13} className="sort-option-icon" />
                      <span>{t('search.groupByType', 'Group by File Extension/Type')}</span>
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
                <span>{t('search.recentSearches', 'Recent Search History')}</span>
              </div>
              <button
                type="button"
                className="search-history-clear-btn"
                onClick={clearAllHistory}
                title={t('search.clearHistory', 'Clear History')}
              >
                <Trash2 size={11} />
                <span>{t('search.clearHistory', 'Clear History')}</span>
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
                  title={t('search.searchCountTip', 'Search frequency: {{count}} times · Click to reuse', { count: item.searchCount })}
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
                    title={t('search.deleteHistoryItem', 'Delete this history')}
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
                <span>{t('search.drawerTitle', 'View & Search Preferences')}</span>
              </div>
              <button
                className="popover-close"
                onClick={() => setShowViewSettings(false)}
                type="button"
                title={t('search.close', 'Close')}
              >
                <X size={14} />
              </button>
            </div>

            <div className="popover-body">
              {/* 顶部一键全盘重新扫描与快照固化 */}
              <div className="popover-top-rebuild-card">
                <div className="popover-top-rebuild-info">
                  <div className="popover-top-rebuild-title">{t('search.rebuildSectionTitle', 'Full-Disk Index & Snapshot Maintenance')}</div>
                  <div className="popover-top-rebuild-desc">
                    {t('search.indexMaintenanceDesc', 'Rescan all NTFS partitions for changes and synchronize snapshot to EasyTools.db (F5 / Ctrl+R)')}
                  </div>
                </div>
                <button
                  type="button"
                  className={`popover-rebuild-btn popover-rebuild-btn--top ${isRebuilding ? 'popover-rebuild-btn--loading' : ''}`}
                  onClick={rebuildIndex}
                  disabled={isRebuilding}
                  title={t('search.rebuildButtonTip', 'Rescan all selected disks NTFS MFT partitions and save snapshot (F5 / Ctrl+R)')}
                >
                  <RefreshCw size={13} className={isRebuilding ? 'spin-animation' : ''} />
                  <span>{isRebuilding ? t('search.rebuildingInProgress', 'Rescanning and updating snapshot...') : t('search.rebuildButton', 'Rescan and Update Index & Snapshot (F5)')}</span>
                </button>
              </div>

              {/* 1. 默认搜索范围与模式 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>{t('search.defaultSearchMode', 'Default Search Mode')}</span>
                  <span className="popover-badge-curr">
                    {searchMode === 'name' ? t('search.modeFileOnlyBadge', 'Name Only') : searchMode === 'both' ? t('search.modeBothBadge', 'Name + Content') : t('search.modeContentOnlyBadge', 'Content Only')}
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
                        <span className="search-mode-title">{t('search.modeFileOnlyTitle', 'Search File Name Only (Fast · Default)')}</span>
                      </div>
                      {searchMode === 'name' && <Check size={13} className="search-mode-check" />}
                    </div>
                    <div className="search-mode-desc">
                      {t('search.modeNameDesc', 'Sub-millisecond response (<5ms); to search content, click the top "Content" tab or prefix with content:')}
                    </div>
                  </div>

                  <div 
                    className={`popover-search-mode-card ${searchMode === 'both' ? 'popover-search-mode-card--active' : ''}`}
                    onClick={() => changeSearchMode('both')}
                  >
                    <div className="search-mode-header">
                      <div className="search-mode-title-wrap">
                        <Sparkles size={14} className="search-mode-icon-both" />
                        <span className="search-mode-title">{t('search.modeHybrid', 'Filename & Content Hybrid')}</span>
                      </div>
                      {searchMode === 'both' && <Check size={13} className="search-mode-check" />}
                    </div>
                    <div className="search-mode-desc">
                      {t('search.modeHybridDesc', 'Search both filename and document contents simultaneously')}
                    </div>
                  </div>

                  <div 
                    className={`popover-search-mode-card ${searchMode === 'content' ? 'popover-search-mode-card--active' : ''}`}
                    onClick={() => changeSearchMode('content')}
                  >
                    <div className="search-mode-header">
                      <div className="search-mode-title-wrap">
                        <FileText size={14} className="search-mode-icon-content" />
                        <span className="search-mode-title">{t('search.modeContentOnly', 'Content Only (Full-Text)')}</span>
                      </div>
                      {searchMode === 'content' && <Check size={13} className="search-mode-check" />}
                    </div>
                    <div className="search-mode-desc">
                      {t('search.modeContentOnlyDesc', 'Full-text search inside documents, spreadsheets, PDFs, CAD and code files')}
                    </div>
                  </div>
                </div>
              </div>

              {/* 1. 布局密度 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>{t('search.density', 'List Display Density')}</span>
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
                    {t('search.densityCompact', 'Compact')}
                  </button>
                  <button
                    type="button"
                    className={`popover-segment ${density === 'standard' ? 'popover-segment--active' : ''}`}
                    onClick={() => changeDensity('standard')}
                  >
                    {t('search.densityStandard', 'Standard')}
                  </button>
                  <button
                    type="button"
                    className={`popover-segment ${density === 'comfortable' ? 'popover-segment--active' : ''}`}
                    onClick={() => changeDensity('comfortable')}
                  >
                    {t('search.densityComfortable', 'Comfortable')}
                  </button>
                </div>
              </div>

              {/* 2. 检索结果数量上限 (支持全部返回) */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>{t('search.maxResults')}</span>
                  <span className="popover-badge-curr">
                    {maxResultLimit === 0 ? t('search.maxResultsAll') : t('search.maxResultsCount', '{{count}} items', { count: maxResultLimit })}
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
                  <span>{t('search.windowSizePlacement', 'Window Size & Placement')} ({windowSize.width} × {windowSize.height})</span>
                  <button
                    type="button"
                    className="popover-mini-link"
                    onClick={() => {
                      void bridgeRequest('search.resetPlacement');
                      setWindowSize({ width: 760, height: 520 });
                      toast.success(t('search.placementReset', 'Window placement and size reset to default'));
                    }}
                    title={t('search.resetPlacementBtn', 'Reset to Default Center (760×520)')}
                  >
                    <RotateCcw size={11} style={{ marginRight: 3, verticalAlign: -1 }} />
                    {t('search.resetPlacementBtn', 'Reset to Default Center (760×520)')}
                  </button>
                </div>
                <div className="popover-presets-grid">
                  <button
                    type="button"
                    className={`popover-preset-btn ${windowSize.width === 760 && windowSize.height === 520 ? 'popover-preset-btn--active' : ''}`}
                    onClick={() => changeWindowSize(760, 520)}
                  >
                    {t('search.sizeDefault', 'Default (760×520)')}
                  </button>
                  {WINDOW_PRESETS.map((preset) => (
                    <button
                      key={preset.id}
                      type="button"
                      className={`popover-preset-btn ${windowSize.width === preset.width && windowSize.height === preset.height ? 'popover-preset-btn--active' : ''}`}
                      onClick={() => changeWindowSize(preset.width, preset.height)}
                    >
                      {preset.id === 'standard' ? t('search.presetStandard', 'Standard (800×600)') :
                       preset.id === 'wide' ? t('search.presetWide', 'Widescreen (1000×650)') :
                       preset.id === 'large' ? t('search.presetLarge', 'Large (1200×750)') :
                       preset.id === 'extra' ? t('search.presetExtra', 'Ultra-Wide (1400×800)') : preset.label}
                    </button>
                  ))}
                </div>
                <div className="popover-section-hint">
                  <Info size={12} style={{ marginRight: 4, verticalAlign: -1, display: 'inline-block' }} />
                  {t('search.windowPlacementHint', 'Drag top header or move icon to reposition; drag edges or corner to resize freely.')}
                </div>
              </div>

              {/* 3. 列显示开关 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>{t('search.columnControls', 'Result Column Display Controls')}</span>
                </div>
                <div className="popover-cols-grid">
                  {columns.map(col => (
                    <label key={col.id} className="popover-col-checkbox-label">
                      <input
                        type="checkbox"
                        checked={col.visible}
                        onChange={() => toggleColumnVisibility(col.id)}
                      />
                      <span>{col.id === 'name' ? t('search.colName', 'File Name') :
                           col.id === 'ext' ? t('search.colExt', 'Type Tag') :
                           col.id === 'parent' ? t('search.colParent', 'Folder') :
                           col.id === 'path' ? t('search.colPath', 'Full Path') :
                           col.id === 'size' ? t('search.colSize', 'Size') :
                           col.id === 'modified' ? t('search.colModified', 'Modified Time') :
                           col.id === 'created' ? t('search.colCreated', 'Created Time') :
                           col.id === 'snippets' ? t('search.colSnippets', 'Content Snippets') : col.label}</span>
                    </label>
                  ))}
                </div>
              </div>

              {/* 4. 名称与路径列宽占比 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <span>{t('search.namePathRatio', 'Name & Path Width Ratio')} ({nameFlex}% : {pathFlex}%)</span>
                </div>
                <div className="popover-slider-row">
                  <span className="slider-label">{t('search.narrow', 'Narrow')}</span>
                  <input
                    type="range"
                    min="15"
                    max="65"
                    step="1"
                    value={nameFlex}
                    onChange={(e) => updateNameAndPathFlex(parseInt(e.target.value, 10))}
                    className="popover-ratio-slider"
                  />
                  <span className="slider-label">{t('search.wide', 'Wide')}</span>
                </div>
              </div>

              {/* 5. 搜索驱动器与磁盘位置 */}
              {systemDrives.length > 0 && (
                <div className="popover-section">
                  <div className="popover-section-title">
                    <span>{t('search.searchDrives', 'Search Drives & Locations')}</span>
                    <div className="popover-drives-actions">
                      <button
                        type="button"
                        className="popover-mini-link"
                        onClick={selectAllDrives}
                      >
                        {t('search.selectAll', 'Select All')}
                      </button>
                      <span className="popover-action-divider">/</span>
                      <button
                        type="button"
                        className="popover-mini-link"
                        onClick={deselectAllDrives}
                      >
                        {t('search.deselectAll', 'Deselect All')}
                      </button>
                    </div>
                  </div>
                  <div className="popover-drives-grid">
                    {systemDrives.map((drv) => {
                      const isChecked = enabledDrives.includes(drv.letter);
                      const isRemote = drv.type === 'remote';
                      const isRemovable = drv.type === 'removable';
                      const totalStr = drv.totalBytes > 0 ? formatBytes(drv.totalBytes) : '';
                      const freeStr = drv.freeBytes > 0 ? t('search.freeSpace', '(Free {{size}})', { size: formatBytes(drv.freeBytes) }) : '';
                      const label = drv.volumeLabel ? `${drv.volumeLabel} (${drv.letter}:)` : (isRemote ? t('search.remoteDrive', 'Network Drive ({{letter}}:)', { letter: drv.letter }) : t('search.localDrive', 'Local Disk ({{letter}}:)', { letter: drv.letter }));

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
                            aria-label={t('search.enableDriveAria', 'Enable Drive {{letter}}', { letter: drv.letter })}
                          />
                          <div className="drive-card-icon">
                            {isRemote ? <Network size={15} /> : isRemovable ? <Disc size={15} /> : <HardDrive size={15} />}
                          </div>
                          <div className="drive-card-details">
                            <div className="drive-card-title-row">
                              <span className="drive-card-title">{label}</span>
                              <span className="drive-card-tag">{drv.fileSystem || (isRemote ? t('search.remoteShare', 'Network Share') : t('search.localDisk', 'Local'))}</span>
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
                  <span>{t('search.excludeRules', 'Exclusion Rules')}</span>
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
                    <span>{t('search.excludeDevTrash', 'Exclude dev dependencies & Recycle Bin (node_modules, .git, $Recycle.Bin)')}</span>
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
                    <span>{t('search.excludeHiddenProtected', 'Exclude hidden & protected system files')}</span>
                  </label>
                </div>
              </div>

              {/* 7. 文档内容检索格式定制 (Content Search Formats & Exts) */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <div className="popover-section-title-left">
                    <SlidersHorizontal size={13} className="popover-title-icon" />
                    <span>{t('search.contentFormats', 'Supported Content Search Formats')}</span>
                    <span className="popover-title-badge">content:</span>
                  </div>
                  <button
                    type="button"
                    className="popover-quick-action"
                    onClick={resetContentFormats}
                    title={t('search.resetDefaultFormats', 'Restore Default Formats')}
                  >
                    <RotateCcw size={11} />
                    <span>{t('search.resetDefault')}</span>
                  </button>
                </div>
                
                <div className="popover-format-categories">
                  {CONTENT_FORMAT_CATEGORIES.map(cat => {
                    const enabledCount = cat.extensions.filter(ext => !disabledContentFormats.includes(ext)).length;
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
                          <Toggle
                            id={`format-cat-toggle-${cat.id}`}
                            checked={enabledCount > 0}
                            size="sm"
                            onChange={(checked) => toggleCategoryFormats(cat, checked)}
                          />
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
                                title={isEnabled ? t('search.disableExtTip', 'Click to disable .{{ext}}', { ext }) : t('search.enableExtTip', 'Click to enable .{{ext}}', { ext })}
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
                        <span className="popover-format-badge-count">{t('search.customFormatsCount', '{{count}} items', { count: customContentFormats.length })}</span>
                      </div>
                      {customContentFormats.length > 0 && (
                        <Toggle
                          id="format-cat-toggle-custom"
                          checked={customContentFormats.some(ext => !disabledContentFormats.includes(ext))}
                          size="sm"
                          onChange={(checked) => {
                            setDisabledContentFormats(prev => {
                              let next: string[];
                              if (checked) {
                                next = prev.filter(ext => !customContentFormats.includes(ext));
                              } else {
                                const addList = customContentFormats.filter(ext => !prev.includes(ext));
                                next = [...prev, ...addList];
                              }
                              localStorage.setItem('easytools_search_disabled_content_formats', JSON.stringify(next));
                              return next;
                            });
                          }}
                        />
                      )}
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
                                title={t('search.deleteCustomExt', 'Delete this custom format')}
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
                        placeholder={t('search.formatInputPlaceholder', 'Add extensions separated by commas/spaces (e.g. ps1, ch, log2)...')}
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
                        title={t('search.addCustomExtTip', 'Add custom format (supports comma/space bulk adding)')}
                      >
                        <Plus size={13} />
                        <span>{t('search.addBtn', 'Add')}</span>
                      </button>
                    </div>
                    <div className="popover-format-add-tip">
                      <Lightbulb size={12} className="popover-format-add-tip-icon" />
                      <span>{t('search.formatInputHint', 'Supports batch adding multiple extensions separated by commas or spaces, press Enter to add')}</span>
                    </div>
                  </div>
                </div>
              </div>

              {/* 8. 索引数据库快照与缓存状态 */}
              <div className="popover-section">
                <div className="popover-section-title">
                  <div className="popover-section-title-left">
                    <HardDrive size={13} className="popover-title-icon" />
                    <span>{t('search.dbSnapshot', 'Disk Snapshot & Database')}</span>
                    <span className="popover-title-badge">EasyTools.db</span>
                  </div>
                </div>
                
                <div className="popover-db-card">
                  <div className="popover-db-row">
                    <span className="popover-db-label">{t('search.snapshotStatus', 'Snapshot Status:')}</span>
                    <span className="popover-db-val">
                      {dbStats?.exists ? (
                        <span className="popover-db-status--ok">{t('search.snapshotPersisted', 'Persisted')} ({formatBytes(dbStats.dbSize)})</span>
                      ) : (
                        <span className="popover-db-status--empty">{t('search.snapshotReady', 'Ready (Auto-persisted on exit or idle)')}</span>
                      )}
                    </span>
                  </div>
                  <div className="popover-db-row">
                    <span className="popover-db-label">{t('search.totalRecords', 'Indexed Records:')}</span>
                    <span className="popover-db-val">{t('search.maxResultsCount', { defaultValue: '{{count}} items', count: Number(dbStats?.totalRecords ?? totalIndexedFiles ?? 0) })}</span>
                  </div>
                  <div className="popover-db-row">
                    <span className="popover-db-label">{t('search.runHistory', 'Run History:')}</span>
                    <span className="popover-db-val">{t('search.runHistoryStats', 'Run History ({{count}} frequent)', { count: dbStats?.runHistoryCount ?? 0 })}</span>
                  </div>
                  <div className="popover-db-row">
                    <span className="popover-db-label">{t('search.searchHistory', 'Search History:')}</span>
                    <span className="popover-db-val">{t('search.searchHistoryStats', 'Search History ({{count}} queries)', { count: dbStats?.searchHistoryCount ?? 0 })}</span>
                  </div>
                  <div className="popover-db-actions">
                    <button
                      type="button"
                      className="popover-db-btn popover-db-btn--danger"
                      onClick={clearAllHistory}
                      style={{ width: '100%' }}
                    >
                      <Trash2 size={12} />
                      <span>{t('search.clearHistoryBtn', 'Clear Run & Search History')}</span>
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
                  <span>{t('search.resetViewPrefBtn', 'Reset All View Preferences')}</span>
                </button>
              </div>
            </div>
          </div>
        )}

        {showSyntaxHelp && (
          <div className="search-syntax-drawer">
            <div className="syntax-drawer-header">
              <div className="syntax-drawer-header-left">
                <div className="syntax-drawer-title">
                  <Sparkles size={16} className="syntax-drawer-title-icon" />
                  <span>{t('search.syntaxHelpTitle', 'Advanced Search Syntax Quick Guide')}</span>
                  <span className="syntax-drawer-kbd"><kbd>F1</kbd></span>
                </div>
                <div className="syntax-drawer-tabs">
                  {SYNTAX_CATEGORIES.map(cat => (
                    <button
                      key={cat.id}
                      type="button"
                      className={`syntax-drawer-tab ${syntaxCat === cat.id ? 'active' : ''}`}
                      onClick={() => setSyntaxCat(cat.id)}
                    >
                      {t(cat.labelKey, cat.defaultLabel)}
                    </button>
                  ))}
                </div>
              </div>
              <button
                className="syntax-drawer-close"
                onClick={() => setShowSyntaxHelp(false)}
                type="button"
                title={t('search.closeSyntaxHelp', 'Close Syntax Guide (Esc / F1)')}
              >
                <X size={16} />
              </button>
            </div>
            <div className="syntax-drawer-list">
              {filteredSyntaxExamples.map((item, idx) => (
                <div
                  key={idx}
                  className={`syntax-example-item ${item.highlight ? 'syntax-example-item--highlight' : ''}`}
                  onClick={() => applySyntaxExample(item.syntax)}
                  title={t('search.clickToFill', 'Click to insert into search box')}
                >
                  <div className="syntax-example-item-top">
                    <code className="syntax-code">{item.syntax}</code>
                    <span className="syntax-example-action-hint">{t('search.fillInHint', 'Insert ↵')}</span>
                  </div>
                  <span className="syntax-desc">{t(item.descKey, item.defaultDesc)}</span>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* ── 全息深度内容检索加载舱 (Cold Content Scan Holographic State) ── */}
        {!isInitialIndexing && !isServiceStarting && loading && ((activeCategory === 'content' || query.trim().toLowerCase().startsWith('content:') || query.trim().startsWith('内容:')) || sortedResults.length === 0) && (
          <div className="search-empty-container search-empty-container--deep-scan" role="status">
            <div className="search-deep-scan-radar">
              <div className="search-deep-scan-radar-wave wave-1" />
              <div className="search-deep-scan-radar-wave wave-2" />
              <div className="search-empty-icon-wrap search-empty-icon-wrap--deep-scan">
                <FileText size={32} className="search-empty-icon" />
              </div>
            </div>
            
            <div className="search-empty-title search-deep-scan-title">
              {t('search.contentScanningTitle', 'Deep Full-Disk Content Search in progress...')}
            </div>

            <div className="search-empty-desc search-deep-scan-desc">
              {t('search.contentScanningDesc', 'First-time query reads file contents directly from disk sectors (cold cache). Subsequent searches are accelerated by Windows memory page cache (instant).')}
            </div>

            <div className="search-deep-scan-dashboard">
              <div className="search-deep-scan-metrics">
                <div className="search-deep-scan-metric-badge search-deep-scan-metric--elapsed">
                  <Clock size={13} />
                  <span>{t('search.contentScanningElapsed', 'Scanned for {{elapsed}}s', { elapsed: scanElapsedSeconds.toFixed(1) })}</span>
                </div>
                <div className="search-deep-scan-metric-badge search-deep-scan-metric--bench" title={t('search.contentScanningLastBench', 'Last cold scan took ~{{duration}}s', { duration: lastColdScanDurationSec.toFixed(1) })}>
                  <Zap size={13} />
                  <span>{t('search.contentScanningLastBench', 'Last cold scan took ~{{duration}}s', { duration: lastColdScanDurationSec.toFixed(1) })}</span>
                </div>
              </div>

              <div className="search-deep-scan-progress-bar-wrap">
                <div
                  className="search-deep-scan-progress-bar"
                  style={{
                    width: `${Math.min(94, Math.max(8, (scanElapsedSeconds / lastColdScanDurationSec) * 100))}%`
                  }}
                />
              </div>

              <div className="search-deep-scan-tip">
                <span>{t('search.contentScanningSpeedupTip', 'Accelerated 5x+ once Windows memory cache warms up')}</span>
              </div>
            </div>
          </div>
        )}

        {(isInitialIndexing || isServiceStarting) && sortedResults.length === 0 && (
          <div className="search-empty-container search-empty-container--indexing" role="status">
            <div className="search-empty-icon-wrap search-empty-icon-wrap--indexing">
              <RefreshCw size={32} className="search-empty-icon spin-animation" />
            </div>
            <div className="search-empty-title">
              {isInitialIndexing
                 ? t('search.initialIndexingTitle', 'Building and persisting full disk index...')
                : t('search.serviceStartingTitle', 'Connecting to File Indexing Engine...')}
            </div>
            <div className="search-empty-desc">
              {isInitialIndexing
                ? t('search.initialIndexingDesc', 'First run is scanning all local MFT tables and persisting index. Instant search will be ready in seconds.')
                : t('search.serviceStartingDesc', 'Waking up the indexing engine on demand and loading disk cache. Results will appear automatically, please wait...')}
            </div>
            <div className="search-indexing-progress-bar-wrap">
              <div className="search-indexing-progress-bar-indeterminate" />
            </div>
          </div>
        )}

        {!isInitialIndexing && !isServiceStarting && !serviceAvailable && (
          <div className="search-status" role="status">
            <ServerOff size={18} aria-hidden="true" />
            <span>{t('search.serviceUnavailable', 'The file index service is unavailable. Repair or reinstall EasyTools to restore instant search.')}</span>
          </div>
        )}

        {!isInitialIndexing && !isServiceStarting && actionError && <div className="search-status search-status--error" role="alert">{actionError}</div>}

        {!isInitialIndexing && !isServiceStarting && serviceAvailable && query.trim() && !loading && sortedResults.length === 0 && (
          <div className="search-empty-container" role="status">
            <div className="search-empty-icon-wrap">
              <SearchX size={34} className="search-empty-icon" />
            </div>
            <div className="search-empty-title">
              {query.trim().toLowerCase().startsWith('content:') || query.trim().startsWith('内容:')
                ? t('search.noContentResults', 'No matching content found')
                : (activeCategory !== 'all' && activeCategory !== 'content')
                ? t('search.noMatchInCat', { cat: categories.find(c => c.id === activeCategory)?.label || '', defaultValue: `No matching ${categories.find(c => c.id === activeCategory)?.label || ''} files found` })
                : (query.trim().startsWith('ext:') || query.trim().startsWith('path:') || query.trim().startsWith('folder:') || query.trim().startsWith('dir:') || query.trim().startsWith('file:'))
                ? t('search.noMatchForFilter', { query: query.trim(), defaultValue: `No files matching filter "${query.trim()}"` })
                : t('search.noMatchForName', { query: query.trim(), defaultValue: `No files containing "${query.trim()}"` })}
            </div>
            {!query.trim().toLowerCase().startsWith('content:') &&
             !(query.trim().toLowerCase().startsWith('c:') && !query.trim().toLowerCase().startsWith('c:\\') && !query.trim().toLowerCase().startsWith('c:/')) &&
             !query.trim().startsWith('内容:') && (
              <button
                className="search-switch-content-btn"
                onClick={() => {
                  const contentCat = categories.find(c => c.id === 'content') || categories[1];
                  if (contentCat) selectCategory(contentCat);
                }}
                type="button"
                title={t('search.searchContentDirectly', { query: query.trim(), defaultValue: `Search full document and code contents for: "${query.trim()}"` })}
              >
                <FileText size={15} />
                <span>{t('search.searchContentDirectly', { query: query.trim(), defaultValue: `Search full document and code contents for: "${query.trim()}"` })}</span>
              </button>
            )}
            {(query.trim().toLowerCase().startsWith('content:') || query.trim().startsWith('内容:') || activeCategory === 'content') && (
              <div className="search-empty-content-diagnostic">
                <span>{t('search.contentZeroResultTip1', 'Click top-right Customize to add custom file extensions (e.g. .vue, .ts, .log)')}</span>
                <span>{t('search.contentZeroResultTip2', 'Ensure target files are not inside excluded directories (e.g. node_modules, .git)')}</span>
              </div>
            )}
            <div className="search-empty-hint">
              <Lightbulb size={12} className="search-empty-hint-icon" />
              <span>{t('search.searchHint', 'Tip: Press Enter to perform full-text search, or press F1 for advanced syntax.')}</span>
            </div>
          </div>
        )}

        {!isInitialIndexing && !isServiceStarting && serviceAvailable && !query.trim() && sortedResults.length === 0 && searchHistory.length === 0 && (
          <div className="search-empty-container search-empty-container--initial" role="status">
            <div className="search-empty-icon-wrap search-empty-icon-wrap--initial">
              <Search size={32} className="search-empty-icon" />
            </div>
            <div className="search-empty-title">{t('search.readyTitle', 'Instant Full-Disk Index Ready')}</div>
            <div className="search-empty-desc">{t('search.readyDesc', 'Type keywords, wildcards (*.pdf), or extensions (ext:docx) to search instantly')}</div>
            <div className="search-empty-quick-tags">
              {['*.docx', 'ext:png', 'size:>100mb', 'content:meeting'].map((tag) => (
                <button
                  key={tag}
                  type="button"
                  className="search-empty-quick-tag"
                  onClick={() => {
                    updateQuery(tag);
                    inputRef.current?.focus();
                  }}
                  title={t('search.clickToFill', 'Click to insert into search box')}
                >
                  <code>{tag}</code>
                </button>
              ))}
            </div>
          </div>
        )}

        {!isInitialIndexing && !isServiceStarting && !((activeCategory === 'content' || query.trim().toLowerCase().startsWith('content:') || query.trim().startsWith('内容:')) && loading) && sortedResults.length > 0 && (
          <VirtualSearchResults
            key={`${density}:${query}:${sortField}:${sortDirection}:${sortedResults.length}:${sortedResults[0]?.path ?? ''}`}
            results={sortedResults}
            selectedIndex={selectedIndex}
            density={density}
            columns={columnLayout}
            queryKeywords={queryKeywords}
            onHover={handleRowHover}
            onSelect={handleRowSelect}
            onOpen={handleRowOpen}
            onContextMenu={handleRowContextMenu}
          />
        )}

        <footer className="search-footer">
          <div className="search-footer-left">
            {((activeCategory === 'content' || query.trim().toLowerCase().startsWith('content:') || query.trim().startsWith('内容:')) && loading) ? (
              <div className="search-footer-stat-item search-footer-stat-item--scanning" style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <RefreshCw size={11} className="spin-animation" style={{ color: 'var(--primary)' }} />
                <span>
                  <strong>{t('search.contentSearchingFooter', 'Deep full-disk content search in progress... ({{elapsed}}s)', { elapsed: scanElapsedSeconds.toFixed(1) })}</strong>
                </span>
              </div>
            ) : (
              <>
                {/* 1. 总对象数显示 (Everything 级核心状态) 与一键刷新微按钮 */}
                <div className="search-footer-stat-item search-footer-stat-item--interactive" title={t('search.rescanIndexTitle', 'Rescan full disk index and save snapshot (F5 / Ctrl+R)')}>
                  <span>
                    {isServiceStarting ? (
                      <><strong>{t('search.serviceConnectingStatus', 'Connecting to Index Service...')}</strong></>
                    ) : isInitialIndexing ? (
                      <><strong>{t('search.initialIndexingTitle', 'Building and persisting full disk index...')}</strong></>
                    ) : sortedResults.length > 0 ? (
                      <><strong>{sortedResults.length.toLocaleString()}</strong> {t('search.objectsCount', 'objects')}</>
                    ) : (
                      <><strong>{totalIndexedFiles ? totalIndexedFiles.toLocaleString() : '--'}</strong> {t('search.objectsCount', 'objects')}</>
                    )}
                  </span>
                  <button
                    type="button"
                    className="search-footer-refresh-btn"
                    onClick={(e) => {
                      e.stopPropagation();
                      void rebuildIndex();
                    }}
                    disabled={isRebuilding || isServiceStarting}
                    title={t('search.rescanIndexTitle', 'Rescan full disk index and save snapshot (F5 / Ctrl+R)')}
                  >
                    <RefreshCw size={11} className={(isRebuilding || isServiceStarting) ? 'spin-animation' : ''} />
                  </button>
                </div>

                {/* 2. 当前匹配结果总大小 */}
                {sortedResults.length > 0 && totalResultSize > 0 && (
                  <>
                    <span className="search-footer-stat-divider" />
                    <div className="search-footer-stat-item" title={formatBytes(totalResultSize)}>
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
                        {t('search.selectedLabel', 'Selected')} <strong>{selectedIndex + 1}</strong> / {sortedResults.length}
                        {!sortedResults[selectedIndex].isDirectory && sortedResults[selectedIndex].size !== undefined ? (
                          <> ({formatBytes(sortedResults[selectedIndex].size || 0)})</>
                        ) : (
                          <> ({t('search.folderLabel', 'Folder')})</>
                        )}
                      </span>
                    </div>
                  </>
                )}

                {/* 4. 引擎匹配耗时 */}
                {searchElapsedMs !== undefined && searchElapsedMs >= 0 && sortedResults.length > 0 && (
                  <>
                    <span className="search-footer-stat-divider" />
                    <div className="search-footer-stat-item search-footer-stat-sub" title={t('search.searchTimeTitle', 'MFT memory index match elapsed time')}>
                      <span>{searchElapsedMs} ms</span>
                    </div>
                  </>
                )}
              </>
            )}
          </div>

          <div className="search-footer-right">
            {sortedResults.length > 0 && (
              <>
                <button
                  type="button"
                  className="search-hint-btn"
                  onClick={() => openResult(sortedResults[selectedIndex])}
                  title={t('search.openFileTitle', 'Open selected file (Enter)')}
                >
                  <kbd>Enter</kbd>
                  <span>{t('search.openShortcut', 'Open')}</span>
                </button>
                <button
                  type="button"
                  className="search-hint-btn"
                  onClick={() => openFolderResult(sortedResults[selectedIndex])}
                  title={t('search.openFolderTitle', 'Locate and select file in Explorer (Ctrl+Enter)')}
                >
                  <kbd>Ctrl+Enter</kbd>
                  <span>{t('search.openFolderShortcut', 'Open Folder')}</span>
                </button>
                <button
                  type="button"
                  className="search-hint-btn"
                  onClick={() => copyPathResult(sortedResults[selectedIndex])}
                  title={t('search.copyPathTitle', 'Copy full file path (Ctrl+C)')}
                >
                  <kbd>Ctrl+C</kbd>
                  <span>{t('search.copyPathShortcut', 'Copy Path')}</span>
                </button>
                <button
                  type="button"
                  className="search-hint-btn"
                  onClick={exportResultsToCsv}
                  title={t('search.exportReportTitle', 'Export search results to CSV report (Ctrl+E)')}
                >
                  <kbd>Ctrl+E</kbd>
                  <span>{t('search.exportShortcut', 'Export')}</span>
                </button>
              </>
            )}
            <button
              type="button"
              className="search-hint-btn"
              onClick={rebuildIndex}
              disabled={isRebuilding}
              title={t('search.rescanIndexTitle', 'Rescan full disk index and save snapshot (F5 / Ctrl+R)')}
            >
              <kbd>F5</kbd>
              <span>{isRebuilding ? t('search.refreshingShortcut', 'Refreshing...') : t('search.refreshShortcut', 'Refresh')}</span>
            </button>
            <button
              type="button"
              className="search-hint-btn"
              onClick={() => {
                setShowSyntaxHelp(prev => !prev);
                setShowViewSettings(false);
              }}
              title={t('search.syntaxHelpBtnTitle', 'View advanced search syntax and expressions (F1)')}
            >
              <kbd>F1</kbd>
              <span>{t('search.syntaxShortcut', 'Syntax')}</span>
            </button>
            <button
              type="button"
              className="search-hint-btn"
              onClick={hide}
              title={t('search.closeWindowBtnTitle', 'Close search window (Esc)')}
            >
              <kbd>Esc</kbd>
              <span>{t('search.closeShortcut', 'Close')}</span>
            </button>
          </div>
        </footer>

        {/* ── 现代世界级右键上下文菜单 ── */}
        {contextMenu.visible && contextMenu.result && (
          <div
            className="search-context-menu"
            style={{ left: contextMenu.x, top: contextMenu.y }}
            onClick={(e) => e.stopPropagation()}
            onContextMenu={(e) => e.preventDefault()}
          >
            <div className="search-context-menu-header">
              <span className="search-context-menu-filename" title={contextMenu.result.path}>
                {contextMenu.result.name}
              </span>
            </div>

            <div className="search-context-menu-group">
              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  void openResult(res);
                }}
              >
                <FolderOpen size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuOpen', 'Open')} {contextMenu.result.isDirectory ? t('search.folderLabel', 'Folder') : t('search.fileLabel', 'File')}</span>
                <kbd className="menu-shortcut">Enter</kbd>
              </button>

              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  void openFolderResult(res);
                }}
              >
                <HardDrive size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuOpenFolder', 'Open Containing Folder')}</span>
                <kbd className="menu-shortcut">Ctrl+Enter</kbd>
              </button>

              {/* 在记事本中编辑 */}
              {!contextMenu.result.isDirectory && (
                <button
                  type="button"
                  className="search-context-menu-item"
                  onClick={() => {
                    const res = contextMenu.result;
                    setContextMenu({ visible: false, x: 0, y: 0 });
                    void openWithNotepad(res);
                  }}
                  title={t('search.notepadEditTooltip', 'View or edit file with Windows Notepad')}
                >
                  <FileCode size={14} className="menu-icon" />
                  <span className="menu-label">{t('search.menuEditNotepad', 'Edit in Notepad')}</span>
                </button>
              )}

              {/* 重命名 */}
              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  startRename(res);
                }}
                title={t('search.renameTooltip', 'Rename file or folder (F2)')}
              >
                <Pencil size={14} className="menu-icon" />
                <span className="menu-label">{t('search.rename', 'Rename')}</span>
                <kbd className="menu-shortcut">F2</kbd>
              </button>

              {/* 图片独立贴图 */}
              {!contextMenu.result.isDirectory && IMAGE_EXTENSIONS.test(contextMenu.result.name) && (
                <button
                  type="button"
                  className="search-context-menu-item"
                  onClick={() => {
                    const res = contextMenu.result;
                    setContextMenu({ visible: false, x: 0, y: 0 });
                    void pinResult(res);
                  }}
                >
                  <Sparkles size={14} className="menu-icon" />
                  <span className="menu-label">{t('search.menuPin', 'Pin to Desktop')}</span>
                  <kbd className="menu-shortcut">Ctrl+P</kbd>
                </button>
              )}

              {/* 以管理员身份运行 */}
              {!contextMenu.result.isDirectory && /\.(exe|bat|cmd|ps1|msi|vbs)$/i.test(contextMenu.result.name) && (
                <button
                  type="button"
                  className="search-context-menu-item"
                  onClick={() => {
                    const res = contextMenu.result;
                    setContextMenu({ visible: false, x: 0, y: 0 });
                    void openResultAsAdmin(res);
                  }}
                >
                  <ShieldAlert size={14} className="menu-icon" />
                  <span className="menu-label">{t('search.menuRunAdmin', 'Run as Administrator')}</span>
                </button>
              )}
            </div>

            <div className="search-context-menu-divider" />

            <div className="search-context-menu-group">
              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  copyPathResult(res);
                }}
              >
                <Copy size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuCopyPath', 'Copy Full Path')}</span>
                <kbd className="menu-shortcut">Ctrl+C</kbd>
              </button>

              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  if (res) copyText(res.name);
                }}
              >
                <FileText size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuCopyName', 'Copy Filename')}</span>
              </button>

              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  if (res) copyText(extractParentFolder(res.path));
                }}
              >
                <Folder size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuCopyParent', 'Copy Parent Directory')}</span>
              </button>
            </div>

            <div className="search-context-menu-divider" />

            <div className="search-context-menu-group">
              <button
                type="button"
                className="search-context-menu-item"
                onClick={(e) => {
                  const res = contextMenu.result;
                  const dpr = window.devicePixelRatio || 1;
                  const sx = Math.round(e.screenX * dpr);
                  const sy = Math.round(e.screenY * dpr);
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  if (res) {
                    setTimeout(() => {
                      void bridgeRequest('search.showShellContextMenu', {
                        filepath: res.path,
                        path: res.path,
                        x: sx,
                        y: sy,
                      });
                    }, 50);
                  }
                }}
                title={t('search.nativeExplorerTooltip', 'Open native Windows Explorer context menu (Shift+Right Click)')}
              >
                <AppWindow size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuNativeExplorer', 'More Windows Explorer Menu...')}</span>
                <kbd className="menu-shortcut">{t('search.menuShortcutShiftRightClick', 'Shift+Right Click')}</kbd>
              </button>

              <button
                type="button"
                className="search-context-menu-item"
                onClick={() => {
                  const res = contextMenu.result;
                  setContextMenu({ visible: false, x: 0, y: 0 });
                  if (res) void showFileProperties(res);
                }}
              >
                <Info size={14} className="menu-icon" />
                <span className="menu-label">{t('search.menuProperties', 'Properties')}</span>
                <kbd className="menu-shortcut">Alt+Enter</kbd>
              </button>
            </div>
          </div>
        )}

        {/* ── 智能文件重命名对话框 ── */}
        {renameTarget.visible && renameTarget.result && (
          <div className="search-modal-overlay" onClick={() => setRenameTarget({ visible: false, newName: '' })}>
            <div
              className="search-rename-card"
              onClick={(e) => e.stopPropagation()}
            >
              <div className="search-rename-header">
                <div className="search-rename-title">
                  <Pencil size={15} className="search-rename-icon" />
                  <span>{t('search.renameTitle', 'Rename')} {renameTarget.result.isDirectory ? t('search.folderLabel', 'Folder') : t('search.fileLabel', 'File')}</span>
                </div>
                <button
                  type="button"
                  className="search-rename-close"
                  onClick={() => setRenameTarget({ visible: false, newName: '' })}
                >
                  <X size={14} />
                </button>
              </div>

              <div className="search-rename-body">
                <div className="search-rename-path-hint" title={renameTarget.result.path}>
                  <Folder size={12} />
                  <span>{extractParentFolder(renameTarget.result.path) || renameTarget.result.path}</span>
                </div>

                <div className="search-rename-input-wrap">
                  <input
                    ref={renameInputRef}
                    type="text"
                    className="search-rename-input"
                    value={renameTarget.newName}
                    onChange={(e) => setRenameTarget(prev => ({ ...prev, newName: e.target.value }))}
                    onKeyDown={(e) => {
                      if (e.key === 'Enter') {
                        e.preventDefault();
                        void confirmRename();
                      } else if (e.key === 'Escape') {
                        e.preventDefault();
                        setRenameTarget({ visible: false, newName: '' });
                      }
                    }}
                    placeholder={t('search.renamePlaceholder', 'Enter new name...')}
                    spellCheck={false}
                  />
                </div>
              </div>

              <div className="search-rename-footer">
                <button
                  type="button"
                  className="search-rename-btn search-rename-btn--cancel"
                  onClick={() => setRenameTarget({ visible: false, newName: '' })}
                >
                  {t('common.cancel', 'Cancel')}
                </button>
                <button
                  type="button"
                  className="search-rename-btn search-rename-btn--confirm"
                  onClick={() => void confirmRename()}
                  disabled={!renameTarget.newName.trim() || renameTarget.newName.trim() === renameTarget.result.name}
                >
                {t('search.confirmRename', 'Confirm Rename')}
                </button>
              </div>
            </div>
          </div>
        )}

        <div
          className="search-resize-handle"
          onMouseDown={handleResizeMouseDown}
          title={t('search.dragResizeTooltip', 'Drag to resize window')}
          aria-label={t('search.resizeAria', 'Drag to resize window')}
        >
          <svg width="10" height="10" viewBox="0 0 10 10" fill="none" xmlns="http://www.w3.org/2000/svg">
            <line x1="8" y1="2" x2="2" y2="8" stroke="rgba(255,255,255,0.4)" strokeWidth="1.5" strokeLinecap="round" />
            <line x1="8" y1="5.5" x2="5.5" y2="8" stroke="rgba(255,255,255,0.4)" strokeWidth="1.5" strokeLinecap="round" />
          </svg>
        </div>

        <WindowResizeHandles method="search.startResize" showMaximizedCheck={false} />
      </section>
    </main>
  );
}

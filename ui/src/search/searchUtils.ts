import type { SearchResult, SortDirection, SortField } from './searchTypes';

export function formatBytes(bytes: number): string {
  if (!bytes || bytes <= 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const index = Math.floor(Math.log(bytes) / Math.log(1024));
  return `${(bytes / Math.pow(1024, index)).toFixed(index === 0 ? 0 : 1)} ${units[index]}`;
}

export function extractParentFolder(fullPath: string): string {
  if (!fullPath) return '';
  const clean = fullPath.replace(/[/\\]+$/, '');
  const lastSlash = Math.max(clean.lastIndexOf('/'), clean.lastIndexOf('\\'));
  if (lastSlash <= 0) return '';
  const parentPath = clean.slice(0, lastSlash);
  const secondLastSlash = Math.max(parentPath.lastIndexOf('/'), parentPath.lastIndexOf('\\'));
  return secondLastSlash >= 0 ? parentPath.slice(secondLastSlash + 1) : parentPath;
}

export function getFileTypeBadge(name: string, isDirectory: boolean): { label: string; colorClass: string } {
  if (isDirectory) return { label: 'DIR', colorClass: 'badge-folder' };
  const dot = name.lastIndexOf('.');
  if (dot === -1) return { label: 'FILE', colorClass: 'badge-generic' };

  const extension = name.slice(dot + 1).toUpperCase();
  const lowerExtension = extension.toLowerCase();
  if (['C', 'CPP', 'H', 'HPP', 'RS', 'GO'].includes(extension)) return { label: `.${lowerExtension}`, colorClass: 'badge-code-c' };
  if (['JS', 'TS', 'TSX', 'JSX', 'VUE', 'HTML', 'CSS', 'SCSS'].includes(extension)) return { label: `.${lowerExtension}`, colorClass: 'badge-code-js' };
  if (['PY', 'JAVA', 'CS', 'SH', 'BAT', 'PS1', 'LUA', 'SQL'].includes(extension)) return { label: `.${lowerExtension}`, colorClass: 'badge-code-other' };
  if (['JSON', 'YAML', 'YML', 'TOML', 'XML', 'INI', 'CONF'].includes(extension)) return { label: `.${lowerExtension}`, colorClass: 'badge-config' };
  if (['PNG', 'JPG', 'JPEG', 'WEBP', 'SVG', 'GIF', 'ICO', 'BMP'].includes(extension)) return { label: extension, colorClass: 'badge-img' };
  if (['ZIP', '7Z', 'RAR', 'TAR', 'GZ', 'BZ2'].includes(extension)) return { label: extension, colorClass: 'badge-archive' };
  if (['PDF', 'DOC', 'DOCX', 'XLS', 'XLSX', 'PPT', 'PPTX', 'TXT', 'MD'].includes(extension)) return { label: extension, colorClass: 'badge-doc' };
  if (['MP4', 'MKV', 'AVI', 'MOV', 'MP3', 'WAV', 'FLAC'].includes(extension)) return { label: extension, colorClass: 'badge-media' };
  if (['EXE', 'DLL', 'SYS', 'SO', 'BIN'].includes(extension)) return { label: extension, colorClass: 'badge-bin' };
  return { label: `.${lowerExtension.slice(0, 5)}`, colorClass: 'badge-generic' };
}

export function formatFileSize(bytes?: number, isDirectory?: boolean): string {
  if (isDirectory) return '';
  if (bytes === undefined || bytes === null || bytes === 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let size = bytes;
  let unitIndex = 0;
  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024;
    unitIndex += 1;
  }
  return `${size.toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

export function formatWindowsTime(fileTime?: number | string): string {
  if (!fileTime) return '';
  const numericFileTime = typeof fileTime === 'string' ? parseInt(fileTime, 10) : fileTime;
  if (!numericFileTime) return '';
  const milliseconds = Math.floor((numericFileTime - 116444736000000000) / 10000);
  if (milliseconds <= 0) return '';
  const date = new Date(milliseconds);
  if (Number.isNaN(date.getTime())) return '';

  const pad = (value: number) => (value < 10 ? `0${value}` : String(value));
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

export function getSortedResults(
  results: SearchResult[],
  sortField: SortField,
  sortDirection: SortDirection,
  foldersFirst: boolean,
  groupByType: boolean,
): SearchResult[] {
  if (results.length <= 1) return results;
  return [...results].sort((left, right) => {
    if (foldersFirst && left.isDirectory !== right.isDirectory) return left.isDirectory ? -1 : 1;

    if (groupByType && !left.isDirectory && !right.isDirectory) {
      const leftExtension = left.name.includes('.') ? left.name.slice(left.name.lastIndexOf('.')).toLowerCase() : '';
      const rightExtension = right.name.includes('.') ? right.name.slice(right.name.lastIndexOf('.')).toLowerCase() : '';
      const extensionComparison = leftExtension.localeCompare(rightExtension);
      if (extensionComparison !== 0) return extensionComparison;
    }

    let comparison = 0;
    if (sortField === 'relevance') comparison = (Number(left.frecencyScore) || 0) - (Number(right.frecencyScore) || 0);
    else if (sortField === 'modified') comparison = (Number(left.lastWriteTime) || 0) - (Number(right.lastWriteTime) || 0);
    else if (sortField === 'created') comparison = (Number(left.creationTime) || 0) - (Number(right.creationTime) || 0);
    else if (sortField === 'size') comparison = (left.isDirectory ? -1 : Number(left.size) || 0) - (right.isDirectory ? -1 : Number(right.size) || 0);
    else if (sortField === 'name') comparison = left.name.localeCompare(right.name, 'zh-CN', { numeric: true, sensitivity: 'base' });

    if (comparison !== 0) return sortDirection === 'asc' ? comparison : -comparison;
    return left.name.localeCompare(right.name, 'zh-CN', { numeric: true, sensitivity: 'base' });
  });
}

/**
 * 从原始查询文本中提取用于行高亮的核心关键词列表，剔除语法前缀与特殊修饰符。
 */
export function parseQueryKeywords(query: string): string[] {
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
}


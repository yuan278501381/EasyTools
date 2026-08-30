import type { LucideIcon } from 'lucide-react';

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
  /** The server received a newer query while calculating this response. */
  cancelled?: boolean;
  queryId?: number;
}

export type SearchDensity = 'compact' | 'standard' | 'comfortable';
export type SearchIconStyle = 'native' | 'vector';
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

export interface SyntaxExampleItem {
  category: 'content' | 'path' | 'ext' | 'logic';
  syntax: string;
  descKey: string;
  defaultDesc: string;
  highlight?: boolean;
}

export interface FormatCategory {
  id: string;
  nameKey: 'search.catCode' | 'search.catDocs' | 'search.catConfig' | 'search.catDesign';
  iconName: 'code' | 'doc' | 'config' | 'design';
  extensions: string[];
}

/** Column visibility and widths shared by every memoized result row. */
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

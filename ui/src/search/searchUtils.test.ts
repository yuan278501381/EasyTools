import { describe, expect, it } from 'vitest';
import { getSortedResults, parseQueryKeywords } from './searchUtils';
import type { SearchResult } from './searchTypes';

const mockResults: SearchResult[] = [
  {
    path: 'C:\\test\\subfolder',
    name: 'subfolder',
    isDirectory: true,
    size: 0,
    lastWriteTime: 1000,
    creationTime: 1000,
  },
  {
    path: 'C:\\test\\app.exe',
    name: 'app.exe',
    isDirectory: false,
    size: 2048,
    lastWriteTime: 2000,
    creationTime: 2000,
  },
  {
    path: 'C:\\test\\document.pdf',
    name: 'document.pdf',
    isDirectory: false,
    size: 1024,
    lastWriteTime: 3000,
    creationTime: 3000,
  },
  {
    path: 'C:\\test\\another_dir',
    name: 'another_dir',
    isDirectory: true,
    size: 0,
    lastWriteTime: 4000,
    creationTime: 4000,
  },
];

describe('getSortedResults with FileFolderPriority', () => {
  it('places files first before folders when priority is filesFirst', () => {
    const sorted = getSortedResults(mockResults, 'name', 'asc', 'filesFirst', false);
    expect(sorted[0].isDirectory).toBe(false);
    expect(sorted[1].isDirectory).toBe(false);
    expect(sorted[2].isDirectory).toBe(true);
    expect(sorted[3].isDirectory).toBe(true);

    expect(sorted[0].name).toBe('app.exe');
    expect(sorted[1].name).toBe('document.pdf');
    expect(sorted[2].name).toBe('another_dir');
    expect(sorted[3].name).toBe('subfolder');
  });

  it('places folders first when priority is foldersFirst', () => {
    const sorted = getSortedResults(mockResults, 'name', 'asc', 'foldersFirst', false);
    expect(sorted[0].isDirectory).toBe(true);
    expect(sorted[1].isDirectory).toBe(true);
    expect(sorted[2].isDirectory).toBe(false);
    expect(sorted[3].isDirectory).toBe(false);

    expect(sorted[0].name).toBe('another_dir');
    expect(sorted[1].name).toBe('subfolder');
  });

  it('mixes files and folders when priority is mixed', () => {
    const sorted = getSortedResults(mockResults, 'name', 'asc', 'mixed', false);
    expect(sorted.map((r) => r.name)).toEqual(['another_dir', 'app.exe', 'document.pdf', 'subfolder']);
  });

  it('supports legacy boolean parameter correctly', () => {
    const sortedFoldersFirst = getSortedResults(mockResults, 'name', 'asc', true, false);
    expect(sortedFoldersFirst[0].isDirectory).toBe(true);

    const sortedMixed = getSortedResults(mockResults, 'name', 'asc', false, false);
    expect(sortedMixed.map((r) => r.name)).toEqual(['another_dir', 'app.exe', 'document.pdf', 'subfolder']);
  });

  it('respects secondary sort direction for filesFirst', () => {
    const sorted = getSortedResults(mockResults, 'modified', 'desc', 'filesFirst', false);
    expect(sorted[0].name).toBe('document.pdf');
    expect(sorted[1].name).toBe('app.exe');
    expect(sorted[2].name).toBe('another_dir');
    expect(sorted[3].name).toBe('subfolder');
  });
});

describe('parseQueryKeywords', () => {
  it('extracts tokens and removes prefixes and wildcards', () => {
    expect(parseQueryKeywords('ext:txt document* !exclude')).toEqual(['txt', 'document', 'exclude']);
    expect(parseQueryKeywords('')).toEqual([]);
  });
});

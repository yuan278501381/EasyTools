import {
  startTransition,
  useCallback,
  useEffect,
  useRef,
  useState,
  type Dispatch,
  type SetStateAction,
} from 'react';
import { bridgeRequest } from '../hooks/useBridge';
import { isEmptyContentSyntax, nextQueryId, resolveDebounceMs } from '../searchScheduling';
import type { SearchMode, SearchResponse, SearchResult, SearchSnapshot } from './searchTypes';
import { parseQueryKeywords } from './searchUtils';

interface UseSearchEngineOptions {
  query: string;
  isComposing: boolean;
  activeCategory: string;
  searchMode: SearchMode;
  enabledDrives: string[];
  maxResultLimit: number;
  excludeGitAndModules: boolean;
  excludeHidden: boolean;
  customContentFormats: string[];
  disabledContentFormats: string[];
}

interface UseSearchEngineResult {
  snapshot: SearchSnapshot;
  results: SearchResult[];
  setResults: Dispatch<SetStateAction<SearchResult[]>>;
  selectedIndex: number;
  setSelectedIndex: Dispatch<SetStateAction<number>>;
  loading: boolean;
  setLoading: Dispatch<SetStateAction<boolean>>;
  serviceAvailable: boolean;
  setServiceAvailable: Dispatch<SetStateAction<boolean>>;
  isServiceStarting: boolean;
  setIsServiceStarting: Dispatch<SetStateAction<boolean>>;
  totalIndexedFiles: number;
  setTotalIndexedFiles: Dispatch<SetStateAction<number>>;
  searchElapsedMs: number;
  scanElapsedSeconds: number;
  lastColdScanDurationSec: number;
  clearResultCache: () => void;
}

interface CachedQuery {
  results: SearchResult[];
  elapsedMs: number;
  totalIndexedFiles?: number;
  timestamp: number;
}

const RESULT_CACHE_TTL_MS = 45_000;
const MAX_RESULT_CACHE_ENTRIES = 32;

/**
 * Owns the asynchronous search lifecycle: debounce, stale-response rejection,
 * service-start retries, the short-lived heavy-query cache and scan timing.
 */
export function useSearchEngine({
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
}: UseSearchEngineOptions): UseSearchEngineResult {
  const [snapshot, setSnapshot] = useState<SearchSnapshot>({
    generationId: 0,
    query: '',
    keywords: [],
    results: [],
  });

  const setResults: Dispatch<SetStateAction<SearchResult[]>> = useCallback((action) => {
    setSnapshot((prev) => {
      const nextResults = typeof action === 'function' ? action(prev.results) : action;
      return {
        ...prev,
        results: nextResults,
      };
    });
  }, []);

  const [selectedIndex, setSelectedIndex] = useState(0);
  const [loading, setLoading] = useState(false);
  const [serviceAvailable, setServiceAvailable] = useState(true);
  const [isServiceStarting, setIsServiceStarting] = useState(false);
  const [totalIndexedFiles, setTotalIndexedFiles] = useState(0);
  const [searchElapsedMs, setSearchElapsedMs] = useState(0);
  const [scanElapsedSeconds, setScanElapsedSeconds] = useState(0);
  const [lastColdScanDurationSec, setLastColdScanDurationSec] = useState(() => {
    const saved = Number.parseFloat(localStorage.getItem('easytools_last_cold_content_scan_sec') ?? '');
    return Number.isFinite(saved) ? Math.max(3, saved) : 14.5;
  });

  const requestSequenceRef = useRef(0);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<number | null>(null);
  const scanTimerRef = useRef<{ sequence: number; intervalId: number; startTime: number } | null>(null);
  const resultCacheRef = useRef<Map<string, CachedQuery>>(new Map());

  const clearResultCache = useCallback(() => {
    resultCacheRef.current.clear();
  }, []);

  const stopScanStopwatch = useCallback((targetSequence?: number) => {
    const timer = scanTimerRef.current;
    if (!timer || (targetSequence !== undefined && timer.sequence !== targetSequence)) return;
    window.clearInterval(timer.intervalId);
    scanTimerRef.current = null;
  }, []);

  const startScanStopwatch = useCallback((sequence: number) => {
    stopScanStopwatch();
    setScanElapsedSeconds(0);
    const startTime = Date.now();
    const intervalId = window.setInterval(() => {
      if (scanTimerRef.current?.sequence === sequence) {
        setScanElapsedSeconds(Number(((Date.now() - startTime) / 1000).toFixed(1)));
      }
    }, 100);
    scanTimerRef.current = { sequence, intervalId, startTime };
  }, [stopScanStopwatch]);

  useEffect(() => () => {
    stopScanStopwatch();
    if (retryTimerRef.current !== null) window.clearTimeout(retryTimerRef.current);
    requestSequenceRef.current += 1;
  }, [stopScanStopwatch]);

  useEffect(() => {
    const trimmedQuery = query.trim();
    if (!trimmedQuery) {
      stopScanStopwatch();
      if (retryTimerRef.current !== null) {
        window.clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
      startTransition(() => {
        setSnapshot({
          generationId: ++requestSequenceRef.current,
          query: '',
          keywords: [],
          results: [],
        });
        setLoading(false);
      });
      return;
    }
    if (isComposing) {
      if (retryTimerRef.current !== null) {
        window.clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
      return;
    }

    const sequence = ++requestSequenceRef.current;
    const debounceMs = resolveDebounceMs({ query: trimmedQuery, activeCategory, searchMode });

    const runQuery = async () => {
      if (sequence !== requestSequenceRef.current) return;
      if (isEmptyContentSyntax(trimmedQuery)) {
        stopScanStopwatch(sequence);
        startTransition(() => {
          setSnapshot({
            generationId: sequence,
            query: trimmedQuery,
            keywords: [],
            results: [],
          });
        });
        setLoading(false);
        return;
      }

      const effectiveMode: SearchMode = activeCategory === 'content' ? 'content' : searchMode;
      const cacheKey = [
        effectiveMode,
        trimmedQuery,
        enabledDrives.slice().sort().join(','),
        maxResultLimit,
        excludeGitAndModules ? 'exDev' : 'all',
        excludeHidden ? 'exHidden' : 'showHidden',
        disabledContentFormats.slice().sort().join(','),
        customContentFormats.slice().sort().join(','),
      ].join(':');

      const cached = resultCacheRef.current.get(cacheKey);
      if (cached && Date.now() - cached.timestamp < RESULT_CACHE_TTL_MS) {
        startTransition(() => {
          setSnapshot({
            generationId: sequence,
            query: trimmedQuery,
            keywords: parseQueryKeywords(trimmedQuery),
            results: cached.results,
          });
          setSelectedIndex(0);
          setSearchElapsedMs(cached.elapsedMs);
          if (cached.totalIndexedFiles !== undefined) setTotalIndexedFiles(cached.totalIndexedFiles);
          setLoading(false);
        });
        return;
      }
      if (cached) resultCacheRef.current.delete(cacheKey);

      const loadingTimer = window.setTimeout(() => {
        if (sequence === requestSequenceRef.current) setLoading(true);
      }, 80);
      startScanStopwatch(sequence);

      const excludes = excludeGitAndModules
        ? ['$Recycle.Bin', 'System Volume Information', 'node_modules', '.git', '__pycache__', 'npm-cache', 'go-build', '.gradle', 'pip\\cache']
        : [];

      try {
        const response = await bridgeRequest<SearchResponse>('search.query', {
          query: trimmedQuery,
          queryId: nextQueryId(),
          searchMode: effectiveMode,
          limit: maxResultLimit,
          drives: enabledDrives.length > 0 ? enabledDrives : undefined,
          excludes: excludes.length > 0 ? excludes : undefined,
          excludeHidden,
          contentCustomExts: customContentFormats,
          contentDisabledExts: disabledContentFormats,
        });

        if (response?.cancelled || sequence !== requestSequenceRef.current) return;

        if (Array.isArray(response.results) && response.results.length > 0) {
          if (resultCacheRef.current.size >= MAX_RESULT_CACHE_ENTRIES) {
            const oldestKey = resultCacheRef.current.keys().next().value as string | undefined;
            if (oldestKey !== undefined) resultCacheRef.current.delete(oldestKey);
          }
          resultCacheRef.current.set(cacheKey, {
            results: response.results,
            elapsedMs: response.elapsedMs ?? 0,
            totalIndexedFiles: response.totalIndexedFiles,
            timestamp: Date.now(),
          });
        }

        if (response.elapsedMs && response.elapsedMs >= 3500) {
          const actualColdSeconds = Number((response.elapsedMs / 1000).toFixed(1));
          setLastColdScanDurationSec(actualColdSeconds);
          localStorage.setItem('easytools_last_cold_content_scan_sec', String(actualColdSeconds));
        }

        window.clearTimeout(loadingTimer);
        if (response.status === 'starting' || (!response.available && response.status !== 'unavailable')) {
          startTransition(() => {
            setIsServiceStarting(true);
            setServiceAvailable(true);
          });
          if (retryCountRef.current < 25) {
            retryCountRef.current += 1;
            const retryDelay = Math.min(250 + retryCountRef.current * 100, 1000);
            retryTimerRef.current = window.setTimeout(() => void runQuery(), retryDelay);
          } else {
            startTransition(() => {
              setIsServiceStarting(false);
              setServiceAvailable(false);
            });
          }
          return;
        }

        retryCountRef.current = 0;
        startTransition(() => {
          setIsServiceStarting(false);
          setSnapshot({
            generationId: sequence,
            query: trimmedQuery,
            keywords: parseQueryKeywords(trimmedQuery),
            results: Array.isArray(response.results) ? response.results : [],
          });
          setSelectedIndex(0);
          if (response.totalIndexedFiles !== undefined) setTotalIndexedFiles(response.totalIndexedFiles);
          if (response.elapsedMs !== undefined) setSearchElapsedMs(response.elapsedMs);
          if (response.available !== undefined) setServiceAvailable(response.available);
        });
      } catch {
        if (sequence === requestSequenceRef.current) {
          startTransition(() => {
            setSnapshot({
              generationId: sequence,
              query: trimmedQuery,
              keywords: parseQueryKeywords(trimmedQuery),
              results: [],
            });
          });
        }
      } finally {
        window.clearTimeout(loadingTimer);
        if (sequence === requestSequenceRef.current) {
          stopScanStopwatch(sequence);
          setLoading(false);
        }
      }
    };

    retryCountRef.current = 0;
    if (retryTimerRef.current !== null) {
      window.clearTimeout(retryTimerRef.current);
      retryTimerRef.current = null;
    }
    const debounceTimer = window.setTimeout(() => void runQuery(), debounceMs);

    return () => {
      window.clearTimeout(debounceTimer);
      if (retryTimerRef.current !== null) {
        window.clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
      if (requestSequenceRef.current === sequence) requestSequenceRef.current += 1;
      stopScanStopwatch(sequence);
    };
  }, [
    activeCategory,
    customContentFormats,
    disabledContentFormats,
    enabledDrives,
    excludeGitAndModules,
    excludeHidden,
    isComposing,
    maxResultLimit,
    query,
    searchMode,
    startScanStopwatch,
    stopScanStopwatch,
  ]);

  return {
    snapshot,
    results: snapshot.results,
    setResults,
    selectedIndex,
    setSelectedIndex,
    loading,
    setLoading,
    serviceAvailable,
    setServiceAvailable,
    isServiceStarting,
    setIsServiceStarting,
    totalIndexedFiles,
    setTotalIndexedFiles,
    searchElapsedMs,
    scanElapsedSeconds,
    lastColdScanDurationSec,
    clearResultCache,
  };
}

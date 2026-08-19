// ─────────────────────────────────────────────────────────────────────────────
// searchScheduling — 搜索请求的节流与编号策略
//
// 搜索链路要跨进程访问索引服务，单次查询的代价远高于一次键盘事件。此前防抖
// 固定为 25ms，正常打字的击键间隔基本都超过这个值，等于每敲一个字符就发一次
// 查询；中文输入法逐字母组字时更是把一个词放大成十余次查询。
//
// 这里按查询代价分级：内容搜索要读盘，最贵；文件名搜索里越短的前缀命中越多，
// 也越慢。查询编号用墙钟毫秒生成并保证严格递增，服务端据此丢弃过期查询。
// ─────────────────────────────────────────────────────────────────────────────

export interface SearchScopeInput {
  /** 已去除首尾空白的查询串 */
  query: string;
  /** 当前结果分类，'content' 表示全文分类 */
  activeCategory: string;
  /** 搜索模式设置：'name' | 'content' | 'both' */
  searchMode: string;
}

/** 显式声明全文检索的查询前缀。 */
const CONTENT_PREFIXES = ['content:', '内容:'];

/**
 * 判断一次查询是否需要读取文件内容。
 *
 * 除了查询串前缀，分类与搜索模式同样会把查询切到内容通道 —— 早期版本只看前缀，
 * 导致在"内容"分类下打字仍按文件名搜索的节奏发请求。
 */
export function isContentSearch({ query, activeCategory, searchMode }: SearchScopeInput): boolean {
  if (activeCategory === 'content') return true;
  if (searchMode === 'content' || searchMode === 'both') return true;

  const lowered = query.toLowerCase();
  if (CONTENT_PREFIXES.some((prefix) => lowered.startsWith(prefix))) return true;
  // 'c:' 是内容搜索的简写，但要避开 C 盘路径。
  return lowered.startsWith('c:') && !lowered.startsWith('c:\\') && !lowered.startsWith('c:/');
}

/** 内容搜索的防抖时长。读盘代价高，等用户明显停顿后再发。 */
export const CONTENT_DEBOUNCE_MS = 400;

/** 文件名搜索的防抖时长，按前缀长度分级：越短命中越多、越慢。 */
export const NAME_DEBOUNCE_MS = {
  veryShort: 240,
  short: 160,
  normal: 110,
} as const;

/**
 * 计算一次查询应当等待的防抖时长（毫秒）。
 */
export function resolveDebounceMs(input: SearchScopeInput): number {
  if (isContentSearch(input)) return CONTENT_DEBOUNCE_MS;
  const length = input.query.length;
  if (length <= 1) return NAME_DEBOUNCE_MS.veryShort;
  if (length <= 3) return NAME_DEBOUNCE_MS.short;
  return NAME_DEBOUNCE_MS.normal;
}

let lastQueryId = 0;

/**
 * 生成严格递增的查询编号。
 *
 * 以墙钟毫秒为基准，因此重开搜索窗后依然单调，服务端的代际比较不会回退；
 * 同一毫秒内的连续调用则退化为自增，保证不产生重复编号。
 */
export function nextQueryId(now: number = Date.now()): number {
  lastQueryId = now > lastQueryId ? now : lastQueryId + 1;
  return lastQueryId;
}

/** 仅供测试使用：重置编号基准。 */
export function resetQueryIdForTest(): void {
  lastQueryId = 0;
}

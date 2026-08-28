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
const CONTENT_PREFIXES = ['content:', '\u5185\u5bb9:'];

/**
 * 检查当前查询是否仅为内容搜索语法前缀（未提供实际内容关键词）。
 */
export function isEmptyContentSyntax(query: string): boolean {
  const lowered = query.trim().toLowerCase();
  return lowered === 'content:' || lowered === '\u5185\u5bb9:' || lowered === 'c:';
}

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

/** 内容搜索的防抖时长分级：读盘与文本提取代价高，按关键词成熟度分级。 */
export const CONTENT_DEBOUNCE_MS = {
  short: 500,  // <= 2 字符（如 content:cr）：给足打字缓冲，避免单字母过早扫盘
  normal: 350, // >= 3 字符（如 content:create）：用户输入完单词自然停顿即刻触发
} as const;

/** 文件名搜索的防抖时长分级：基于人类正常击键间隔 (150~250ms) 精准吸纳连续打字。 */
export const NAME_DEBOUNCE_MS = {
  veryShort: 300, // <= 1 字符：防止单字母引发大范围内存初筛
  short: 220,     // 2~3 字符：平滑吸纳快速打字击键
  normal: 180,    // >= 4 字符：精准词停顿 180ms 即刻毫秒级呈现
} as const;

/**
 * 提取去除语法前缀后的核心搜索词。
 */
export function extractContentKeyword(query: string): string {
  const trimmed = query.trim();
  const lowered = trimmed.toLowerCase();
  for (const prefix of CONTENT_PREFIXES) {
    if (lowered.startsWith(prefix)) {
      return trimmed.slice(prefix.length).trim();
    }
  }
  if (lowered.startsWith('c:') && !lowered.startsWith('c:\\') && !lowered.startsWith('c:/')) {
    return trimmed.slice(2).trim();
  }
  return trimmed;
}

/**
 * 计算一次查询应当等待的防抖时长（毫秒）。
 * 基于人类打字生理节奏 (150ms~250ms/键)，在连续键入期间平滑静默，用户手指停顿时秒级响应。
 */
export function resolveDebounceMs(input: SearchScopeInput): number {
  if (isContentSearch(input)) {
    const keyword = extractContentKeyword(input.query);
    return keyword.length <= 2 ? CONTENT_DEBOUNCE_MS.short : CONTENT_DEBOUNCE_MS.normal;
  }
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

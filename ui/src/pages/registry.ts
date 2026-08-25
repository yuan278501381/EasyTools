/* ─────────────────────────────────────────────────────────────────────────────
 * registry.ts — 设置中心页面元数据与统一注册表
 *
 * 架构目标:
 *   1. 统一管理所有设置菜单项的 ID、标题、副标题与分类，解耦 Sidebar 与 App。
 *   2. 天然支持未来随时增减菜单页、重命名或由动态扩展插件注入新设置页。
 * ───────────────────────────────────────────────────────────────────────────── */

export type BuiltinNavId =
  | 'general'
  | 'plugins'
  | 'search'
  | 'gesture'
  | 'hotcorner'
  | 'capture'
  | 'history'
  | 'ocr'
  | 'dialog_enhancer'
  | 'stats'
  | 'about'
  | 'ai_assistant'
  | 'color_picker'
  | 'clipboard_manager'
  | 'markdown_preview';

export type NavId = BuiltinNavId | (string & {});

export type NavCategory = 'system' | 'core_tools' | 'insights' | 'extension';

export interface PageDefinition {
  id: NavId;
  titleKey: string;
  subtitleKey: string;
  category: NavCategory;
  requiresPlugin?: 'gesture' | 'capture' | 'search' | 'dialogenhancer' | 'dialog_enhancer';
}

export const PAGE_DEFINITIONS: PageDefinition[] = [
  // ── 系统设置 ──────────────────────────────────────────────────────────────
  { id: 'general', titleKey: 'nav.settings', subtitleKey: 'navSubtitle.general', category: 'system' },
  { id: 'plugins', titleKey: 'nav.plugins', subtitleKey: 'navSubtitle.plugins', category: 'system' },

  // ── 核心效率工具 ──────────────────────────────────────────────────────────
  { id: 'search', titleKey: 'nav.search', subtitleKey: 'navSubtitle.search', category: 'core_tools', requiresPlugin: 'search' },
  { id: 'gesture', titleKey: 'nav.gesture', subtitleKey: 'navSubtitle.gesture', category: 'core_tools', requiresPlugin: 'gesture' },
  { id: 'hotcorner', titleKey: 'nav.hotcorner', subtitleKey: 'navSubtitle.hotcorner', category: 'core_tools', requiresPlugin: 'gesture' },
  { id: 'capture', titleKey: 'nav.capture', subtitleKey: 'navSubtitle.capture', category: 'core_tools', requiresPlugin: 'capture' },
  { id: 'history', titleKey: 'nav.history', subtitleKey: 'navSubtitle.history', category: 'core_tools', requiresPlugin: 'capture' },
  { id: 'ocr', titleKey: 'nav.ocr', subtitleKey: 'navSubtitle.ocr', category: 'core_tools', requiresPlugin: 'capture' },
  { id: 'dialog_enhancer', titleKey: 'nav.dialog_enhancer', subtitleKey: 'navSubtitle.dialog_enhancer', category: 'core_tools', requiresPlugin: 'dialogenhancer' },

  // ── 洞察与关于 ────────────────────────────────────────────────────────────
  { id: 'stats', titleKey: 'nav.stats', subtitleKey: 'navSubtitle.stats', category: 'insights' },
  { id: 'about', titleKey: 'nav.about', subtitleKey: 'navSubtitle.about', category: 'insights' },

  // ── 扩展应用 ──────────────────────────────────────────────────────────────
  { id: 'ai_assistant', titleKey: 'nav.ai_assistant', subtitleKey: 'navSubtitle.ai_assistant', category: 'extension' },
  { id: 'color_picker', titleKey: 'nav.color_picker', subtitleKey: 'navSubtitle.color_picker', category: 'extension' },
  { id: 'clipboard_manager', titleKey: 'nav.clipboard_manager', subtitleKey: 'navSubtitle.clipboard_manager', category: 'extension' },
  { id: 'markdown_preview', titleKey: 'nav.markdown_preview', subtitleKey: 'navSubtitle.markdown_preview', category: 'extension' },
];

const PAGE_MAP = new Map<string, PageDefinition>(PAGE_DEFINITIONS.map(p => [p.id, p]));

/**
 * 获取页面的国际化标题与副标题 key
 * 具备健壮的兜底能力：即使遇到未来新增但未在此注册的动态扩展页，也能平滑解析为 nav.<id>
 */
export function getPageMetadata(id: string): { titleKey: string; subtitleKey: string } {
  const item = PAGE_MAP.get(id);
  if (item) {
    return { titleKey: item.titleKey, subtitleKey: item.subtitleKey };
  }
  return {
    titleKey: `nav.${id}`,
    subtitleKey: `navSubtitle.${id}`,
  };
}

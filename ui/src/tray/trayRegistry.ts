/* ─────────────────────────────────────────────────────────────────────────────
 * trayRegistry.ts — 托盘快捷开关数据驱动注册中心 (World-Class Extensible Grid)
 * ───────────────────────────────────────────────────────────────────────────── */

import type { ComponentType } from 'react';
import {
  Mouse,
  Keyboard,
  Camera,
  Search,
  Sparkles,
  FolderSymlink,
  Cast,
} from 'lucide-react';

export interface TrayExtraState {
  gesturePaused: boolean;
  remoteBoostEnabled?: boolean;
}

export interface TrayControlItem {
  /** 唯一功能键名 */
  id: string;
  /** 关联的主插件/模块 ID */
  pluginId: string;
  /** 兼容备用插件 ID (例如 dialog_enhancer / dialogenhancer) */
  aliasPluginIds?: string[];
  /** i18n 极简 2~3 字标签键 */
  labelKey: string;
  /** 默认极简中文显示（如：手势、回显、截图、搜索、特效、助手） */
  fallbackLabel: string;
  /** i18n 详尽功能描述键 (用于悬浮气泡 Tooltip) */
  descKey: string;
  /** 默认详尽功能描述 */
  fallbackDesc: string;
  /** 统一 Lucide 矢量 SVG 图标组件 */
  icon: ComponentType<{ size?: number; className?: string }>;
  /** 显示优先级排序 */
  order: number;
  /** 自定义活跃判定 (例如手势区分 paused) */
  getCustomActive?: (activePlugins: Set<string>, extraState: TrayExtraState) => boolean;
}

/**
 * 托盘快捷功能开关注册表
 * 未来如需增减功能（如 AI 助手、剪贴板历史等），只需在此处增减配置，全链路自适应生效
 */
export const TRAY_CONTROL_REGISTRY: TrayControlItem[] = [
  {
    id: 'gesture',
    pluginId: 'gesture',
    labelKey: 'tray.pillGesture',
    fallbackLabel: 'Gestures',
    descKey: 'tray.pillGestureFullDesc',
    fallbackDesc: 'Mouse Gestures (Global gestures & hot corners)',
    icon: Mouse,
    order: 10,
    getCustomActive: (activePlugins, { gesturePaused }) => activePlugins.has('gesture') && !gesturePaused,
  },
  {
    id: 'keycast',
    pluginId: 'keycast',
    labelKey: 'tray.pillKeycast',
    fallbackLabel: 'Keycast',
    descKey: 'tray.pillKeycastFullDesc',
    fallbackDesc: 'Keycast (Real-time keystrokes on screen)',
    icon: Keyboard,
    order: 20,
  },
  {
    id: 'capture',
    pluginId: 'capture',
    labelKey: 'tray.pillCapture',
    fallbackLabel: 'Capture',
    descKey: 'tray.pillCaptureFullDesc',
    fallbackDesc: 'Capture (Screenshots, stitching & screen recording)',
    icon: Camera,
    order: 30,
  },
  {
    id: 'search',
    pluginId: 'search',
    labelKey: 'tray.pillSearch',
    fallbackLabel: 'Search',
    descKey: 'tray.pillSearchFullDesc',
    fallbackDesc: 'File Search (Instant NTFS lightning search)',
    icon: Search,
    order: 40,
  },
  {
    id: 'spotlight',
    pluginId: 'spotlight',
    labelKey: 'tray.pillSpotlight',
    fallbackLabel: 'Effects',
    descKey: 'tray.pillSpotlightFullDesc',
    fallbackDesc: 'Mouse Effects (Spotlight, ripples & motion trails)',
    icon: Sparkles,
    order: 50,
  },
  {
    id: 'dialog_enhancer',
    pluginId: 'dialogenhancer',
    aliasPluginIds: ['dialog_enhancer', 'dialogenhancer'],
    labelKey: 'tray.pillDialog',
    fallbackLabel: 'Dialog',
    descKey: 'tray.pillDialogFullDesc',
    fallbackDesc: 'Dialog Enhancer (Instant jump & recent paths in dialogs)',
    icon: FolderSymlink,
    order: 60,
  },
  {
    id: 'remote_boost',
    pluginId: 'remote_boost',
    labelKey: 'tray.pillRemoteBoost',
    fallbackLabel: 'Remote',
    descKey: 'tray.pillRemoteBoostFullDesc',
    fallbackDesc: 'Remote Boost (Immersive hotkey tunnel & emergency flush)',
    icon: Cast,
    order: 70,
    getCustomActive: (_activePlugins, { remoteBoostEnabled }) => Boolean(remoteBoostEnabled),
  },
];

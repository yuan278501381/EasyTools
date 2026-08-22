// ─────────────────────────────────────────────────────────────────────────────
// useBridge — WebView2 ↔ C++ IPC 通信 Hook
//
// 提供 request(method, params) 方法调用 C++ 处理器，
// 自动管理消息 ID、响应匹配和事件监听。
// ─────────────────────────────────────────────────────────────────────────────

import { useCallback, useEffect, useRef } from 'react';

type MessageHandler = (data: unknown) => void;

export class BridgeError extends Error {
  readonly code?: string | number;

  constructor(reason: unknown) {
    const record = reason && typeof reason === 'object' ? reason as Record<string, unknown> : null;
    super(typeof record?.message === 'string' ? record.message : String(reason || 'Bridge request failed'));
    this.name = 'BridgeError';
    if (typeof record?.code === 'string' || typeof record?.code === 'number') this.code = record.code;
  }
}

let nextId = 1;
const pendingRequests = new Map<number, {
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
  timeoutId: ReturnType<typeof setTimeout>;
}>();
const eventListeners = new Map<string, Set<MessageHandler>>();
const LONG_RUNNING_METHODS = new Set(['config.export', 'config.import', 'capture.browseDirectory']);

// 全局消息监听（只注册一次）
let initialized = false;
function initGlobalListener() {
  if (initialized) return;
  initialized = true;

  window.chrome?.webview?.addEventListener('message', (e: MessageEvent) => {
    try {
      const msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;

      if (msg.type === 'event') {
        // C++ → JS 事件推送
        const listeners = eventListeners.get(msg.event);
        if (listeners) {
          listeners.forEach(handler => {
            try {
              handler(msg.data);
            } catch (error) {
              console.error(`[Bridge] Event handler failed (${String(msg.event)}):`, error);
            }
          });
        }
      } else if (msg.id !== undefined) {
        // 响应匹配
        const pending = pendingRequests.get(msg.id);
        if (pending) {
          pendingRequests.delete(msg.id);
          clearTimeout(pending.timeoutId);
          if (msg.error) {
            pending.reject(new BridgeError(msg.error));
          } else {
            pending.resolve(msg.result);
          }
        }
      }
    } catch (err) {
      console.error('[Bridge] Failed to parse message:', err);
    }
  });
}

/**
 * 向 C++ 发送请求
 */
export function bridgeRequest<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
  initGlobalListener();

  return new Promise((resolve, reject) => {
    if (nextId >= Number.MAX_SAFE_INTEGER) nextId = 1;
    while (pendingRequests.has(nextId)) nextId += 1;
    const id = nextId++;
    // 文件/目录选择器允许用户停留；普通 IPC 仍保持快速失败，避免悬空 Promise。
    const timeoutMs = LONG_RUNNING_METHODS.has(method)
      ? 5 * 60_000
      : 10_000;
    const timeoutId = setTimeout(() => {
      if (pendingRequests.delete(id)) {
        reject(new Error(`Bridge request timeout: ${method}`));
      }
    }, timeoutMs);
    pendingRequests.set(id, { resolve: resolve as (v: unknown) => void, reject, timeoutId });

    const message = JSON.stringify({ id, method, params });

    if (window.chrome?.webview) {
      try {
        window.chrome.webview.postMessage(message);
      } catch (error) {
        pendingRequests.delete(id);
        clearTimeout(timeoutId);
        reject(new BridgeError(error));
      }
    } else {
      // 开发模式下模拟响应
      console.warn('[Bridge] WebView2 not available, mocking response for:', method);
      pendingRequests.delete(id);
      clearTimeout(timeoutId);
      resolve(getMockResponse(method, params) as T);
    }
  });
}

/**
 * React Hook: 使用 IPC 通信
 */
export function useBridge() {
  const request = useCallback(<T = unknown>(method: string, params: Record<string, unknown> = {}) => {
    return bridgeRequest<T>(method, params);
  }, []);

  return { request };
}

/**
 * React Hook: 监听 C++ 推送的事件
 */
export function useBridgeEvent(eventName: string, handler: MessageHandler) {
  const handlerRef = useRef(handler);
  // 在 effect 中同步最新 handler，避免在渲染期间写入 ref
  useEffect(() => {
    handlerRef.current = handler;
  });

  useEffect(() => {
    initGlobalListener();

    const wrappedHandler: MessageHandler = (data) => handlerRef.current(data);

    if (!eventListeners.has(eventName)) {
      eventListeners.set(eventName, new Set());
    }
    eventListeners.get(eventName)!.add(wrappedHandler);

    return () => {
      eventListeners.get(eventName)?.delete(wrappedHandler);
    };
  }, [eventName]);
}

// ── 开发模式 Mock 数据 ──────────────────────────────────────────────────────
let mockPlugins = [
  { id: 'capture', name: 'Capture', version: '1.0.0', fileName: 'Plugin_Capture.dll', enabled: true, active: true, restartRequired: false, state: 'running' },
  { id: 'gesture', name: 'Gesture', version: '1.0.0', fileName: 'Plugin_Gesture.dll', enabled: true, active: true, restartRequired: false, state: 'running' },
  { id: 'keycast', name: 'Keycast', version: '1.0.0', fileName: 'Plugin_Keycast.dll', enabled: false, active: false, restartRequired: false, state: 'disabled' },
  { id: 'search', name: 'Search', version: '1.0.0', fileName: 'Plugin_Search.dll', enabled: true, active: true, restartRequired: false, state: 'running' },
];

function getMockResponse(method: string, params: Record<string, unknown> = {}): unknown {
  switch (method) {
    case 'config.getAll':
      return {
        gesture: { enabled: true, triggerButton: 'right', trailVisible: true },
        capture: { format: 'png', quality: 95, copyToClipboard: true, saveToFile: true },
        recording: { format: 'mp4_h264', fps: 30, bitrate: 8 },
        general: { theme: 'light', language: 'zh-CN', autoStart: false },
        ocr: { engine: 'windows', language: 'system', copyResult: true, showResultWindow: true },
      };

    case 'plugins.getAll':
      return mockPlugins;

    case 'plugins.setEnabled': {
      const id = String(params.id ?? '');
      const enabled = Boolean(params.enabled);
      mockPlugins = mockPlugins.map((plugin) => plugin.id === id
        ? { ...plugin, enabled, restartRequired: enabled !== plugin.active, state: enabled !== plugin.active ? 'pendingRestart' : (plugin.active ? 'running' : 'disabled') }
        : plugin);
      return { success: mockPlugins.some((plugin) => plugin.id === id), restartRequired: true };
    }

    case 'gesture.getProfiles':
      return [
        {
          name: 'default',
          mappings: [
            { gestureCode: 'L', action: { type: 0, name: '后退', keyStroke: 'Alt+Left' } },
            { gestureCode: 'R', action: { type: 0, name: '前进', keyStroke: 'Alt+Right' } },
            { gestureCode: 'U', action: { type: 0, name: '关闭窗口', keyStroke: 'Alt+F4' } },
            { gestureCode: 'D', action: { type: 0, name: '新建标签页', keyStroke: 'Ctrl+T' } },
            { gestureCode: 'UL', action: { type: 0, name: '复制', keyStroke: 'Ctrl+C' } },
            { gestureCode: 'DR', action: { type: 0, name: '关闭标签页', keyStroke: 'Ctrl+W' } },
            { gestureCode: 'LU', action: { type: 0, name: '剪切', keyStroke: 'Ctrl+X' } },
            { gestureCode: 'UR', action: { type: 2, name: '最大化', builtinCmd: 2 } },
            { gestureCode: 'DL', action: { type: 2, name: '最小化', builtinCmd: 3 } },
            { gestureCode: 'U-R', action: { type: 0, name: '下一个标签页', keyStroke: 'Ctrl+Tab' } },
            { gestureCode: 'U-L', action: { type: 0, name: '上一个标签页', keyStroke: 'Ctrl+Shift+Tab' } },
            { gestureCode: 'D-U', action: { type: 0, name: '刷新', keyStroke: 'F5' } },
            { gestureCode: 'U-D', action: { type: 0, name: '撤销', keyStroke: 'Ctrl+Z' } },
            { gestureCode: 'R-L', action: { type: 0, name: '全选', keyStroke: 'Ctrl+A' } },
          ],
        },
      ];

    case 'gesture.getState':
      return {
        enabled: true,
        paused: false,
        triggerButton: 'right',
        trailVisible: true,
        targetMode: 'underPointer',
        elevated: false,
        runAsAdmin: false,
      };

    case 'gesture.getScopeRules':
      return [
        {
          id: 'mock-1', name: 'Chrome 浏览器', enabled: true,
          processName: 'chrome.exe', windowClass: '', matchMode: 0, effect: 2, profileName: 'browser',
        },
        {
          id: 'mock-2', name: '游戏全屏禁用', enabled: true,
          processName: '*.exe', windowClass: '', matchMode: 1, effect: 1, profileName: '',
        },
      ];

    case 'capture.getSettings':
      return {
        format: 'png', quality: 90, saveToFile: true, copyToClipboard: true,
        saveDirectory: '', showCrosshair: true, autoDetectWindow: true,
        showShortcutHints: true,
      };

    case 'recording.getSettings':
      return {
        format: 'mp4_h264', fps: 30, bitrate: 8, saveDirectory: '',
      };

    case 'general.getSettings':
      return {
        language: 'zh-CN', autoStart: false, runAsAdmin: false, elevated: false, theme: 'light',
        logLevel: 'info', minimizeToTray: true, checkUpdates: true,
      };

    case 'ocr.getSettings':
      return {
        engine: 'windows', language: 'system', copyResult: true,
        showResultWindow: true,
      };

    case 'ocr.getStatus':
      return { available: true };

    case 'ocr.recognizeImageFile':
      return { success: true, text: '(mock) 识别到的示例文字', copied: true };

    case 'history.getAll':
      return [];

    case 'history.open':
      return { success: true };

    case 'stats.getHistory': {
      const today = new Date();
      return Object.fromEntries(Array.from({ length: 7 }, (_, index) => {
        const date = new Date(today);
        date.setDate(today.getDate() - (6 - index));
        const dateKey = [
          date.getFullYear(),
          String(date.getMonth() + 1).padStart(2, '0'),
          String(date.getDate()).padStart(2, '0'),
        ].join('-');
        return [dateKey, {
          totalKeys: 7800 + index * 1260,
          leftClicks: 980 + index * 94,
          rightClicks: 180 + index * 21,
          mouseDistance: 182000 + index * 24500,
          keyMap: { 8: 315, 13: 482, 32: 1210, 65: 720, 69: 884, 73: 691, 78: 742, 79: 804, 83: 765, 84: 910 },
        }];
      }));
    }

    case 'stats.getTotal':
      return { totalKeystrokes: 128640 };

    case 'search.query':
      return { available: true, results: [] };

    case 'perf.getMetrics':
      return { memoryMB: 42.5, cpuPercent: 0.8, screenshotLatencyMs: 0, gestureLatencyMs: 1.2, uiRenderLatencyMs: 4.1 };

    case 'app.getSystemInfo':
      return { version: '1.0.0', cpuArch: 'x64', cpuCores: 12, totalMemoryGB: 32, dpiScale: 1 };

    case 'app.checkForUpdates':
      return { success: true, started: false };

    case 'hotcorner.getSettings':
      return {
        enabled: false,
        delay: 300,
        corners: {
          topLeft:     { commandIndex: -1 },
          topRight:    { commandIndex: 2 },
          bottomLeft:  { commandIndex: 5 },
          bottomRight: { commandIndex: -1 },
        },
      };

    case 'radialmenu.getItems':
      return { items: [
        { label: '截图', command: '10' },
        { label: '搜索', command: '16' },
        { label: '锁屏', command: '8' },
        { label: '贴图', command: '18' },
      ] };

    case 'hotkey.getAll':
      return [
        { name: 'Screenshot', shortcut: 'Ctrl+Shift+A' },
        { name: 'Record', shortcut: 'Ctrl+Shift+R' },
        { name: 'OCR', shortcut: 'Ctrl+Shift+O' },
        { name: 'Pause Gestures', shortcut: 'Ctrl+Alt+Shift+W' },
        { name: 'Toggle Search', shortcut: 'Alt+Space' },
        { name: 'Pin Toggle', shortcut: 'Ctrl+Alt+Shift+X' },
        { name: 'Pin Paste', shortcut: 'Ctrl+Alt+Shift+V' },
        { name: 'Pin Hide All', shortcut: 'Ctrl+Alt+Shift+H' },
        { name: 'Pin Arrange', shortcut: 'Ctrl+Alt+Shift+G' },
      ];

    case 'config.get':
      return false;

    // 所有 update / action 方法返回成功
    case 'config.export':
    case 'config.import':
    case 'config.reset':
    case 'capture.updateSettings':
    case 'recording.updateSettings':
    case 'general.updateSettings':
    case 'ocr.updateSettings':
    case 'gesture.updateProfile':
    case 'gesture.updateScopeRules':
    case 'gesture.setPaused':
    case 'gesture.updateSettings':
    case 'hotcorner.updateSettings':
    case 'radialmenu.updateItems':
    case 'hotkey.rebind':
    case 'config.set':
    case 'app.restart':
    case 'app.restartElevated':
      return { success: true };

    default:
      return {};
  }
}

// ── TypeScript 声明扩展 ─────────────────────────────────────────────────────
declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage: (message: string) => void;
        addEventListener: (type: string, listener: (e: MessageEvent) => void) => void;
      };
    };
  }
}

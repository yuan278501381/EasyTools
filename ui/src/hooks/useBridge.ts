// ─────────────────────────────────────────────────────────────────────────────
// useBridge — WebView2 ↔ C++ IPC 通信 Hook
//
// 提供 request(method, params) 方法调用 C++ 处理器，
// 自动管理消息 ID、响应匹配和事件监听。
// ─────────────────────────────────────────────────────────────────────────────

import { useCallback, useEffect, useRef } from 'react';

type MessageHandler = (data: unknown) => void;

let nextId = 1;
const pendingRequests = new Map<number, {
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
}>();
const eventListeners = new Map<string, Set<MessageHandler>>();

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
          listeners.forEach(handler => handler(msg.data));
        }
      } else if (msg.id !== undefined) {
        // 响应匹配
        const pending = pendingRequests.get(msg.id);
        if (pending) {
          pendingRequests.delete(msg.id);
          if (msg.error) {
            pending.reject(msg.error);
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
    const id = nextId++;
    pendingRequests.set(id, { resolve: resolve as (v: unknown) => void, reject });

    const message = JSON.stringify({ id, method, params });

    if (window.chrome?.webview) {
      window.chrome.webview.postMessage(message);
    } else {
      // 开发模式下模拟响应
      console.warn('[Bridge] WebView2 not available, mocking response for:', method);
      pendingRequests.delete(id);
      resolve(getMockResponse(method) as T);
    }

    // 超时处理
    setTimeout(() => {
      if (pendingRequests.has(id)) {
        pendingRequests.delete(id);
        reject(new Error(`Bridge request timeout: ${method}`));
      }
    }, 10000);
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
  handlerRef.current = handler;

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
function getMockResponse(method: string): unknown {
  switch (method) {
    case 'config.getAll':
      return {
        gesture: { enabled: true, triggerButton: 'right', trailVisible: true },
        capture: { format: 'png', quality: 95, copyToClipboard: true, saveToFile: true },
        recording: { format: 'mp4_h264', fps: 30, bitrate: 8 },
        general: { theme: 'light', language: 'zh-CN', autoStart: false },
        ocr: { engine: 'paddleocr', language: 'ch', autoOcr: false },
      };

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
      };

    case 'gesture.getScopeRules':
      return [];

    case 'capture.getSettings':
      return {
        format: 'png', quality: 90, saveToFile: true, copyToClipboard: true,
        savePath: '', showCrosshair: true, autoDetectWindow: true,
      };

    case 'recording.getSettings':
      return {
        format: 'mp4_h264', fps: 30, bitrate: 8,
        includeAudio: false, savePath: '',
      };

    case 'general.getSettings':
      return {
        language: 'zh-CN', autoStart: false, theme: 'light',
        logLevel: 'info', minimizeToTray: true, checkUpdates: true,
      };

    case 'ocr.getSettings':
      return {
        engine: 'paddleocr', language: 'ch',
        autoOcr: false, copyResult: true,
      };

    // 所有 update 方法返回成功
    case 'capture.updateSettings':
    case 'recording.updateSettings':
    case 'general.updateSettings':
    case 'ocr.updateSettings':
    case 'gesture.updateProfile':
    case 'gesture.updateScopeRules':
    case 'gesture.setPaused':
    case 'gesture.updateSettings':
    case 'config.set':
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

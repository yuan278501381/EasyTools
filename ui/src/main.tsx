import { StrictMode, Component } from 'react'
import type { ErrorInfo, ReactNode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import './i18n/config'
import App from './App.tsx'
import SearchApp from './SearchApp.tsx'
import TrayApp from './TrayApp.tsx'
import QuickLookApp from './QuickLookApp.tsx'

class ErrorBoundary extends Component<{children: ReactNode}, {hasError: boolean, error: Error | null}> {
  constructor(props: {children: ReactNode}) {
    super(props);
    this.state = { hasError: false, error: null };
  }
  static getDerivedStateFromError(error: Error) {
    return { hasError: true, error };
  }
  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error("React Error:", error, errorInfo);
  }
  render() {
    if (this.state.hasError) {
      return (
        <div role="alert" style={{ maxWidth: '640px', margin: '10vh auto', padding: '24px', color: 'CanvasText', background: 'Canvas', fontFamily: 'Segoe UI, sans-serif' }}>
          <h1 style={{ fontSize: '1.4rem' }}>界面暂时无法显示</h1>
          <p style={{ margin: '12px 0' }}>重新加载后可以继续使用，您的设置不会丢失。</p>
          <button type="button" onClick={() => window.location.reload()} style={{ padding: '8px 16px' }}>
            重新加载
          </button>
          <details style={{ marginTop: '16px' }}>
            <summary>技术详情</summary>
            <pre style={{ marginTop: '8px', whiteSpace: 'pre-wrap', userSelect: 'text' }}>
              {this.state.error?.stack || this.state.error?.message}
            </pre>
          </details>
        </div>
      );
    }
    return this.props.children;
  }
}

const isSearch = window.location.pathname === '/search' || window.location.hash.includes('/search') || window.location.search.includes('search=1');
const isTray = window.location.search.includes('tray=1');
const isQuickLook = window.location.pathname === '/quicklook' || window.location.hash.includes('/quicklook') || window.location.search.includes('quicklook=1');

try {
  const initialAccent = localStorage.getItem('easytools:accent-color') || 'violet';
  document.documentElement.setAttribute('data-accent', initialAccent);
} catch (e) {
  void e;
}

if (isTray) {
  document.documentElement.dataset.surface = 'tray';
} else if (isSearch) {
  document.documentElement.dataset.surface = 'search';
} else if (isQuickLook) {
  document.documentElement.dataset.surface = 'quicklook';
}

window.onerror = function (msg, url, lineNo, columnNo, error) {
  if (document.getElementById('easytools-global-error')) return;
  const errDiv = document.createElement('div');
  errDiv.id = 'easytools-global-error';
  errDiv.setAttribute('role', 'alert');
  errDiv.style.cssText = 'position:fixed;inset:0;background:Canvas;color:CanvasText;z-index:9999;padding:32px;font-family:Segoe UI,sans-serif;white-space:pre-wrap;';
  const message = document.createElement('pre');
  message.textContent = `界面遇到错误，请重新加载。\n\n${String(msg)}\n${url}:${lineNo}:${columnNo}\n${error?.stack || ''}`;
  message.style.userSelect = 'text';
  const reload = document.createElement('button');
  reload.type = 'button';
  reload.textContent = '重新加载';
  reload.style.cssText = 'margin-top:16px;padding:8px 16px;';
  reload.addEventListener('click', () => window.location.reload());
  errDiv.append(message, reload);
  document.body.appendChild(errDiv);
  reload.focus();
};

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ErrorBoundary>
      {isTray ? <TrayApp /> : (isSearch ? <SearchApp /> : (isQuickLook ? <QuickLookApp /> : <App />))}
    </ErrorBoundary>
  </StrictMode>,
)

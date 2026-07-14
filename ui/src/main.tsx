import { StrictMode, Component } from 'react'
import type { ErrorInfo, ReactNode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import './i18n/config'
import App from './App.tsx'
import SearchApp from './SearchApp.tsx'
import TrayApp from './TrayApp.tsx'

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
        <div style={{ position: 'fixed', top: 0, left: 0, width: '100%', background: 'red', color: 'white', zIndex: 9999, padding: '20px', fontFamily: 'monospace', whiteSpace: 'pre-wrap' }}>
          <h1>React Crashed</h1>
          <p>{this.state.error?.toString()}</p>
          <pre>{this.state.error?.stack}</pre>
        </div>
      );
    }
    return this.props.children;
  }
}

const isSearch = window.location.pathname === '/search' || window.location.hash.includes('/search') || window.location.search.includes('search=1');
const isTray = window.location.search.includes('tray=1');

window.onerror = function (msg, url, lineNo, columnNo, error) {
  const errDiv = document.createElement('div');
  errDiv.style.cssText = 'position:fixed;top:0;left:0;width:100%;background:red;color:white;z-index:9999;padding:20px;font-family:monospace;white-space:pre-wrap;';
  errDiv.innerHTML = `Error: ${msg}\nURL: ${url}\nLine: ${lineNo}:${columnNo}\nStack: ${error?.stack}`;
  document.body.appendChild(errDiv);
};

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ErrorBoundary>
      {isTray ? <TrayApp /> : (isSearch ? <SearchApp /> : <App />)}
    </ErrorBoundary>
  </StrictMode>,
)

import { useState, useEffect, useRef } from 'react';
import './SearchApp.css';

export default function SearchApp() {
    const [query, setQuery] = useState('');
    const [results, setResults] = useState<string[]>([]);
    const [selectedIndex, setSelectedIndex] = useState(0);
    const inputRef = useRef<HTMLInputElement>(null);

    useEffect(() => {
        inputRef.current?.focus();

        const handleMessage = (msg: any) => {
            if (msg.results) {
                setResults(msg.results);
                setSelectedIndex(0);
            }
        };

        // Connect to C++ message bridge
        if ((window as any).easyToolsBridge) {
            (window as any).easyToolsBridge.onMessage = handleMessage;
        }

        return () => {
            if ((window as any).easyToolsBridge) {
                (window as any).easyToolsBridge.onMessage = null;
            }
        };
    }, []);

    useEffect(() => {
        if (!query.trim()) {
            setResults([]);
            return;
        }

        // Send query to C++ Plugin_Search
        if ((window as any).chrome?.webview) {
            (window as any).chrome.webview.postMessage(JSON.stringify({
                method: "search.query",
                query: query
            }));
        } else {
            // Mock data for browser testing
            setResults(['React.js', 'Redux.ts', 'NodeJS.exe', 'EasyTools.exe', 'README.md'].filter(i => i.toLowerCase().includes(query.toLowerCase())));
        }
    }, [query]);

    const handleKeyDown = (e: React.KeyboardEvent) => {
        if (e.key === 'ArrowDown') {
            e.preventDefault();
            setSelectedIndex(i => Math.min(i + 1, results.length - 1));
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            setSelectedIndex(i => Math.max(i - 1, 0));
        } else if (e.key === 'Enter') {
            e.preventDefault();
            if (results.length > 0) {
                const selectedFile = results[selectedIndex];
                if ((window as any).chrome?.webview) {
                    (window as any).chrome.webview.postMessage(JSON.stringify({
                        method: "search.openFile",
                        filepath: selectedFile
                    }));
                    (window as any).chrome.webview.postMessage(JSON.stringify({
                        method: "search.hide"
                    }));
                }
            }
        } else if (e.key === 'Escape') {
            e.preventDefault();
            if ((window as any).chrome?.webview) {
                (window as any).chrome.webview.postMessage(JSON.stringify({
                    method: "search.hide"
                }));
            }
        } else if (e.key === 'p' && e.ctrlKey) {
            e.preventDefault();
            if (results.length > 0) {
                const selectedFile = results[selectedIndex];
                if ((window as any).chrome?.webview) {
                    (window as any).chrome.webview.postMessage(JSON.stringify({
                        method: "capture.pinImageFile",
                        path: selectedFile
                    }));
                    (window as any).chrome.webview.postMessage(JSON.stringify({
                        method: "search.hide"
                    }));
                }
            }
        }
    };

    return (
        <div className="search-app">
            <div className="search-container">
                <div className="search-input-wrapper">
                    <svg className="search-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                        <circle cx="11" cy="11" r="8" />
                        <line x1="21" y1="21" x2="16.65" y2="16.65" />
                    </svg>
                    <input 
                        ref={inputRef}
                        className="search-input" 
                        placeholder="搜索文件或拼音首字母..."
                        value={query}
                        onChange={e => setQuery(e.target.value)}
                        onKeyDown={handleKeyDown}
                    />
                </div>
                {results.length > 0 && (
                    <ul className="search-results">
                        {results.map((res, idx) => (
                            <li 
                                key={idx} 
                                className={`search-result-item ${idx === selectedIndex ? 'selected' : ''}`}
                                onMouseEnter={() => setSelectedIndex(idx)}
                                onDoubleClick={() => {
                                    if ((window as any).chrome?.webview) {
                                        (window as any).chrome.webview.postMessage(JSON.stringify({
                                            method: "search.openFile",
                                            filepath: res
                                        }));
                                        (window as any).chrome.webview.postMessage(JSON.stringify({
                                            method: "search.hide"
                                        }));
                                    }
                                }}
                            >
                                <svg className="file-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                                    <path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path>
                                    <polyline points="13 2 13 9 20 9"></polyline>
                                </svg>
                                <span className="file-name">{res}</span>
                            </li>
                        ))}
                    </ul>
                )}
                {results.length > 0 && (
                    <div className="search-footer">
                        <span className="search-hint"><kbd>Enter</kbd> 打开文件</span>
                        <span className="search-hint"><kbd>Ctrl+P</kbd> 悬浮贴图</span>
                        <span className="search-hint"><kbd>Esc</kbd> 退出</span>
                    </div>
                )}
            </div>
        </div>
    );
}

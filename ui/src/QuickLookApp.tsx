import { useState, useEffect, useMemo, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { 
  File, 
  Folder, 
  ExternalLink, 
  FolderOpen, 
  Copy, 
  X, 
  FileText, 
  Image, 
  Film, 
  Music, 
  Code, 
  FileDigit, 
  Search 
} from 'lucide-react';
import { bridgeRequest, useBridgeEvent } from './hooks/useBridge';
import { useAppearance } from './hooks/useAppearance';
import './QuickLookApp.css';

interface FilePreviewData {
  exists: boolean;
  path: string;
  name: string;
  extension: string;
  type: 'markdown' | 'code' | 'image' | 'video' | 'audio' | 'pdf' | 'folder' | 'binary';
  isDirectory: boolean;
  size: number;
  formattedSize: string;
  modified: string;
  content?: string;
  dataUri?: string;
  hexDump?: string;
  folderChildren?: Array<{
    name: string;
    isDirectory: boolean;
    size: number;
    formattedSize: string;
  }>;
}

// 简易 Markdown 解析渲染器（支持标题、粗体、代码块、引用、列表、表格、行内代码）
function renderMarkdownToHtml(markdown: string): string {
  if (!markdown) return '';

  const html = markdown
    // HTML 转义
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    // 代码块 ```lang ... ```
    .replace(/```([a-zA-Z0-9_-]*)\n([\s\S]*?)```/g, (_m, lang, code) => {
      const highlighted = highlightCode(code);
      return `<div class="ql-code-block"><div class="ql-code-header"><span class="ql-code-lang">${lang || 'text'}</span><button class="ql-code-copy-btn" onclick="navigator.clipboard.writeText(decodeURIComponent('${encodeURIComponent(code)}'))">Copy</button></div><pre><code>${highlighted}</code></pre></div>`;
    })
    // 标题 # - ######
    .replace(/^###### (.*$)/gim, '<h6>$1</h6>')
    .replace(/^##### (.*$)/gim, '<h5>$1</h5>')
    .replace(/^#### (.*$)/gim, '<h4>$1</h4>')
    .replace(/^### (.*$)/gim, '<h3>$1</h3>')
    .replace(/^## (.*$)/gim, '<h2>$1</h2>')
    .replace(/^# (.*$)/gim, '<h1>$1</h1>')
    // 引用块 > Note
    .replace(/^> (.*$)/gim, '<blockquote class="ql-blockquote">$1</blockquote>')
    // 水平分割线
    .replace(/^---$/gim, '<hr class="ql-hr" />')
    // 任务列表
    .replace(/^- \[x\] (.*$)/gim, '<div class="ql-task-item"><input type="checkbox" checked disabled /> <span>$1</span></div>')
    .replace(/^- \[ \] (.*$)/gim, '<div class="ql-task-item"><input type="checkbox" disabled /> <span>$1</span></div>')
    // 无序列表
    .replace(/^\* (.*$)/gim, '<li>$1</li>')
    .replace(/^- (.*$)/gim, '<li>$1</li>')
    // 粗体 & 斜体
    .replace(/\*\*\*(.*?)\*\*\*/g, '<strong><em>$1</em></strong>')
    .replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>')
    .replace(/\*(.*?)\*/g, '<em>$1</em>')
    // 行内代码 `code`
    .replace(/`([^`]+)`/g, '<code class="ql-inline-code">$1</code>')
    // 段落换行
    .replace(/\n\n/g, '</p><p>')
    .replace(/\n/g, '<br />');

  return `<div class="ql-markdown-body"><p>${html}</p></div>`;
}

// 简易多语言代码语法高亮
function highlightCode(code: string): string {
  return code
    // 注释
    .replace(/(\/\/[^\n]*|\/\*[\s\S]*?\*\/|#[^\n]*|--[^\n]*)/g, '<span class="tok-comment">$1</span>')
    // 字符串
    .replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`)/g, '<span class="tok-string">$1</span>')
    // 关键字
    .replace(/\b(const|let|var|function|return|if|else|for|while|import|export|from|class|public|private|static|void|int|bool|true|false|null|undefined|struct|template|typename|using|namespace|auto|new|delete|async|await|try|catch|throw|typeof|interface|type)\b/g, '<span class="tok-keyword">$1</span>')
    // 数字
    .replace(/\b([0-9]+(?:\.[0-9]+)?)\b/g, '<span class="tok-number">$1</span>')
    // 函数调用
    .replace(/\b([a-zA-Z_$][a-zA-Z0-9_$]*)(?=\()/g, '<span class="tok-fn">$1</span>');
}

export default function QuickLookApp() {
  const { t } = useTranslation();
  useAppearance();
  const [data, setData] = useState<FilePreviewData | null>(null);
  const [codeWrap, setCodeWrap] = useState(true);
  const [fontSize, setFontSize] = useState(13);
  const [imageScale, setImageScale] = useState(1);
  const contentRef = useRef<HTMLDivElement>(null);

  // 监听原生文件切换事件
  useBridgeEvent('quicklook.fileChanged', (payload: unknown) => {
    setData(payload as FilePreviewData);
    setImageScale(1);
  });

  // 快捷键监听
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape' || e.key === ' ') {
        e.preventDefault();
        bridgeRequest('quicklook.hide');
      } else if (e.key === 'Enter') {
        e.preventDefault();
        bridgeRequest('quicklook.open', { path: data?.path });
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, [data]);

  const fileIcon = useMemo(() => {
    if (!data) return <FileText size={18} />;
    if (data.isDirectory) return <Folder size={18} />;
    switch (data.type) {
      case 'markdown': return <FileText size={18} />;
      case 'image': return <Image size={18} />;
      case 'video': return <Film size={18} />;
      case 'audio': return <Music size={18} />;
      case 'code': return <Code size={18} />;
      case 'pdf': return <FileText size={18} />;
      default: return <File size={18} />;
    }
  }, [data]);

  const typeLabel = useMemo(() => {
    if (!data) return '';
    if (data.isDirectory) return t('quicklook.typeFolder', 'Folder');
    switch (data.type) {
      case 'markdown': return t('quicklook.typeMarkdown', 'Markdown Document');
      case 'image': return t('quicklook.typeImage', '{{ext}} Image', { ext: data.extension.toUpperCase() });
      case 'video': return t('quicklook.typeVideo', '{{ext}} Video', { ext: data.extension.toUpperCase() });
      case 'audio': return t('quicklook.typeAudio', '{{ext}} Audio', { ext: data.extension.toUpperCase() });
      case 'code': return t('quicklook.typeCode', '{{ext}} Source Code', { ext: data.extension.toUpperCase() });
      case 'pdf': return t('quicklook.typePdf', 'PDF Document');
      default: return t('quicklook.typeFile', 'File');
    }
  }, [data, t]);

  const linesCount = useMemo(() => {
    if (!data?.content) return 0;
    return data.content.split('\n').length;
  }, [data]);

  if (!data || !data.exists) {
    return (
      <div className="ql-container ql-empty-container">
        <div className="ql-empty-state">
          <div className="ql-empty-icon"><Search size={32} /></div>
          <div className="ql-empty-title">{t('quicklook.emptyTitle', 'Select a file in File Explorer and press Space')}</div>
          <div className="ql-empty-desc">{t('quicklook.emptyDesc', 'Instant preview for Markdown, code, images, audio, video, PDF and folders')}</div>
        </div>
      </div>
    );
  }

  return (
    <div className="ql-container">
      {/* 顶部标题栏与快捷操作 */}
      <header className="ql-header">
        <div className="ql-file-info">
          <span className="ql-file-icon">{fileIcon}</span>
          <div className="ql-file-meta">
            <div className="ql-file-name-row">
              <span className="ql-file-name" title={data.path}>{data.name}</span>
              <span className="ql-file-badge">{typeLabel}</span>
            </div>
            <div className="ql-file-sub-row">
              <span>{data.formattedSize}</span>
              {linesCount > 0 && <span>· {t('quicklook.linesCount', '{{count}} lines', { count: linesCount })}</span>}
              <span>· {t('quicklook.modifiedAt', 'Modified at {{time}}', { time: data.modified })}</span>
            </div>
          </div>
        </div>

        <div className="ql-actions">
          {data.type === 'code' && (
            <div className="ql-code-controls">
              <button
                type="button"
                className={`ql-btn-sub ${codeWrap ? 'active' : ''}`}
                onClick={() => setCodeWrap(!codeWrap)}
                title={t('quicklook.wrapLines', 'Word Wrap')}
              >
                自动折行
              </button>
              <button
                type="button"
                className="ql-btn-sub"
                onClick={() => setFontSize(Math.max(10, fontSize - 1))}
                title={t('quicklook.decreaseFontSize', 'Decrease Font Size')}
              >
                A-
              </button>
              <button
                type="button"
                className="ql-btn-sub"
                onClick={() => setFontSize(Math.min(24, fontSize + 1))}
                title={t('quicklook.increaseFontSize', 'Increase Font Size')}
              >
                A+
              </button>
            </div>
          )}

          {data.type === 'image' && (
            <div className="ql-code-controls">
              <button
                type="button"
                className="ql-btn-sub"
                onClick={() => setImageScale(Math.max(0.2, imageScale - 0.25))}
                title={t('quicklook.zoomOut', 'Zoom Out')}
              >
                -
              </button>
              <span className="ql-scale-text">{Math.round(imageScale * 100)}%</span>
              <button
                type="button"
                className="ql-btn-sub"
                onClick={() => setImageScale(Math.min(4, imageScale + 0.25))}
                title={t('quicklook.zoomIn', 'Zoom In')}
              >
                +
              </button>
              <button
                type="button"
                className="ql-btn-sub"
                onClick={() => setImageScale(1)}
                title={t('quicklook.resetZoom', 'Reset')}
              >
                100%
              </button>
            </div>
          )}

          <button
            type="button"
            className="ql-btn ql-btn-primary"
            onClick={() => bridgeRequest('quicklook.open', { path: data.path })}
            title={t('quicklook.openFileTitle', 'Open with default application (Enter)')}
          >
            <ExternalLink size={13} style={{ marginRight: 4, verticalAlign: -1 }} />
            打开文件
          </button>
          <button
            type="button"
            className="ql-btn ql-btn-secondary"
            onClick={() => bridgeRequest('quicklook.showInFolder', { path: data.path })}
            title={t('quicklook.locateTitle', 'Locate in File Explorer')}
          >
            <FolderOpen size={13} style={{ marginRight: 4, verticalAlign: -1 }} />
            定位
          </button>
          <button
            type="button"
            className="ql-btn ql-btn-secondary"
            onClick={() => bridgeRequest('quicklook.copyPath', { path: data.path })}
            title={t('quicklook.copyPathTitle', 'Copy absolute file path')}
          >
            <Copy size={13} style={{ marginRight: 4, verticalAlign: -1 }} />
            复制路径
          </button>
          <button
            type="button"
            className="ql-btn ql-btn-close"
            onClick={() => bridgeRequest('quicklook.hide')}
            title={t('quicklook.close', 'Close Preview (Esc / Space)')}
          >
            <X size={14} />
          </button>
        </div>
      </header>

      {/* 主体渲染区 */}
      <main className="ql-main" ref={contentRef}>
        {data.type === 'markdown' && (
          <div
            className="ql-markdown-view"
            dangerouslySetInnerHTML={{ __html: renderMarkdownToHtml(data.content || '') }}
          />
        )}

        {data.type === 'code' && (
          <div className="ql-code-view" style={{ fontSize: `${fontSize}px` }}>
            <div className="ql-line-numbers">
              {(data.content || '').split('\n').map((_, idx) => (
                <div key={idx} className="ql-line-num">{idx + 1}</div>
              ))}
            </div>
            <pre className={`ql-code-content ${codeWrap ? 'wrap' : ''}`}>
              <code dangerouslySetInnerHTML={{ __html: highlightCode(data.content || '') }} />
            </pre>
          </div>
        )}

        {data.type === 'image' && (
          <div className="ql-image-view">
            <div className="ql-image-canvas">
              <img
                src={data.dataUri}
                alt={data.name}
                style={{ transform: `scale(${imageScale})` }}
                className="ql-image-element"
              />
            </div>
          </div>
        )}

        {data.type === 'video' && (
          <div className="ql-media-view">
            <video controls autoPlay src={`https://easytools.local/` + encodeURIComponent(data.name)} className="ql-video-player">
              <p>{t('quicklook.videoNotSupported', 'Your browser does not support this video format')}</p>
            </video>
          </div>
        )}

        {data.type === 'audio' && (
          <div className="ql-media-view ql-audio-container">
            <div className="ql-audio-card">
              <div className="ql-audio-icon"><Music size={36} /></div>
              <div className="ql-audio-title">{data.name}</div>
              <audio controls autoPlay src={`https://easytools.local/` + encodeURIComponent(data.name)} className="ql-audio-player" />
            </div>
          </div>
        )}

        {data.type === 'folder' && (
          <div className="ql-folder-view">
            <div className="ql-folder-header">
              <span>{t('quicklook.folderPreview', 'Folder Content Preview ({{count}} items)', { count: data.folderChildren?.length || 0 })}</span>
            </div>
            <div className="ql-folder-table-container">
              <table className="ql-folder-table">
                <thead>
                  <tr>
                    <th>{t('quicklook.colName', 'Name')}</th>
                    <th>{t('quicklook.colSize', 'Size')}</th>
                    <th>{t('quicklook.colType', 'Type')}</th>
                  </tr>
                </thead>
                <tbody>
                  {data.folderChildren?.map((child, index) => (
                    <tr key={index}>
                      <td className="ql-cell-name">
                        <span className="ql-table-icon" style={{ display: 'inline-flex', alignItems: 'center', marginRight: 4 }}>
                          {child.isDirectory ? <Folder size={14} /> : <File size={14} />}
                        </span>
                        <span>{child.name}</span>
                      </td>
                      <td>{child.formattedSize}</td>
                      <td>{child.isDirectory ? t('quicklook.typeFolder', 'Folder') : t('quicklook.typeFile', 'File')}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        )}

        {data.type === 'binary' && (
          <div className="ql-binary-view">
            <div className="ql-binary-card">
              <div className="ql-binary-icon"><FileDigit size={36} /></div>
              <div className="ql-binary-name">{data.name}</div>
              <div className="ql-binary-meta">{data.formattedSize} · {t('quicklook.typeBinary', 'Binary File')}</div>
              {data.hexDump ? (
                <pre className="ql-hex-dump">{data.hexDump}</pre>
              ) : (
                <p className="ql-binary-hint">{t('quicklook.binaryHint', 'This file is in binary format. Click the button above to open it in an external application.')}</p>
              )}
            </div>
          </div>
        )}
      </main>

      {/* 底部快捷键提示状态栏 */}
      <footer className="ql-footer">
        <div className="ql-shortcut-hint">
          <kbd>Space</kbd> <span>{t('quicklook.spaceClose', 'Close / Toggle')}</span>
          <kbd>Esc</kbd> <span>{t('quicklook.escExit', 'Exit')}</span>
          <kbd>↑</kbd> <kbd>↓</kbd> <span>{t('quicklook.arrowSwitch', 'Switch Selected File')}</span>
          <kbd>Enter</kbd> <span>{t('quicklook.enterOpen', 'Open File')}</span>
        </div>
        <div className="ql-footer-status">
          <span>{t('quicklook.engineStatus', 'EasyTools QuickLook Instant Preview Engine')}</span>
        </div>
      </footer>
    </div>
  );
}

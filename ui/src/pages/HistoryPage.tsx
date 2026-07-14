import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Trash2, ExternalLink, Image as ImageIcon } from 'lucide-react';
import { bridgeRequest } from '../hooks/useBridge';
import './HistoryPage.css';

interface HistoryEntry {
  index: number;
  timestamp: string;
  width: number;
  height: number;
  filePath: string;
  base64?: string; // Loaded on demand
}

export default function HistoryPage() {
  const { t } = useTranslation();
  const [entries, setEntries] = useState<HistoryEntry[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchHistory = async () => {
    setLoading(true);
    try {
      const data = await bridgeRequest<HistoryEntry[]>('history.getAll', {});
      setEntries(data || []);
      
      // Fetch thumbnails
      if (data && data.length > 0) {
        data.forEach(async (entry) => {
          try {
            const thumbData = await bridgeRequest<{base64: string}>('history.getThumbnail', { index: entry.index });
            if (thumbData && thumbData.base64) {
              setEntries(prev => prev.map(e => e.index === entry.index ? { ...e, base64: thumbData.base64 } : e));
            }
          } catch (e) {
            console.error('Failed to fetch thumbnail', e);
          }
        });
      }
    } catch (e) {
      console.error('Failed to fetch history', e);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchHistory();
  }, []);

  const handleClear = async () => {
    if (window.confirm(t('history.confirmClear', 'Are you sure you want to clear all capture history?'))) {
      await bridgeRequest('history.clear', {});
      setEntries([]);
    }
  };

  const openFile = (path: string) => {
    bridgeRequest('system.openFile', { path });
  };

  return (
    <div className="page-container history-page">
      <div className="page-header">
        <div>
          <h2>{t('history.title', 'Capture History')}</h2>
          <p className="page-subtitle">{t('history.subtitle', 'View recent screenshots and recordings')}</p>
        </div>
        <button className="danger-btn" onClick={handleClear} disabled={entries.length === 0}>
          <Trash2 size={16} />
          {t('history.clearAll', 'Clear All')}
        </button>
      </div>

      <div className="history-grid">
        {loading ? (
          <div className="empty-state">{t('common.loading', 'Loading...')}</div>
        ) : entries.length === 0 ? (
          <div className="empty-state">
            <ImageIcon size={48} opacity={0.5} />
            <p>{t('history.empty', 'No capture history found.')}</p>
          </div>
        ) : (
          entries.map(entry => (
            <div key={entry.index} className="history-card" onClick={() => openFile(entry.filePath)}>
              <div className="history-card-image">
                {entry.base64 ? (
                  <img src={`data:image/png;base64,${entry.base64}`} alt="Thumbnail" />
                ) : (
                  <div className="placeholder"><ImageIcon size={24} /></div>
                )}
                <div className="history-card-overlay">
                  <ExternalLink size={24} />
                </div>
              </div>
              <div className="history-card-info">
                <span className="timestamp">{entry.timestamp.replace('T', ' ')}</span>
                <span className="dimensions">{entry.width} × {entry.height}</span>
              </div>
            </div>
          ))
        )}
      </div>
    </div>
  );
}

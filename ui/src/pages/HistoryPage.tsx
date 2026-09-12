import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Trash2, ExternalLink, Image as ImageIcon } from 'lucide-react';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
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

  useEffect(() => {
    let active = true;
    bridgeRequest<HistoryEntry[]>('history.getAll', { includeThumbnails: true })
      .then((data) => {
        if (active) setEntries(Array.isArray(data) ? data : []);
      })
      .catch((error) => {
        console.error('Failed to fetch history', error);
        if (active) toast.error(t('history.loadFailed'));
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => { active = false; };
  }, [t]);

  const handleClear = async () => {
    if (window.confirm(t('history.confirmClear', 'Are you sure you want to clear all capture history?'))) {
      try {
        const result = await bridgeRequest<{ success: boolean; error?: string }>('history.clear', {});
        if (!result.success) throw new Error(result.error || t('history.clearFailed'));
        setEntries([]);
      } catch (error) {
        toast.error(t('history.clearFailed'), { description: String(error) });
      }
    }
  };

  const openEntry = async (index: number) => {
    try {
      const result = await bridgeRequest<{ success: boolean; error?: string }>('history.open', { index });
      if (!result.success) throw new Error(result.error || t('history.openFailed'));
    } catch (error) {
      toast.error(t('history.openFailed'), { description: String(error) });
    }
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
            <button key={entry.index} type="button" className="history-card" onClick={() => openEntry(entry.index)}>
              <div className="history-card-image">
                {entry.base64 ? (
                  <img src={`data:image/png;base64,${entry.base64}`} alt={t('history.thumbnailAlt')} loading="lazy" />
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
            </button>
          ))
        )}
      </div>
    </div>
  );
}

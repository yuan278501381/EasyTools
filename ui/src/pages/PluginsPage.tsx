import { useCallback, useEffect, useState, type FC } from 'react';
import { Camera, FileSearch, Keyboard, MousePointer2, Puzzle, RotateCw, ShieldCheck } from 'lucide-react';
import { toast } from 'sonner';
import { useTranslation } from 'react-i18next';
import { Badge, Toggle } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import './PluginsPage.css';

export interface PluginStatus {
  id: string;
  name: string;
  version: string;
  fileName: string;
  abiVersion: number;
  capabilities: string[];
  permissions: string[];
  enabled: boolean;
  active: boolean;
  restartRequired: boolean;
  state: 'running' | 'disabled' | 'pendingRestart' | 'failed';
  error?: string;
}

interface PluginsPageProps {
  initialPlugins?: PluginStatus[];
}

interface UpdateResult {
  success: boolean;
  restartRequired: boolean;
  error?: string;
}

const ICONS = {
  gesture: MousePointer2,
  capture: Camera,
  search: FileSearch,
  keycast: Keyboard,
} as const;

export const PluginsPage: FC<PluginsPageProps> = ({ initialPlugins = [] }) => {
  const { t } = useTranslation();
  const [plugins, setPlugins] = useState<PluginStatus[]>(initialPlugins);
  const [loading, setLoading] = useState(initialPlugins.length === 0);
  const [savingId, setSavingId] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      const result = await bridgeRequest<PluginStatus[]>('plugins.getAll');
      const next = Array.isArray(result) ? result : [];
      setPlugins(next);
      window.dispatchEvent(new CustomEvent('easytools:plugins-changed', { detail: next }));
    } catch (error) {
      toast.error(t('plugins.loadFailed'), { description: String(error) });
    } finally {
      setLoading(false);
    }
  }, [t]);

  useEffect(() => {
    const frame = requestAnimationFrame(() => { void refresh(); });
    return () => cancelAnimationFrame(frame);
  }, [refresh]);

  const setEnabled = async (plugin: PluginStatus, enabled: boolean) => {
    setSavingId(plugin.id);
    setPlugins((items) => items.map((item) => item.id === plugin.id ? { ...item, enabled } : item));
    try {
      const result = await bridgeRequest<UpdateResult>('plugins.setEnabled', { id: plugin.id, enabled });
      if (!result.success) throw new Error(result.error || t('plugins.saveFailed'));
      await refresh();
      toast.success(enabled ? t('plugins.enabledSaved') : t('plugins.disabledSaved'), {
        description: result.restartRequired ? t('plugins.restartHint') : undefined,
      });
    } catch (error) {
      setPlugins((items) => items.map((item) => item.id === plugin.id ? { ...item, enabled: plugin.enabled } : item));
      toast.error(t('plugins.saveFailed'), { description: String(error) });
    } finally {
      setSavingId(null);
    }
  };

  if (loading) return <div className="plugins-page__loading">{t('common.loading')}</div>;

  const pending = plugins.some((plugin) => plugin.restartRequired);
  return (
    <div className="plugins-page">
      {pending && (
        <div className="plugins-page__restart" role="status">
          <RotateCw size={18} aria-hidden="true" />
          <div><strong>{t('plugins.restartTitle')}</strong><span>{t('plugins.restartHint')}</span></div>
        </div>
      )}
      <div className="plugins-page__grid">
        {plugins.map((plugin) => {
          const Icon = ICONS[plugin.id as keyof typeof ICONS] ?? Puzzle;
          const failed = plugin.state === 'failed';
          const badgeVariant = failed ? 'danger' : plugin.restartRequired ? 'warning' : plugin.active ? 'success' : 'muted';
          return (
            <article className={`plugin-card ${failed ? 'plugin-card--failed' : ''}`} key={plugin.id}>
              <div className="plugin-card__header">
                <span className="plugin-card__icon"><Icon size={22} strokeWidth={2.1} /></span>
                <div className="plugin-card__identity">
                  <div className="plugin-card__title-row">
                    <h2>{t(`plugins.items.${plugin.id}.name`, { defaultValue: plugin.name })}</h2>
                    <Badge text={t(`plugins.state.${plugin.state}`)} variant={badgeVariant} />
                  </div>
                  <span className="plugin-card__version">v{plugin.version || '—'} · {plugin.fileName}</span>
                </div>
              </div>
              <p className="plugin-card__description">
                {t(`plugins.items.${plugin.id}.description`, { defaultValue: plugin.name })}
              </p>
              <div className="plugin-card__manifest" aria-label={t('plugins.capabilities')}>
                <span>{t('plugins.abi', { version: plugin.abiVersion || '—' })}</span>
                {(plugin.capabilities || []).slice(0, 4).map((capability) => (
                  <code key={capability}>{capability}</code>
                ))}
                {(plugin.capabilities || []).length > 4 && (
                  <span>+{plugin.capabilities.length - 4}</span>
                )}
              </div>
              {(plugin.permissions || []).length > 0 && (
                <details className="plugin-card__permissions">
                  <summary><ShieldCheck size={14} aria-hidden="true" />{t('plugins.permissions')}</summary>
                  <div>
                    {plugin.permissions.map((permission) => <code key={permission}>{permission}</code>)}
                  </div>
                </details>
              )}
              {plugin.error && <p className="plugin-card__error" role="alert">{plugin.error}</p>}
              <div className="plugin-card__footer">
                <span>{plugin.active ? t('plugins.resourceActive') : t('plugins.resourceInactive')}</span>
                <Toggle
                  id={`plugin-${plugin.id}`}
                  checked={plugin.enabled}
                  disabled={savingId === plugin.id || (failed && !plugin.enabled)}
                  onChange={(value) => void setEnabled(plugin, value)}
                />
              </div>
            </article>
          );
        })}
      </div>
      {plugins.length === 0 && <div className="plugins-page__empty">{t('plugins.empty')}</div>}
    </div>
  );
};

import { useCallback, useEffect, useMemo, useState, type FC } from 'react';
import {
  Bot,
  Camera,
  CheckCircle2,
  ClipboardList,
  DownloadCloud,
  FileCode2,
  FileSearch,
  FolderDown,
  Keyboard,
  MousePointer2,
  Pipette,
  Puzzle,
  RotateCw,
  Search,
  ShieldCheck,
  Sparkles,
} from 'lucide-react';
import { toast } from 'sonner';
import { useTranslation } from 'react-i18next';
import { Badge, Button, Toggle } from '../components/UIKit';
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

export interface MarketplacePlugin {
  id: string;
  name: string;
  nameEn?: string;
  version: string;
  author: string;
  description: string;
  descriptionEn?: string;
  category: 'ai' | 'utility' | 'productivity';
  abiVersion: number;
  capabilities: string[];
  permissions: string[];
  downloadUrl: string;
  installed?: boolean;
  featured?: boolean;
}

interface PluginsPageProps {
  initialPlugins?: PluginStatus[];
}

interface UpdateResult {
  success: boolean;
  restartRequired: boolean;
  error?: string;
}

const CATEGORY_KEY_MAP: Record<string, string> = {
  all: 'plugins.categoryAll',
  ai: 'plugins.categoryAi',
  utility: 'plugins.categoryUtility',
  productivity: 'plugins.categoryProductivity',
};

const ICONS = {
  gesture: MousePointer2,
  capture: Camera,
  search: FileSearch,
  keycast: Keyboard,
  ai_assistant: Bot,
  color_picker: Pipette,
  clipboard_manager: ClipboardList,
  markdown_preview: FileCode2,
} as const;

export const PluginsPage: FC<PluginsPageProps> = ({ initialPlugins = [] }) => {
  const { t, i18n } = useTranslation();
  const [activeTab, setActiveTab] = useState<'installed' | 'marketplace'>('installed');
  const [plugins, setPlugins] = useState<PluginStatus[]>(initialPlugins);
  const [marketplace, setMarketplace] = useState<MarketplacePlugin[]>([]);
  const [loading, setLoading] = useState(initialPlugins.length === 0);
  const [savingId, setSavingId] = useState<string | null>(null);
  const [installingId, setInstallingId] = useState<string | null>(null);
  const [isRestarting, setIsRestarting] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedCategory, setSelectedCategory] = useState<string>('all');

  const handleRestart = useCallback(async () => {
    if (isRestarting) return;
    setIsRestarting(true);
    toast.loading(t('plugins.restarting'));
    try {
      await bridgeRequest('app.restart');
    } catch {
      setIsRestarting(false);
      toast.error(t('plugins.restartFailed'));
    }
  }, [isRestarting, t]);

  const refresh = useCallback(async () => {
    try {
      const [installedResult, marketResult] = await Promise.allSettled([
        bridgeRequest<PluginStatus[]>('plugins.getAll'),
        bridgeRequest<MarketplacePlugin[]>('plugins.getMarketplace'),
      ]);

      if (installedResult.status === 'fulfilled' && Array.isArray(installedResult.value)) {
        setPlugins(installedResult.value);
        window.dispatchEvent(new CustomEvent('easytools:plugins-changed', { detail: installedResult.value }));
      }
      if (marketResult.status === 'fulfilled' && Array.isArray(marketResult.value)) {
        setMarketplace(marketResult.value);
      }
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
        action: result.restartRequired ? {
          label: t('plugins.restartNow'),
          onClick: () => { void handleRestart(); },
        } : undefined,
      });
    } catch (error) {
      setPlugins((items) => items.map((item) => item.id === plugin.id ? { ...item, enabled: plugin.enabled } : item));
      toast.error(t('plugins.saveFailed'), { description: String(error) });
    } finally {
      setSavingId(null);
    }
  };

  const installMarketPlugin = async (item: MarketplacePlugin) => {
    setInstallingId(item.id);
    try {
      const result = await bridgeRequest<{ success: boolean; restartRequired?: boolean }>('plugins.install', { id: item.id });
      if (result?.success) {
        toast.success(t('plugins.installSuccess'), {
          action: {
            label: t('plugins.restartNow'),
            onClick: () => { void handleRestart(); },
          },
        });
        setMarketplace((prev) => prev.map((p) => p.id === item.id ? { ...p, installed: true } : p));
        setActiveTab('installed');
        await refresh();
      }
    } catch (error) {
      toast.error(String(error));
    } finally {
      setInstallingId(null);
    }
  };

  const handleInstallLocal = () => {
    toast.info(t('plugins.installSuccess'), {
      description: t('plugins.restartHint'),
      action: {
        label: t('plugins.restartNow'),
        onClick: () => { void handleRestart(); },
      },
    });
  };

  const filteredMarketplace = useMemo(() => {
    const isEn = i18n.language.startsWith('en');
    return marketplace.filter((item) => {
      const matchCat = selectedCategory === 'all' || item.category === selectedCategory;
      const query = searchQuery.trim().toLowerCase();
      if (!query) return matchCat;
      const title = (isEn && item.nameEn ? item.nameEn : item.name).toLowerCase();
      const desc = (isEn && item.descriptionEn ? item.descriptionEn : item.description).toLowerCase();
      const caps = (item.capabilities || []).join(' ').toLowerCase();
      return matchCat && (title.includes(query) || desc.includes(query) || caps.includes(query));
    });
  }, [marketplace, selectedCategory, searchQuery, i18n.language]);

  if (loading) return <div className="plugins-page__loading">{t('common.loading')}</div>;

  const pending = plugins.some((plugin) => plugin.restartRequired);

  return (
    <div className="plugins-page">
      {/* 顶部 Tab 切换 */}
      <div className="plugins-tabs-header">
        <div className="plugins-tabs-nav" role="tablist">
          <button
            type="button"
            role="tab"
            aria-selected={activeTab === 'installed'}
            className={`plugins-tab-btn ${activeTab === 'installed' ? 'active' : ''}`}
            onClick={() => setActiveTab('installed')}
          >
            <Puzzle size={16} />
            {t('plugins.tabInstalled')}
            <span className="plugins-tab-badge">{plugins.length}</span>
          </button>
          <button
            type="button"
            role="tab"
            aria-selected={activeTab === 'marketplace'}
            className={`plugins-tab-btn ${activeTab === 'marketplace' ? 'active' : ''}`}
            onClick={() => setActiveTab('marketplace')}
          >
            <Sparkles size={16} />
            {t('plugins.tabMarketplace')}
            <span className="plugins-tab-badge highlight">{marketplace.length}</span>
          </button>
        </div>

        {activeTab === 'marketplace' && (
          <Button variant="secondary" size="sm" onClick={handleInstallLocal} className="plugins-import-btn">
            <FolderDown size={15} />
            {t('plugins.installLocal')}
          </Button>
        )}
      </div>

      {/* 待重启提示条（支持一键热重启） */}
      {pending && (
        <div className="plugins-page__restart" role="status">
          <RotateCw size={20} className={isRestarting ? 'plugins-spin' : ''} aria-hidden="true" />
          <div className="plugins-page__restart-content">
            <strong>{t('plugins.restartTitle')}</strong>
            <span>{t('plugins.restartHint')}</span>
          </div>
          <Button
            variant="primary"
            size="sm"
            onClick={() => { void handleRestart(); }}
            disabled={isRestarting}
            className="plugins-restart-now-btn"
          >
            <RotateCw size={14} className={isRestarting ? 'plugins-spin' : ''} />
            {t('plugins.restartNow')}
          </Button>
        </div>
      )}

      {/* 选项卡 1: 已安装模块 */}
      {activeTab === 'installed' && (
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
      )}

      {/* 选项卡 2: 插件扩展市场 */}
      {activeTab === 'marketplace' && (
        <div className="plugins-marketplace">
          {/* 筛选与搜索工具条 */}
          <div className="plugins-marketplace__toolbar">
            <div className="plugins-marketplace__categories">
              {[
                { key: 'all', label: t('plugins.categoryAll') },
                { key: 'ai', label: t('plugins.categoryAi') },
                { key: 'utility', label: t('plugins.categoryUtility') },
                { key: 'productivity', label: t('plugins.categoryProductivity') },
              ].map((cat) => (
                <button
                  key={cat.key}
                  type="button"
                  className={`plugins-category-pill ${selectedCategory === cat.key ? 'active' : ''}`}
                  onClick={() => setSelectedCategory(cat.key)}
                >
                  {cat.label}
                </button>
              ))}
            </div>

            <div className="plugins-marketplace__search">
              <Search size={16} />
              <input
                type="text"
                placeholder={t('plugins.searchPlaceholder')}
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
              />
            </div>
          </div>

          {/* 市场扩展网格 */}
          <div className="plugins-page__grid">
            {filteredMarketplace.map((item) => {
              const Icon = ICONS[item.id as keyof typeof ICONS] ?? Puzzle;
              const isEn = i18n.language.startsWith('en');
              const title = isEn && item.nameEn ? item.nameEn : item.name;
              const desc = isEn && item.descriptionEn ? item.descriptionEn : item.description;
              const isInstalling = installingId === item.id;
              const isInstalled = item.installed;

              return (
                <article className="plugin-card marketplace-card" key={item.id}>
                  <div className="plugin-card__header">
                    <span className="plugin-card__icon marketplace-icon"><Icon size={22} strokeWidth={2.1} /></span>
                    <div className="plugin-card__identity">
                      <div className="plugin-card__title-row">
                        <h2>{title}</h2>
                        <span className="marketplace-category-tag">{t((CATEGORY_KEY_MAP[item.category] ?? 'plugins.categoryAll') as never)}</span>
                      </div>
                      <span className="plugin-card__version">v{item.version} · {t('plugins.author')}: {item.author}</span>
                    </div>
                  </div>

                  <p className="plugin-card__description">{desc}</p>

                  <div className="plugin-card__manifest">
                    <span>{t('plugins.abi', { version: item.abiVersion })}</span>
                    {(item.capabilities || []).map((cap) => (
                      <code key={cap}>{cap}</code>
                    ))}
                  </div>

                  <div className="plugin-card__footer">
                    <div className="marketplace-permissions">
                      <ShieldCheck size={14} />
                      {(item.permissions || []).join(' · ')}
                    </div>
                    <Button
                      variant={isInstalled ? 'secondary' : 'primary'}
                      size="sm"
                      disabled={isInstalling || isInstalled}
                      onClick={() => void installMarketPlugin(item)}
                      className="marketplace-action-btn"
                    >
                      {isInstalled ? (
                        <>
                          <CheckCircle2 size={14} />
                          {t('plugins.installed')}
                        </>
                      ) : isInstalling ? (
                        <>
                          <RotateCw size={14} className="plugins-spin" />
                          {t('plugins.installing')}
                        </>
                      ) : (
                        <>
                          <DownloadCloud size={14} />
                          {t('plugins.install')}
                        </>
                      )}
                    </Button>
                  </div>
                </article>
              );
            })}
          </div>

          {filteredMarketplace.length === 0 && (
            <div className="plugins-page__empty">{t('plugins.empty')}</div>
          )}
        </div>
      )}

      {activeTab === 'installed' && plugins.length === 0 && (
        <div className="plugins-page__empty">{t('plugins.empty')}</div>
      )}
    </div>
  );
};

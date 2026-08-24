/* ─────────────────────────────────────────────────────────────────────────────
 * DialogEnhancerPage.tsx — 文件对话框智能助手设置页
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select, Button } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import { useTranslation } from 'react-i18next';
import {
  FolderSymlink,
  Sparkles,
  History,
  Star,
  ShieldAlert,
  Trash2,
  Plus,
  FolderHeart,
  Pin,
  PinOff
} from 'lucide-react';
import './DialogEnhancerPage.css';

interface DialogConfig {
  enabled: boolean;
  perAppMemory: boolean;
  quickSwitch: boolean;
  ribbonEnabled: boolean;
  ribbonPosition: string;
}

interface AppMemoryItem {
  processName: string;
  lastPath: string;
  fixedWorkspace?: string;
  isFixed?: boolean;
  lastUsedTimestamp: number;
}

export const DialogEnhancerPage: FC = () => {
  const { t } = useTranslation();
  const [config, setConfig] = useState<DialogConfig>({
    enabled: true,
    perAppMemory: true,
    quickSwitch: true,
    ribbonEnabled: true,
    ribbonPosition: 'top-right',
  });
  const [favorites, setFavorites] = useState<string[]>([]);
  const [appMemories, setAppMemories] = useState<AppMemoryItem[]>([]);
  const [blacklist, setBlacklist] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);

  const [newFavoriteInput, setNewFavoriteInput] = useState('');
  const [newBlacklistInput, setNewBlacklistInput] = useState('');

  useEffect(() => {
    let cancelled = false;
    Promise.all([
      bridgeRequest<DialogConfig>('dialog.getConfig').catch(() => ({
        enabled: true, perAppMemory: true, quickSwitch: true, ribbonEnabled: true, ribbonPosition: 'top-right'
      })),
      bridgeRequest<string[]>('dialog.getFavorites').catch(() => []),
      bridgeRequest<AppMemoryItem[]>('dialog.getAppMemories').catch(() => []),
      bridgeRequest<string[]>('dialog.getBlacklist').catch(() => []),
    ]).then(([cfg, favs, mems, bl]) => {
      if (cancelled) return;
      if (cfg) setConfig(cfg);
      if (Array.isArray(favs)) setFavorites(favs);
      if (Array.isArray(mems)) setAppMemories(mems);
      if (Array.isArray(bl)) setBlacklist(bl);
      setLoading(false);
    }).catch(() => {
      if (!cancelled) setLoading(false);
    });

    return () => {
      cancelled = true;
    };
  }, []);

  const updateConfig = async (patch: Partial<DialogConfig>) => {
    const next = { ...config, ...patch };
    setConfig(next);
    try {
      await bridgeRequest('dialog.updateConfig', patch as Record<string, unknown>);
      toast.success(t('common.save'));
    } catch {
      toast.error('保存失败');
    }
  };

  const handleAddFavorite = async () => {
    const path = newFavoriteInput.trim();
    if (!path) return;
    try {
      await bridgeRequest('dialog.addFavorite', { path });
      setFavorites((prev) => [...prev.filter((p) => p !== path), path]);
      setNewFavoriteInput('');
      toast.success(t('common.save'));
    } catch {
      toast.error('添加失败');
    }
  };

  const handleRemoveFavorite = async (path: string) => {
    try {
      await bridgeRequest('dialog.removeFavorite', { path });
      setFavorites((prev) => prev.filter((p) => p !== path));
      toast.success(t('common.delete'));
    } catch {
      toast.error('删除失败');
    }
  };

  const handleTogglePinWorkspace = async (item: AppMemoryItem) => {
    const nextFixed = !item.isFixed;
    const targetPath = item.fixedWorkspace || item.lastPath;
    try {
      await bridgeRequest('dialog.setAppFixedWorkspace', {
        processName: item.processName,
        workspacePath: targetPath,
        isFixed: nextFixed
      });
      setAppMemories((prev) => prev.map((m) =>
        m.processName === item.processName ? { ...m, isFixed: nextFixed, fixedWorkspace: targetPath } : m
      ));
      toast.success(nextFixed ? '已固定为默认母工作区' : '已取消固定');
    } catch {
      toast.error('操作失败');
    }
  };

  const handleRemoveAppMemory = async (proc: string) => {
    try {
      await bridgeRequest('dialog.removeAppMemory', { processName: proc });
      setAppMemories((prev) => prev.filter((m) => m.processName !== proc));
      toast.success(t('common.delete'));
    } catch {
      toast.error('删除失败');
    }
  };

  const handleClearAppMemories = async () => {
    try {
      await bridgeRequest('dialog.clearAppMemories');
      setAppMemories([]);
      toast.success(t('common.delete'));
    } catch {
      toast.error('清空失败');
    }
  };

  const handleAddBlacklist = async () => {
    const proc = newBlacklistInput.trim();
    if (!proc) return;
    try {
      const next = [...blacklist.filter((p) => p.toLowerCase() !== proc.toLowerCase()), proc];
      await bridgeRequest('dialog.setBlacklist', { blacklist: next });
      setBlacklist(next);
      setNewBlacklistInput('');
      toast.success(t('common.save'));
    } catch {
      toast.error('添加失败');
    }
  };

  const handleRemoveBlacklist = async (proc: string) => {
    try {
      const next = blacklist.filter((p) => p !== proc);
      await bridgeRequest('dialog.setBlacklist', { blacklist: next });
      setBlacklist(next);
      toast.success(t('common.delete'));
    } catch {
      toast.error('删除失败');
    }
  };

  const formatTime = (timestamp: number) => {
    if (!timestamp) return '-';
    const d = new Date(timestamp * 1000);
    return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  };

  if (loading) {
    return <div className="dialog-page-loading">{t('common.loading')}</div>;
  }

  return (
    <div className="dialog-page">
      {/* 1. 基础开关 */}
      <SettingGroup title={t('dialog.generalGroup')} icon={<FolderSymlink size={18} />}>
        <Card>
          <Toggle
            id="dialog-enable"
            label={t('dialog.enable')}
            description={t('dialog.enableDesc')}
            checked={config.enabled}
            onChange={(v) => updateConfig({ enabled: v })}
          />
          <Toggle
            id="dialog-per-app"
            label={t('dialog.perAppMemory')}
            description={t('dialog.perAppMemoryDesc')}
            checked={config.perAppMemory}
            disabled={!config.enabled}
            onChange={(v) => updateConfig({ perAppMemory: v })}
          />
        </Card>
      </SettingGroup>

      {/* 2. 文件对话框顶部快捷工具条：总开关 + 从属选项 */}
      <SettingGroup title={t('dialog.ribbonGroup')} icon={<Sparkles size={18} />}>
        <Card>
          <Toggle
            id="dialog-ribbon-enable"
            label={t('dialog.ribbonEnabled')}
            description={t('dialog.ribbonEnabledDesc')}
            checked={config.ribbonEnabled}
            disabled={!config.enabled}
            onChange={(v) => updateConfig({ ribbonEnabled: v })}
          />
          <div
            className={`dialog-ribbon-options ${!config.enabled || !config.ribbonEnabled ? 'dialog-ribbon-options--disabled' : ''}`}
            aria-label={t('dialog.ribbonOptions')}
          >
            <div className="dialog-ribbon-options__title">{t('dialog.ribbonOptions')}</div>
            <Toggle
              id="dialog-quick-switch"
              label={t('dialog.quickSwitch')}
              description={t('dialog.quickSwitchDesc')}
              checked={config.quickSwitch}
              disabled={!config.enabled || !config.ribbonEnabled}
              onChange={(v) => updateConfig({ quickSwitch: v })}
            />
            <SettingRow label={t('dialog.ribbonPosition')}>
              <Select
                id="dialog-ribbon-pos"
                value={config.ribbonPosition}
                disabled={!config.enabled || !config.ribbonEnabled}
                options={[
                  { value: 'top-right', label: t('dialog.ribbonPositionTopRight') },
                  { value: 'top-center', label: t('dialog.ribbonPositionTopCenter') },
                ]}
                onChange={(v) => updateConfig({ ribbonPosition: v })}
              />
            </SettingRow>
          </div>
        </Card>
      </SettingGroup>

      {/* 3. 常用工作区收藏 */}
      <SettingGroup title={t('dialog.favoritesGroup')} icon={<Star size={18} />}>
        <Card subtitle={t('dialog.favoritesDesc')}>
          <div className="dialog-add-row">
            <input
              type="text"
              className="dialog-input"
              placeholder={t('dialog.enterPath')}
              value={newFavoriteInput}
              onChange={(e) => setNewFavoriteInput(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && handleAddFavorite()}
            />
            <Button variant="primary" onClick={handleAddFavorite}>
              <Plus size={16} /> {t('common.add')}
            </Button>
          </div>

          <div className="dialog-list">
            {favorites.length === 0 ? (
              <div className="dialog-empty">{t('dialog.noFavorites')}</div>
            ) : (
              favorites.map((fav) => (
                <div key={fav} className="dialog-list-item">
                  <div className="dialog-item-icon">
                    <FolderHeart size={16} />
                  </div>
                  <div className="dialog-item-text" title={fav}>
                    {fav}
                  </div>
                  <Button
                    variant="ghost"
                    size="sm"
                    className="dialog-btn-danger"
                    onClick={() => handleRemoveFavorite(fav)}
                  >
                    <Trash2 size={14} />
                  </Button>
                </div>
              ))
            )}
          </div>
        </Card>
      </SettingGroup>

      {/* 4. 已记忆的应用路径表 */}
      <SettingGroup title={t('dialog.appMemoriesGroup')} icon={<History size={18} />}>
        <Card subtitle={t('dialog.appMemoriesDesc')}>
          {appMemories.length > 0 && (
            <div style={{ marginBottom: '1rem', display: 'flex', justifyContent: 'flex-end' }}>
              <Button variant="ghost" size="sm" onClick={handleClearAppMemories}>
                <Trash2 size={14} /> {t('dialog.clearAllMemories')}
              </Button>
            </div>
          )}
          <div className="dialog-memories-table">
            {appMemories.length === 0 ? (
              <div className="dialog-empty">{t('dialog.noMemories')}</div>
            ) : (
              appMemories.map((item) => (
                <div key={item.processName} className="dialog-memory-row">
                  <div className="dialog-memory-proc">
                    <span className="dialog-proc-badge">{item.processName}</span>
                    {item.isFixed && (
                      <span className="dialog-fixed-badge">{t('dialog.fixedBadge')}</span>
                    )}
                  </div>
                  <div className="dialog-memory-path" title={item.isFixed ? item.fixedWorkspace : item.lastPath}>
                    {item.isFixed ? item.fixedWorkspace : item.lastPath}
                  </div>
                  <div className="dialog-memory-time">{formatTime(item.lastUsedTimestamp)}</div>
                  <div className="dialog-memory-action" style={{ display: 'flex', gap: '0.25rem' }}>
                    <Button
                      variant="ghost"
                      size="sm"
                      title={item.isFixed ? t('dialog.unpinWorkspace') : t('dialog.pinWorkspace')}
                      onClick={() => handleTogglePinWorkspace(item)}
                    >
                      {item.isFixed ? <PinOff size={14} /> : <Pin size={14} />}
                    </Button>
                    <Button
                      variant="ghost"
                      size="sm"
                      className="dialog-btn-danger"
                      onClick={() => handleRemoveAppMemory(item.processName)}
                    >
                      <Trash2 size={14} />
                    </Button>
                  </div>
                </div>
              ))
            )}
          </div>
        </Card>
      </SettingGroup>

      {/* 5. 排除黑名单 */}
      <SettingGroup title={t('dialog.blacklistGroup')} icon={<ShieldAlert size={18} />}>
        <Card subtitle={t('dialog.blacklistDesc')}>
          <div className="dialog-add-row">
            <input
              type="text"
              className="dialog-input"
              placeholder={t('dialog.enterProcessName')}
              value={newBlacklistInput}
              onChange={(e) => setNewBlacklistInput(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && handleAddBlacklist()}
            />
            <Button variant="primary" onClick={handleAddBlacklist}>
              <Plus size={16} /> {t('common.add')}
            </Button>
          </div>

          <div className="dialog-chips">
            {blacklist.map((proc) => (
              <div key={proc} className="dialog-chip">
                <span>{proc}</span>
                <button
                  type="button"
                  className="dialog-chip-remove"
                  onClick={() => handleRemoveBlacklist(proc)}
                >
                  ×
                </button>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>
    </div>
  );
};

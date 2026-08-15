/* ─────────────────────────────────────────────────────────────────────────────
 * GesturePage — 鼠标手势设置页
 *
 * 功能:
 *   - 手势全局开关 / 触发按钮 / 轨迹显示 / 全屏游戏免打扰
 *   - 手势映射表的增 / 删 / 改 (经 gesture.updateProfile 持久化)
 *   - 动作类型: 快捷键 / Lua 脚本 / 内置命令 / 运行程序
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Badge, Select, Button, TextInput } from '../components/UIKit';
import { GestureEditorModal } from '../components/GestureEditorModal';
import { ScopeRulesManager } from '../components/ScopeRulesManager';
import { GestureGuide } from '../components/GestureGuide';
import {
  codeToArrows,
  ACTION_TYPE_KEYS,
  BUILTIN_COMMAND_KEYS,
  type GestureMapping,
} from '../components/gestureModel';
import { bridgeRequest, useBridgeEvent } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { MousePointer2, Hand, Edit3, Trash2, Target, Compass, ArrowUp, ArrowDown } from 'lucide-react';
import './GesturePage.css';

interface RadialMenuItem {
  label: string;
  command: string;
}

interface GestureState {
  enabled: boolean;
  paused: boolean;
  triggerButton: string;
  trailVisible: boolean;
  autoBypassFullscreen?: boolean;
}

interface OperationResult {
  success: boolean;
  error?: string;
}

interface HotkeyEntry {
  name: string;
  shortcut: string;
}

const PROFILE_NAME = 'default';

export const GesturePage: FC = () => {
  const [enabled, setEnabled] = useState(true);
  const [trailVisible, setTrailVisible] = useState(true);
  const [autoBypassFullscreen, setAutoBypassFullscreen] = useState(false);
  const [triggerButton, setTriggerButton] = useState('right');
  const [mappings, setMappings] = useState<GestureMapping[]>([]);
  const [profileNames, setProfileNames] = useState<string[]>([PROFILE_NAME]);
  const [loading, setLoading] = useState(true);
  const [radialItems, setRadialItems] = useState<RadialMenuItem[]>([]);
  const [radialDirty, setRadialDirty] = useState(false);
  const [radialSaving, setRadialSaving] = useState(false);
  const [pauseHotkey, setPauseHotkey] = useState('Ctrl+Alt+Shift+W');
  const { t } = useTranslation();

  const actionDetail = (action: GestureMapping['action']): string => {
    switch (action.type) {
      case 0: return action.keyStroke ?? '';
      case 1: return '';
      case 2: {
        const key = BUILTIN_COMMAND_KEYS[action.builtinCmd ?? 0];
        return key ? t(key) : '';
      }
      case 3: return action.programPath ?? '';
      default: return '';
    }
  };

  const [editorOpen, setEditorOpen] = useState(false);
  const [editing, setEditing] = useState<GestureMapping | null>(null);

  useBridgeEvent('gesture.stateChanged', (data) => {
    const state = data as Partial<GestureState>;
    if (typeof state.enabled === 'boolean') setEnabled(state.enabled);
  });

  useEffect(() => {
    async function loadData() {
      try {
        const [state, profiles, radialRes, hotkeys] = await Promise.all([
          bridgeRequest<GestureState>('gesture.getState'),
          bridgeRequest<Array<{ name: string; mappings: GestureMapping[] }>>('gesture.getProfiles'),
          bridgeRequest<{ items: RadialMenuItem[] }>('radialmenu.getItems'),
          bridgeRequest<HotkeyEntry[]>('hotkey.getAll'),
        ]);
        setEnabled(state.enabled);
        setTriggerButton(state.triggerButton ?? 'right');
        setTrailVisible(state.trailVisible ?? true);
        setAutoBypassFullscreen(state.autoBypassFullscreen ?? false);
        if (profiles?.length) setProfileNames(profiles.map((p) => p.name));
        const defaultProfile = profiles?.find((p) => p.name === PROFILE_NAME);
        if (defaultProfile) setMappings(defaultProfile.mappings);
        if (radialRes?.items) setRadialItems(radialRes.items);
        const pauseBinding = hotkeys.find((entry) => entry.name === 'Pause Gestures');
        if (pauseBinding) setPauseHotkey(pauseBinding.shortcut);
      } catch (err) {
        console.error('Failed to load gesture config:', err);
        toast.error(t('gesture.loadFailed'));
      } finally {
        setLoading(false);
      }
    }
    loadData();
  }, [t]);

  // ── 持久化整张映射表 ────────────────────────────────────────────────────────
  const persist = useCallback(async (next: GestureMapping[]) => {
    const prev = mappings;
    setMappings(next);
    try {
      const result = await bridgeRequest<OperationResult>('gesture.updateProfile', {
        name: PROFILE_NAME,
        mappings: next,
      });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
    } catch (err) {
      console.error('Failed to save gesture profile:', err);
      setMappings(prev); // 回滚
      toast.error(t('gesture.saveFailed'), { description: String(err) });
    }
  }, [mappings, t]);

  const handleToggleEnabled = async (checked: boolean) => {
    setEnabled(checked);
    try {
      const result = await bridgeRequest<OperationResult>('gesture.setPaused', { paused: !checked });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
    } catch (err) {
      setEnabled(!checked);
      console.error('Failed to update gesture enabled state:', err);
      toast.error(t('gesture.saveFailed'), { description: String(err) });
    }
  };

  const handleToggleTrail = async (checked: boolean) => {
    setTrailVisible(checked);
    try {
      const result = await bridgeRequest<OperationResult>('gesture.updateSettings', { trailVisible: checked });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
    } catch (err) {
      setTrailVisible(!checked);
      console.error('Failed to update gesture trail state:', err);
      toast.error(t('gesture.saveFailed'), { description: String(err) });
    }
  };

  const handleToggleAutoBypass = async (checked: boolean) => {
    setAutoBypassFullscreen(checked);
    try {
      const result = await bridgeRequest<OperationResult>('gesture.updateSettings', { autoBypassFullscreen: checked });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
    } catch (err) {
      setAutoBypassFullscreen(!checked);
      console.error('Failed to update gesture autoBypassFullscreen state:', err);
      toast.error(t('gesture.saveFailed'), { description: String(err) });
    }
  };

  const handleTriggerChange = async (value: string) => {
    const previous = triggerButton;
    setTriggerButton(value);
    try {
      const result = await bridgeRequest<OperationResult>('gesture.updateSettings', { triggerButton: value });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
    } catch (err) {
      setTriggerButton(previous);
      console.error('Failed to update gesture trigger button:', err);
      toast.error(t('gesture.saveFailed'), { description: String(err) });
    }
  };

  const radialCommandIndex = (command: string): number => {
    const legacy: Record<string, number> = { capture: 10, search: 16, lock: 8, pin: 18, record: 11 };
    if (command in legacy) return legacy[command];
    const parsed = Number(command);
    return Number.isInteger(parsed) && parsed >= 0 && parsed < BUILTIN_COMMAND_KEYS.length ? parsed : 10;
  };

  const updateRadialItem = (index: number, patch: Partial<RadialMenuItem>) => {
    setRadialItems(items => items.map((item, itemIndex) =>
      itemIndex === index ? { ...item, ...patch } : item));
    setRadialDirty(true);
  };

  const moveRadialItem = (index: number, direction: -1 | 1) => {
    const target = index + direction;
    if (target < 0 || target >= radialItems.length) return;
    setRadialItems(items => {
      const next = [...items];
      [next[index], next[target]] = [next[target], next[index]];
      return next;
    });
    setRadialDirty(true);
  };

  const removeRadialItem = (index: number) => {
    setRadialItems(items => items.filter((_, itemIndex) => itemIndex !== index));
    setRadialDirty(true);
  };

  const addRadialItem = () => {
    if (radialItems.length >= 8) return;
    setRadialItems(items => [...items, {
      label: t('gesture.radialDefaultLabel', { count: items.length + 1 }),
      command: '10',
    }]);
    setRadialDirty(true);
  };

  const saveRadialItems = async () => {
    const normalized = radialItems.map(item => ({ ...item, label: item.label.trim() }));
    if (normalized.some(item => !item.label)) {
      toast.error(t('gesture.radialLabelRequired'));
      return;
    }
    setRadialSaving(true);
    try {
      const result = await bridgeRequest<OperationResult>('radialmenu.updateItems', { items: normalized });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
      setRadialItems(normalized);
      setRadialDirty(false);
      toast.success(t('gesture.radialSaved'));
    } catch (error) {
      toast.error(t('gesture.saveFailed'), { description: String(error) });
    } finally {
      setRadialSaving(false);
    }
  };

  // ── CRUD ────────────────────────────────────────────────────────────────────
  const openAdd = () => { setEditing(null); setEditorOpen(true); };
  const openEdit = (m: GestureMapping) => { setEditing(m); setEditorOpen(true); };

  const handleSaveMapping = (saved: GestureMapping) => {
    const idx = mappings.findIndex((m) => m.gestureCode === (editing?.gestureCode ?? saved.gestureCode));
    const next = [...mappings];
    if (editing && idx >= 0) next[idx] = saved;            // 编辑
    else {
      const dup = next.findIndex((m) => m.gestureCode === saved.gestureCode);
      if (dup >= 0) next[dup] = saved;                     // 同码覆盖
      else next.push(saved);                               // 新增
    }
    persist(next);
    setEditorOpen(false);
  };

  const handleDelete = (m: GestureMapping) => {
    if (!window.confirm(t('gesture.deleteConfirm', { name: m.action.name, code: m.gestureCode }))) return;
    persist(mappings.filter((x) => x.gestureCode !== m.gestureCode));
  };

  if (loading) {
    return (
      <div className="page-loading">
        <div className="page-loading__spinner" />
        <span>{t('common.loading')}</span>
      </div>
    );
  }

  return (
    <div className="gesture-page" style={{ animation: 'fadeIn 0.3s ease', paddingBottom: '2rem' }}>
      {/* ── 全局开关 ──────────────────────────────────────────────── */}
      <SettingGroup title={t('gesture.title')} icon={<MousePointer2 size={20} strokeWidth={2.5} />}>
        <Card>
          <div className={`gesture-status ${enabled ? 'gesture-status--active' : 'gesture-status--paused'}`}>
            <span className="gesture-status__dot" />
            <span className="gesture-status__text">{enabled ? t('gesture.statusRunning') : t('gesture.statusPaused')}</span>
            <kbd className="gesture-status__hotkey">{pauseHotkey}</kbd>
          </div>
          <Toggle
            id="gesture-enabled"
            label={t('gesture.enabled')}
            description={t('gesture.enabledDesc')}
            checked={enabled}
            onChange={handleToggleEnabled}
          />
          <Toggle
            id="gesture-trail"
            label={t('gesture.showTrail')}
            description={t('gesture.showTrailDesc')}
            checked={trailVisible}
            onChange={handleToggleTrail}
          />
          <Toggle
            id="gesture-bypass-fullscreen"
            label={t('gesture.autoBypassFullscreen')}
            description={t('gesture.autoBypassFullscreenDesc')}
            checked={autoBypassFullscreen}
            onChange={handleToggleAutoBypass}
          />
          <SettingRow label={t('gesture.triggerButton')} description={t('gesture.triggerButtonDesc')}>
            <Select
              id="gesture-trigger"
              value={triggerButton}
              onChange={handleTriggerChange}
              options={[
                { value: 'right', label: t('gesture.btnRight') },
                { value: 'middle', label: t('gesture.btnMiddle') },
              ]}
            />
          </SettingRow>

          <GestureGuide />
        </Card>
      </SettingGroup>

      {/* ── 手势映射表 ────────────────────────────────────────────── */}
      <SettingGroup title={t('gesture.mapping')} icon={<Hand size={20} strokeWidth={2.5} />}>
        <Card>
          <div className="gesture-toolbar">
            <span className="gesture-toolbar__count">{t('gesture.mappingCount', { count: mappings.length })}</span>
            <Button size="sm" variant="primary" onClick={openAdd}>{t('gesture.addMapping')}</Button>
          </div>

          <div className="gesture-table">
            <div className="gesture-table__header">
              <span className="gesture-table__col gesture-table__col--arrow">{t('gesture.colGesture')}</span>
              <span className="gesture-table__col gesture-table__col--code">{t('gesture.colCode')}</span>
              <span className="gesture-table__col gesture-table__col--action">{t('gesture.colAction')}</span>
              <span className="gesture-table__col gesture-table__col--type">{t('gesture.colType')}</span>
              <span className="gesture-table__col gesture-table__col--key">{t('gesture.colDetail')}</span>
              <span className="gesture-table__col gesture-table__col--actions" />
            </div>

            {mappings.length === 0 && (
              <div className="gesture-empty">{t('gesture.emptyMapping')}</div>
            )}

            {mappings.map((m, i) => (
              <div key={m.gestureCode} className="gesture-table__row" style={{ animationDelay: `${i * 30}ms` }}>
                <span className="gesture-table__col gesture-table__col--arrow">
                  <span className="gesture-arrow">{codeToArrows(m.gestureCode) || m.gestureCode}</span>
                </span>
                <span className="gesture-table__col gesture-table__col--code">
                  <code>{m.gestureCode}</code>
                </span>
                <span className="gesture-table__col gesture-table__col--action">{m.action.name}</span>
                <span className="gesture-table__col gesture-table__col--type">
                  <Badge
                    text={ACTION_TYPE_KEYS[m.action.type] ? t(ACTION_TYPE_KEYS[m.action.type]) : t('common.unknown')}
                    variant={m.action.type === 0 ? 'primary' : m.action.type === 2 ? 'success' : 'muted'}
                  />
                </span>
                <span className="gesture-table__col gesture-table__col--key">
                  {actionDetail(m.action) && <kbd className="gesture-kbd">{actionDetail(m.action)}</kbd>}
                </span>
                <span className="gesture-table__col gesture-table__col--actions">
                  <div style={{ display: 'flex', gap: '4px' }}>
                    <button className="gesture-icon-btn" title={t('common.edit')} onClick={() => openEdit(m)}><Edit3 size={16} /></button>
                    <button 
                      className="gesture-icon-btn" 
                      title={t('common.delete')} 
                      onClick={() => handleDelete(m)}
                      style={{ color: 'var(--error, #ef4444)' }}
                    ><Trash2 size={16} /></button>
                  </div>
                </span>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>

      {/* ── 轮盘菜单 ──────────────────────────────────────────────── */}
      <SettingGroup title={t('gesture.radialMenu')} icon={<Compass size={20} strokeWidth={2.5} />}>
        <Card>
          <div className="gesture-radial-toolbar">
            <span>{t('gesture.radialHint')}</span>
            <div className="gesture-radial-toolbar__actions">
              <Button size="sm" variant="ghost" onClick={addRadialItem} disabled={radialItems.length >= 8}>
                {t('common.add')}
              </Button>
              <Button size="sm" variant="primary" onClick={() => void saveRadialItems()} disabled={!radialDirty || radialSaving}>
                {radialSaving ? t('common.saving') : t('common.save')}
              </Button>
            </div>
          </div>
          <div className="gesture-radial-list">
            {radialItems.length === 0 && (
              <div className="gesture-empty">{t('common.empty')}</div>
            )}
            {radialItems.map((item, i) => (
              <div key={i} className="gesture-radial-row">
                <span className="gesture-radial-index">{i + 1}</span>
                <TextInput
                  id={`radial-label-${i}`}
                  value={item.label}
                  onChange={(label) => updateRadialItem(i, { label })}
                  placeholder={t('gesture.radialLabelPlaceholder')}
                />
                <Select
                  id={`radial-command-${i}`}
                  value={String(radialCommandIndex(item.command))}
                  onChange={(command) => updateRadialItem(i, { command })}
                  options={BUILTIN_COMMAND_KEYS.map((key, commandIndex) => ({
                    value: String(commandIndex),
                    label: t(key),
                  }))}
                />
                <div className="gesture-radial-actions">
                  <button className="gesture-icon-btn" disabled={i === 0} title={t('common.moveUp')} onClick={() => moveRadialItem(i, -1)}><ArrowUp size={15} /></button>
                  <button className="gesture-icon-btn" disabled={i === radialItems.length - 1} title={t('common.moveDown')} onClick={() => moveRadialItem(i, 1)}><ArrowDown size={15} /></button>
                  <button className="gesture-icon-btn gesture-icon-btn--danger" title={t('common.delete')} onClick={() => removeRadialItem(i)}><Trash2 size={15} /></button>
                </div>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>

      {/* ── 作用域规则 ────────────────────────────────────────────── */}
      <SettingGroup title={t('gesture.scopeRules')} icon={<Target size={20} strokeWidth={2.5} />}>
        <ScopeRulesManager profileNames={profileNames} />
      </SettingGroup>

      {editorOpen && (
        <GestureEditorModal
          key={editing?.gestureCode ?? '__new__'}
          initial={editing}
          existingCodes={mappings.map((m) => m.gestureCode)}
          onSave={handleSaveMapping}
          onClose={() => setEditorOpen(false)}
        />
      )}
    </div>
  );
};

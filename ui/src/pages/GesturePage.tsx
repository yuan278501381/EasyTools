/* ─────────────────────────────────────────────────────────────────────────────
 * GesturePage — 鼠标手势设置页 (WGesture 2 风格 Master-Detail 作用目标架构)
 *
 * 功能:
 *   - 左侧作用目标树: 全局 / 禁用免打扰组 / 应用程序 / 特殊目标(桌面/任务栏)
 *   - 右侧专属手势与触发管理: 支持独立配置、覆盖全局、禁用响应
 *   - 轮盘菜单 (RadialMenu) 配置
 *   - 全局开关 / 轨迹显示 / 触发按钮 / 游戏免打扰
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Badge, Select, Button, TextInput } from '../components/UIKit';
import { GestureEditorModal } from '../components/GestureEditorModal';
import { GestureGuide } from '../components/GestureGuide';
import { HotkeyStatusBadge, type HotkeyEntry } from '../components/HotkeyStatusBadge';
import {
  ScopeTargetsSidebar,
  type ScopeTargetItem,
} from '../components/ScopeTargetsSidebar';
import { TargetAppPickerModal, type AddedTargetResult } from '../components/TargetAppPickerModal';
import {
  type ScopeRule,
  makeRuleId,
} from '../components/scopeModel';
import {
  codeToArrows,
  ACTION_TYPE_KEYS,
  BUILTIN_COMMAND_KEYS,
  type GestureMapping,
} from '../components/gestureModel';
import { bridgeRequest, useBridgeEvent } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import {
  MousePointer2,
  Hand,
  Edit3,
  Trash2,
  Compass,
  ArrowUp,
  ArrowDown,
  Globe,
  ShieldAlert,
  Monitor,
  LayoutTemplate,
  Code2,
  Plus,
  SlidersHorizontal,
  Folder,
  AppWindow,
} from 'lucide-react';
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

interface GestureProfileData {
  name: string;
  mappings: GestureMapping[];
}

export const GesturePage: FC = () => {
  const { t } = useTranslation();
  const [enabled, setEnabled] = useState(true);
  const [trailVisible, setTrailVisible] = useState(true);
  const [autoBypassFullscreen, setAutoBypassFullscreen] = useState(false);
  const [triggerButton, setTriggerButton] = useState('right');
  
  // Profiles & Rules
  const [profiles, setProfiles] = useState<Record<string, GestureMapping[]>>({ default: [] });
  const [rules, setRules] = useState<ScopeRule[]>([]);
  const [selectedTarget, setSelectedTarget] = useState<ScopeTargetItem>({
    id: 'global',
    title: '全局',
    subtitle: '所有未特别定制的窗口',
    kind: 'global',
  });

  const [loading, setLoading] = useState(true);
  const [hotkeys, setHotkeys] = useState<HotkeyEntry[]>([]);
  const [radialItems, setRadialItems] = useState<RadialMenuItem[]>([]);
  const [radialDirty, setRadialDirty] = useState(false);
  const [radialSaving, setRadialSaving] = useState(false);
  const [pauseHotkey, setPauseHotkey] = useState('Ctrl+Alt+Shift+W');

  // Modal states
  const [appPickerOpen, setAppPickerOpen] = useState(false);
  const [appPickerDefaultDisabled, setAppPickerDefaultDisabled] = useState(false);
  const [editorOpen, setEditorOpen] = useState(false);
  const [editingMapping, setEditingMapping] = useState<GestureMapping | null>(null);

  const getHotkey = (name: string) => hotkeys.find(h => h.name === name);

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

  useBridgeEvent('gesture.stateChanged', (data) => {
    const state = data as Partial<GestureState>;
    if (typeof state.enabled === 'boolean') setEnabled(state.enabled);
  });

  useEffect(() => {
    let isMounted = true;
    Promise.all([
      bridgeRequest<GestureState>('gesture.getState'),
      bridgeRequest<GestureProfileData[]>('gesture.getProfiles'),
      bridgeRequest<ScopeRule[]>('gesture.getScopeRules'),
      bridgeRequest<{ items: RadialMenuItem[] }>('radialmenu.getItems'),
      bridgeRequest<HotkeyEntry[]>('hotkey.getAll'),
    ])
      .then(([state, profileList, ruleList, radialRes, hotkeyList]) => {
        if (!isMounted) return;
        setEnabled(state.enabled);
        setTriggerButton(state.triggerButton ?? 'right');
        setTrailVisible(state.trailVisible ?? true);
        setAutoBypassFullscreen(state.autoBypassFullscreen ?? false);

        const pMap: Record<string, GestureMapping[]> = {};
        if (Array.isArray(profileList)) {
          profileList.forEach((p) => {
            pMap[p.name] = Array.isArray(p.mappings) ? p.mappings : [];
          });
        }
        if (!pMap.default) pMap.default = [];
        setProfiles(pMap);

        const rList = Array.isArray(ruleList) ? ruleList : [];
        setRules(rList);

        if (radialRes?.items) setRadialItems(radialRes.items);
        const list = Array.isArray(hotkeyList) ? hotkeyList : [];
        setHotkeys(list);
        const pauseBinding = list.find((entry) => entry.name === 'Pause Gestures');
        if (pauseBinding) setPauseHotkey(pauseBinding.shortcut);
        setLoading(false);
      })
      .catch((err) => {
        if (!isMounted) return;
        console.error('Failed to load gesture config:', err);
        toast.error(t('gesture.loadFailed'));
        setLoading(false);
      });

    return () => {
      isMounted = false;
    };
  }, [t]);

  // ── 获取当前选中 Target 对应的 Profile 名称 ────────────────────────────────
  const getCurrentProfileName = useCallback((): string => {
    if (selectedTarget.kind === 'global') return 'default';
    if (selectedTarget.kind === 'special') {
      return selectedTarget.specialType === 'desktop' ? 'special_desktop' : 'special_taskbar';
    }
    if (selectedTarget.rule?.profileName) return selectedTarget.rule.profileName;
    const sanitized = (selectedTarget.rule?.processName || 'custom').replace(/[^a-zA-Z0-9_]/g, '_').toLowerCase();
    return `app_${sanitized}`;
  }, [selectedTarget]);

  const currentProfileName = getCurrentProfileName();
  const currentMappings: GestureMapping[] = profiles[currentProfileName] ?? (
    selectedTarget.kind === 'global' ? [] : (profiles.default ?? [])
  );

  // ── 持久化当前 Profile 的手势映射 ──────────────────────────────────────────
  const persistMappings = useCallback(async (nextMappings: GestureMapping[]) => {
    const profName = getCurrentProfileName();
    const prevMap = profiles[profName];
    setProfiles((prev) => ({ ...prev, [profName]: nextMappings }));

    try {
      const result = await bridgeRequest<OperationResult>('gesture.updateProfile', {
        name: profName,
        mappings: nextMappings,
      });
      if (!result.success) throw new Error(result.error || t('gesture.saveFailed'));
    } catch (err) {
      console.error('Failed to save gesture profile:', err);
      setProfiles((prev) => ({ ...prev, [profName]: prevMap ?? [] }));
      toast.error(t('gesture.saveFailed'), { description: String(err) });
    }
  }, [getCurrentProfileName, profiles, t]);

  // ── 持久化 ScopeRules ───────────────────────────────────────────────────────
  const persistRules = useCallback(async (nextRules: ScopeRule[]) => {
    const prevRules = rules;
    setRules(nextRules);
    try {
      const result = await bridgeRequest<OperationResult>('gesture.updateScopeRules', { rules: nextRules });
      if (!result.success) throw new Error(result.error || 'Failed to save scope rules');
    } catch (err) {
      console.error('Failed to save scope rules:', err);
      setRules(prevRules);
      toast.error('保存作用域规则失败', { description: String(err) });
    }
  }, [rules]);

  // ── 添加 Target (通过 AppPickerModal) ───────────────────────────────────────
  const handleAddTarget = (res: AddedTargetResult) => {
    const profName = res.effect === 2 ? `app_${(res.processName || res.name).replace(/[^a-zA-Z0-9_]/g, '_').toLowerCase()}` : '';
    const newRule: ScopeRule = {
      id: makeRuleId(),
      name: res.name,
      enabled: true,
      processName: res.processName,
      windowClass: res.windowClass,
      matchMode: 0,
      effect: res.effect,
      profileName: profName,
    };

    const nextRules = [...rules, newRule];
    void persistRules(nextRules);

    // 如果是自定义手势，初始化该 profile (继承当前 default 手势)
    if (res.effect === 2 && profName && !profiles[profName]) {
      const initialMappings = profiles.default ? structuredClone(profiles.default) : [];
      setProfiles((p) => ({ ...p, [profName]: initialMappings }));
      void bridgeRequest('gesture.updateProfile', { name: profName, mappings: initialMappings });
    }

    setAppPickerOpen(false);
    setSelectedTarget({
      id: `rule:${newRule.id}`,
      title: newRule.name || newRule.processName,
      subtitle: newRule.processName || newRule.windowClass,
      kind: res.effect === 1 ? 'disabled' : 'app',
      rule: newRule,
    });
    toast.success(`已添加目标: ${res.name}`);
  };

  // ── 删除 Target ─────────────────────────────────────────────────────────────
  const handleDeleteTarget = (ruleId: string) => {
    const r = rules.find((x) => x.id === ruleId);
    if (!r) return;
    if (!window.confirm(`确定要移除目标「${r.name || r.processName}」的专属配置吗？`)) return;

    const nextRules = rules.filter((x) => x.id !== ruleId);
    void persistRules(nextRules);

    if (selectedTarget.id === `rule:${ruleId}`) {
      setSelectedTarget({
        id: 'global',
        title: '全局',
        subtitle: '所有未特别定制的窗口',
        kind: 'global',
      });
    }
    toast.success('已移除目标配置');
  };

  // ── 切换当前 Target 的作用策略 (自定义手势 <-> 禁用手势) ────────────────────
  const handleToggleStrategy = (newEffect: number) => {
    if (!selectedTarget.rule) return;
    const ruleId = selectedTarget.rule.id;
    const profName = newEffect === 2
      ? (selectedTarget.rule.profileName || `app_${(selectedTarget.rule.processName || 'custom').replace(/[^a-zA-Z0-9_]/g, '_').toLowerCase()}`)
      : '';

    const nextRules = rules.map((r) => {
      if (r.id === ruleId) {
        return { ...r, effect: newEffect, profileName: profName };
      }
      return r;
    });

    void persistRules(nextRules);

    // 如果切换为自定义手势且 Profile 尚不存在，则复制全局手势
    if (newEffect === 2 && profName && !profiles[profName]) {
      const initMaps = profiles.default ? structuredClone(profiles.default) : [];
      setProfiles((p) => ({ ...p, [profName]: initMaps }));
      void bridgeRequest('gesture.updateProfile', { name: profName, mappings: initMaps });
    }

    const updatedRule = nextRules.find((r) => r.id === ruleId);
    setSelectedTarget((prev) => ({
      ...prev,
      kind: newEffect === 1 ? 'disabled' : 'app',
      rule: updatedRule,
    }));
  };

  // ── 手势映射 CRUD ───────────────────────────────────────────────────────────
  const openAddMapping = () => {
    setEditingMapping(null);
    setEditorOpen(true);
  };

  const openEditMapping = (m: GestureMapping) => {
    setEditingMapping(m);
    setEditorOpen(true);
  };

  const handleSaveMapping = (saved: GestureMapping) => {
    const list = [...currentMappings];
    const idx = list.findIndex((m) => m.gestureCode === (editingMapping?.gestureCode ?? saved.gestureCode));
    if (editingMapping && idx >= 0) {
      list[idx] = saved;
    } else {
      const dup = list.findIndex((m) => m.gestureCode === saved.gestureCode);
      if (dup >= 0) list[dup] = saved;
      else list.push(saved);
    }
    void persistMappings(list);
    setEditorOpen(false);
  };

  const handleDeleteMapping = (m: GestureMapping) => {
    if (!window.confirm(t('gesture.deleteConfirm', { name: m.action.name, code: m.gestureCode }))) return;
    void persistMappings(currentMappings.filter((x) => x.gestureCode !== m.gestureCode));
  };

  // ── 触发与全局配置变更 ──────────────────────────────────────────────────────
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

  // ── 轮盘菜单逻辑 ────────────────────────────────────────────────────────────
  const radialCommandIndex = (command: string): number => {
    const legacy: Record<string, number> = { capture: 10, search: 16, lock: 8, pin: 18, record: 11 };
    if (command in legacy) return legacy[command];
    const parsed = Number(command);
    return Number.isInteger(parsed) && parsed >= 0 && parsed < BUILTIN_COMMAND_KEYS.length ? parsed : 10;
  };

  const updateRadialItem = (index: number, patch: Partial<RadialMenuItem>) => {
    setRadialItems((items) =>
      items.map((item, itemIndex) => (itemIndex === index ? { ...item, ...patch } : item))
    );
    setRadialDirty(true);
  };

  const moveRadialItem = (index: number, direction: -1 | 1) => {
    const target = index + direction;
    if (target < 0 || target >= radialItems.length) return;
    setRadialItems((items) => {
      const next = [...items];
      [next[index], next[target]] = [next[target], next[index]];
      return next;
    });
    setRadialDirty(true);
  };

  const removeRadialItem = (index: number) => {
    setRadialItems((items) => items.filter((_, itemIndex) => itemIndex !== index));
    setRadialDirty(true);
  };

  const addRadialItem = () => {
    if (radialItems.length >= 8) return;
    setRadialItems((items) => [
      ...items,
      { label: t('gesture.radialDefaultLabel', { count: items.length + 1 }), command: '10' },
    ]);
    setRadialDirty(true);
  };

  const saveRadialItems = async () => {
    const normalized = radialItems.map((item) => ({ ...item, label: item.label.trim() }));
    if (normalized.some((item) => !item.label)) {
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

  if (loading) {
    return (
      <div className="page-loading">
        <div className="page-loading__spinner" />
        <span>{t('common.loading')}</span>
      </div>
    );
  }

  const renderTargetHeaderIcon = () => {
    if (selectedTarget.kind === 'global') return <Globe size={18} />;
    if (selectedTarget.kind === 'disabled') return <ShieldAlert size={18} style={{ color: '#ef4444' }} />;
    if (selectedTarget.specialType === 'desktop') return <Monitor size={18} />;
    if (selectedTarget.specialType === 'taskbar') return <LayoutTemplate size={18} />;
    const p = (selectedTarget.rule?.processName || '').toLowerCase();
    if (p.includes('code') || p.includes('studio')) return <Code2 size={18} />;
    if (p.includes('explorer')) return <Folder size={18} />;
    return <AppWindow size={18} />;
  };

  return (
    <div className="gesture-page" style={{ animation: 'fadeIn 0.3s ease', paddingBottom: '2.5rem' }}>
      {/* ── 顶部全局开关与触发设置 ──────────────────────────────────── */}
      <SettingGroup title={t('gesture.title')} icon={<MousePointer2 size={20} strokeWidth={2.5} />}>
        <Card>
          <div className={`gesture-status ${enabled ? 'gesture-status--active' : 'gesture-status--paused'}`}>
            <span className="gesture-status__dot" />
            <span className="gesture-status__text">{enabled ? t('gesture.statusRunning') : t('gesture.statusPaused')}</span>
            <div style={{ display: 'inline-flex', alignItems: 'center', gap: '6px' }}>
              <kbd className="gesture-status__hotkey">{pauseHotkey}</kbd>
              <HotkeyStatusBadge entry={getHotkey('Pause Gestures')} />
            </div>
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

      {/* ── WGesture 2 风格作用目标与专属手势配置 (Master-Detail) ─────── */}
      <SettingGroup title="作用目标与手势配置" icon={<Hand size={20} strokeWidth={2.5} />}>
        <div className="gesture-master-detail-layout">
          {/* 左侧目标导航树 */}
          <ScopeTargetsSidebar
            selectedId={selectedTarget.id}
            rules={rules}
            onSelect={setSelectedTarget}
            onAddApp={() => { setAppPickerDefaultDisabled(false); setAppPickerOpen(true); }}
            onAddDisabled={() => { setAppPickerDefaultDisabled(true); setAppPickerOpen(true); }}
            onDeleteRule={handleDeleteTarget}
          />

          {/* 右侧主配置区域 */}
          <main className="gesture-detail-pane">
            {/* 目标信息卡片 */}
            <div className="target-header-card">
              <div className="target-header-info">
                <div className="target-header-icon">
                  {renderTargetHeaderIcon()}
                </div>
                <div className="target-header-texts">
                  <span className="target-header-title">{selectedTarget.title}</span>
                  <span className="target-header-sub">
                    {selectedTarget.kind === 'global' ? '全局通用手势映射' : (selectedTarget.subtitle || '专属手势规则')}
                  </span>
                </div>
              </div>

              {/* 仅在应用程序 Target 下显示策略切换 */}
              {selectedTarget.kind !== 'global' && selectedTarget.kind !== 'special' && selectedTarget.rule && (
                <div className="target-strategy-segmented">
                  <button
                    type="button"
                    className={`target-strategy-btn ${selectedTarget.rule.effect !== 1 ? 'active' : ''}`}
                    onClick={() => handleToggleStrategy(2)}
                  >
                    <SlidersHorizontal size={13} />
                    <span>自定义手势</span>
                  </button>
                  <button
                    type="button"
                    className={`target-strategy-btn ${selectedTarget.rule.effect === 1 ? 'active' : ''}`}
                    onClick={() => handleToggleStrategy(1)}
                  >
                    <ShieldAlert size={13} />
                    <span>禁用手势 (免打扰)</span>
                  </button>
                </div>
              )}
            </div>

            {/* 如果该目标被设置为“禁用手势” */}
            {selectedTarget.kind === 'disabled' || (selectedTarget.rule && selectedTarget.rule.effect === 1) ? (
              <div className="target-disabled-card">
                <div className="target-disabled-icon">
                  <ShieldAlert size={28} />
                </div>
                <span className="target-disabled-title">已在此目标中停用手势响应</span>
                <p className="target-disabled-desc">
                  当该程序处于前台时，EasyTools 将自动放行全部鼠标操作，绝不拦截任何右键或中键事件，保障游戏与绘图无干扰。
                </p>
                <Button size="sm" variant="secondary" onClick={() => handleToggleStrategy(2)}>
                  恢复自定义手势
                </Button>
              </div>
            ) : (
              /* 否则展示手势映射列表卡片 */
              <Card>
                <div className="gesture-toolbar">
                  <span className="gesture-toolbar__count">
                    {selectedTarget.kind === 'global' ? '全局手势表' : `「${selectedTarget.title}」专属手势`}
                    {' '}({currentMappings.length})
                  </span>
                  <Button size="sm" variant="primary" onClick={openAddMapping}>
                    <Plus size={14} />
                    <span>{t('gesture.addMapping')}</span>
                  </Button>
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

                  {currentMappings.length === 0 && (
                    <div className="gesture-empty">{t('gesture.emptyMapping')}</div>
                  )}

                  {currentMappings.map((m, i) => (
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
                          <button className="gesture-icon-btn" title={t('common.edit')} onClick={() => openEditMapping(m)}>
                            <Edit3 size={16} />
                          </button>
                          <button
                            className="gesture-icon-btn"
                            title={t('common.delete')}
                            onClick={() => handleDeleteMapping(m)}
                            style={{ color: 'var(--error, #ef4444)' }}
                          >
                            <Trash2 size={16} />
                          </button>
                        </div>
                      </span>
                    </div>
                  ))}
                </div>
              </Card>
            )}
          </main>
        </div>
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
              <Button
                size="sm"
                variant="primary"
                onClick={() => void saveRadialItems()}
                disabled={!radialDirty || radialSaving}
              >
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
                  <button className="gesture-icon-btn" disabled={i === 0} title={t('common.moveUp')} onClick={() => moveRadialItem(i, -1)}>
                    <ArrowUp size={15} />
                  </button>
                  <button className="gesture-icon-btn" disabled={i === radialItems.length - 1} title={t('common.moveDown')} onClick={() => moveRadialItem(i, 1)}>
                    <ArrowDown size={15} />
                  </button>
                  <button className="gesture-icon-btn gesture-icon-btn--danger" title={t('common.delete')} onClick={() => removeRadialItem(i)}>
                    <Trash2 size={15} />
                  </button>
                </div>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>

      {/* ── 弹窗组 ────────────────────────────────────────────────── */}
      {appPickerOpen && (
        <TargetAppPickerModal
          defaultDisabled={appPickerDefaultDisabled}
          onAdd={handleAddTarget}
          onClose={() => setAppPickerOpen(false)}
        />
      )}

      {editorOpen && (
        <GestureEditorModal
          key={editingMapping?.gestureCode ?? '__new__'}
          initial={editingMapping}
          existingCodes={currentMappings.map((m) => m.gestureCode)}
          onSave={handleSaveMapping}
          onClose={() => setEditorOpen(false)}
        />
      )}
    </div>
  );
};

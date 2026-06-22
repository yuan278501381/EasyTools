/* ─────────────────────────────────────────────────────────────────────────────
 * GesturePage — 鼠标手势设置页
 *
 * 功能:
 *   - 手势全局开关 / 触发按钮 / 轨迹显示
 *   - 手势映射表的增 / 删 / 改 (经 gesture.updateProfile 持久化)
 *   - 动作类型: 快捷键 / Lua 脚本 / 内置命令 / 运行程序
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Badge, Select, Button } from '../components/UIKit';
import { GestureEditorModal } from '../components/GestureEditorModal';
import { ScopeRulesManager } from '../components/ScopeRulesManager';
import { GestureGuide } from '../components/GestureGuide';
import {
  codeToArrows,
  ACTION_TYPE_OPTIONS,
  BUILTIN_COMMANDS,
  type GestureMapping,
} from '../components/gestureModel';
import { bridgeRequest, useBridgeEvent } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { MousePointer2, Hand, Edit3, Trash2, Target } from 'lucide-react';
import './GesturePage.css';

interface GestureState {
  enabled: boolean;
  paused: boolean;
  triggerButton: string;
  trailVisible: boolean;
}

const ACTION_TYPE_LABELS: Record<number, string> = Object.fromEntries(
  ACTION_TYPE_OPTIONS.map((o) => [Number(o.value), o.label]),
);

const PROFILE_NAME = 'default';

/** 取动作的“详情”文本 (按类型显示快捷键 / 命令 / 脚本 / 程序)。 */
function actionDetail(action: GestureMapping['action']): string {
  switch (action.type) {
    case 0: return action.keyStroke ?? '';
    case 1: return '脚本';
    case 2: return BUILTIN_COMMANDS[action.builtinCmd ?? 0] ?? '';
    case 3: return action.programPath ?? '';
    default: return '';
  }
}

export const GesturePage: FC = () => {
  const [enabled, setEnabled] = useState(true);
  const [trailVisible, setTrailVisible] = useState(true);
  const [triggerButton, setTriggerButton] = useState('right');
  const [mappings, setMappings] = useState<GestureMapping[]>([]);
  const [profileNames, setProfileNames] = useState<string[]>([PROFILE_NAME]);
  const [loading, setLoading] = useState(true);
  const { t } = useTranslation();

  const [editorOpen, setEditorOpen] = useState(false);
  const [editing, setEditing] = useState<GestureMapping | null>(null);

  useBridgeEvent('gesture.stateChanged', (data) => {
    const state = data as Partial<GestureState>;
    if (typeof state.enabled === 'boolean') setEnabled(state.enabled);
  });

  useEffect(() => {
    async function loadData() {
      try {
        const [state, profiles] = await Promise.all([
          bridgeRequest<GestureState>('gesture.getState'),
          bridgeRequest<Array<{ name: string; mappings: GestureMapping[] }>>('gesture.getProfiles'),
        ]);
        setEnabled(state.enabled);
        setTriggerButton(state.triggerButton ?? 'right');
        setTrailVisible(state.trailVisible ?? true);
        if (profiles?.length) setProfileNames(profiles.map((p) => p.name));
        const defaultProfile = profiles?.find((p) => p.name === PROFILE_NAME);
        if (defaultProfile) setMappings(defaultProfile.mappings);
      } catch (err) {
        console.error('Failed to load gesture config:', err);
      } finally {
        setLoading(false);
      }
    }
    loadData();
  }, []);

  // ── 持久化整张映射表 ────────────────────────────────────────────────────────
  const persist = useCallback(async (next: GestureMapping[]) => {
    const prev = mappings;
    setMappings(next);
    try {
      await bridgeRequest('gesture.updateProfile', { name: PROFILE_NAME, mappings: next });
    } catch (err) {
      console.error('Failed to save gesture profile:', err);
      setMappings(prev); // 回滚
    }
  }, [mappings]);

  const handleToggleEnabled = async (checked: boolean) => {
    setEnabled(checked);
    try {
      await bridgeRequest('gesture.setPaused', { paused: !checked });
    } catch (err) {
      setEnabled(!checked);
      console.error('Failed to update gesture enabled state:', err);
    }
  };

  const handleToggleTrail = async (checked: boolean) => {
    setTrailVisible(checked);
    try {
      await bridgeRequest('gesture.updateSettings', { trailVisible: checked });
    } catch (err) {
      setTrailVisible(!checked);
      console.error('Failed to update gesture trail state:', err);
    }
  };

  const handleTriggerChange = async (value: string) => {
    const previous = triggerButton;
    setTriggerButton(value);
    try {
      await bridgeRequest('gesture.updateSettings', { triggerButton: value });
    } catch (err) {
      setTriggerButton(previous);
      console.error('Failed to update gesture trigger button:', err);
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
    if (!window.confirm(`确定删除手势 “${m.action.name}” (${m.gestureCode}) 吗？`)) return;
    persist(mappings.filter((x) => x.gestureCode !== m.gestureCode));
  };

  if (loading) {
    return (
      <div className="page-loading">
        <div className="page-loading__spinner" />
        <span>加载中...</span>
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
            <span className="gesture-status__text">{enabled ? '手势正在运行' : '手势已暂停'}</span>
            <kbd className="gesture-status__hotkey">Ctrl+Alt+Shift+W</kbd>
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
                    text={ACTION_TYPE_LABELS[m.action.type] ?? '未知'}
                    variant={m.action.type === 0 ? 'primary' : m.action.type === 2 ? 'success' : 'muted'}
                  />
                </span>
                <span className="gesture-table__col gesture-table__col--key">
                  {actionDetail(m.action) && <kbd className="gesture-kbd">{actionDetail(m.action)}</kbd>}
                </span>
                <span className="gesture-table__col gesture-table__col--actions">
                  <div style={{ display: 'flex', gap: '4px' }}>
                    <button className="gesture-icon-btn" title="编辑" onClick={() => openEdit(m)}><Edit3 size={16} /></button>
                    <button 
                      className="gesture-icon-btn" 
                      title="删除" 
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

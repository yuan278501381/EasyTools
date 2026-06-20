/* ─────────────────────────────────────────────────────────────────────────────
 * ScopeRulesManager — 作用域规则管理 (按进程 / 类名 启用·禁用·切换配置集)
 *
 * 自包含: 挂载时加载 gesture.getScopeRules, 变更后经 gesture.updateScopeRules 持久化。
 * 支持批量勾选删除。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Button, Badge, Toggle } from './UIKit';
import { ScopeRuleModal } from './ScopeRuleModal';
import { type ScopeRule, EFFECT_LABELS, MATCH_MODE_LABELS, ruleTarget } from './scopeModel';
import { bridgeRequest } from '../hooks/useBridge';

interface Props {
  profileNames: string[];
}

export const ScopeRulesManager: FC<Props> = ({ profileNames }) => {
  const [rules, setRules] = useState<ScopeRule[]>([]);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [editorOpen, setEditorOpen] = useState(false);
  const [editing, setEditing] = useState<ScopeRule | null>(null);

  useEffect(() => {
    bridgeRequest<ScopeRule[]>('gesture.getScopeRules')
      .then((data) => setRules(Array.isArray(data) ? data : []))
      .catch((err) => console.error('Failed to load scope rules:', err));
  }, []);

  const persist = useCallback(async (next: ScopeRule[]) => {
    const prev = rules;
    setRules(next);
    try {
      await bridgeRequest('gesture.updateScopeRules', { rules: next });
    } catch (err) {
      console.error('Failed to save scope rules:', err);
      setRules(prev); // 回滚
    }
  }, [rules]);

  const openAdd = () => { setEditing(null); setEditorOpen(true); };
  const openEdit = (r: ScopeRule) => { setEditing(r); setEditorOpen(true); };

  const handleSave = (saved: ScopeRule) => {
    const idx = rules.findIndex((r) => r.id === saved.id);
    const next = [...rules];
    if (idx >= 0) next[idx] = saved; else next.push(saved);
    persist(next);
    setEditorOpen(false);
  };

  const toggleEnabled = (r: ScopeRule, enabled: boolean) =>
    persist(rules.map((x) => (x.id === r.id ? { ...x, enabled } : x)));

  const deleteOne = (r: ScopeRule) => {
    if (!window.confirm(`删除规则 “${r.name}” 吗？`)) return;
    persist(rules.filter((x) => x.id !== r.id));
    setSelected((s) => { const n = new Set(s); n.delete(r.id); return n; });
  };

  const deleteSelected = () => {
    if (selected.size === 0) return;
    if (!window.confirm(`删除选中的 ${selected.size} 条规则吗？`)) return;
    persist(rules.filter((x) => !selected.has(x.id)));
    setSelected(new Set());
  };

  const toggleSelect = (id: string) =>
    setSelected((s) => {
      const n = new Set(s);
      if (n.has(id)) n.delete(id); else n.add(id);
      return n;
    });

  const toggleSelectAll = () =>
    setSelected((s) => (s.size === rules.length ? new Set() : new Set(rules.map((r) => r.id))));

  return (
    <Card>
      <div className="scope-toolbar">
        <span className="scope-toolbar__count">共 {rules.length} 条规则</span>
        <div className="scope-toolbar__actions">
          {selected.size > 0 && (
            <Button size="sm" variant="danger" onClick={deleteSelected}>
              删除选中 ({selected.size})
            </Button>
          )}
          <Button size="sm" variant="primary" onClick={openAdd}>＋ 添加规则</Button>
        </div>
      </div>

      {rules.length === 0 ? (
        <div className="scope-empty">
          还没有作用域规则。默认在所有窗口中启用手势；添加规则可对特定进程/窗口启用、禁用或切换配置集。
        </div>
      ) : (
        <div className="scope-table">
          <div className="scope-row scope-row--head">
            <span className="scope-col scope-col--check">
              <input
                type="checkbox"
                checked={selected.size === rules.length && rules.length > 0}
                onChange={toggleSelectAll}
                aria-label="全选"
              />
            </span>
            <span className="scope-col scope-col--enabled">启用</span>
            <span className="scope-col scope-col--name">名称</span>
            <span className="scope-col scope-col--target">匹配目标</span>
            <span className="scope-col scope-col--effect">效果</span>
            <span className="scope-col scope-col--actions" />
          </div>

          {rules.map((r) => {
            const target = ruleTarget(r);
            return (
              <div key={r.id} className="scope-row">
                <span className="scope-col scope-col--check">
                  <input
                    type="checkbox"
                    checked={selected.has(r.id)}
                    onChange={() => toggleSelect(r.id)}
                    aria-label={`选择 ${r.name}`}
                  />
                </span>
                <span className="scope-col scope-col--enabled">
                  <Toggle id={`scope-${r.id}`} checked={r.enabled} onChange={(v) => toggleEnabled(r, v)} />
                </span>
                <span className="scope-col scope-col--name">{r.name}</span>
                <span className="scope-col scope-col--target">
                  <Badge text={target.kind} variant="muted" />
                  <code className="scope-target-value">{target.value}</code>
                  <span className="scope-matchmode">{MATCH_MODE_LABELS[r.matchMode]}</span>
                </span>
                <span className="scope-col scope-col--effect">
                  <Badge
                    text={EFFECT_LABELS[r.effect] ?? '?'}
                    variant={r.effect === 0 ? 'success' : r.effect === 1 ? 'danger' : 'primary'}
                  />
                  {r.effect === 2 && r.profileName && (
                    <code className="scope-target-value">{r.profileName}</code>
                  )}
                </span>
                <span className="scope-col scope-col--actions">
                  <button className="gesture-icon-btn" title="编辑" onClick={() => openEdit(r)}>✎</button>
                  <button
                    className="gesture-icon-btn gesture-icon-btn--danger"
                    title="删除"
                    onClick={() => deleteOne(r)}
                  >🗑</button>
                </span>
              </div>
            );
          })}
        </div>
      )}

      {editorOpen && (
        <ScopeRuleModal
          key={editing?.id ?? '__new__'}
          initial={editing}
          profileNames={profileNames}
          onSave={handleSave}
          onClose={() => setEditorOpen(false)}
        />
      )}
    </Card>
  );
};

/* ─────────────────────────────────────────────────────────────────────────────
 * GesturePage — 鼠标手势设置页
 *
 * 功能:
 *   - 手势全局开关
 *   - 默认手势列表（支持编辑/删除/添加）
 *   - 手势动作详情展示（方向箭头 + 对应操作）
 *   - 触发按钮配置
 *   - 轨迹显示配置
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Badge, Select } from '../components/UIKit';
import { bridgeRequest, useBridgeEvent } from '../hooks/useBridge';
import './GesturePage.css';

// 手势映射数据
interface GestureMapping {
  gestureCode: string;
  action: {
    type: number;
    name: string;
    keyStroke?: string;
    builtinCmd?: number;
    description?: string;
  };
}

interface GestureState {
  enabled: boolean;
  paused: boolean;
  triggerButton: string;
  trailVisible: boolean;
}

// 方向编码 → 箭头显示
const CODE_TO_ARROWS: Record<string, string> = {
  'L': '←', 'R': '→', 'U': '↑', 'D': '↓',
  'UL': '↖', 'UR': '↗', 'DL': '↙', 'DR': '↘',
  'L-U': '←↑', 'L-D': '←↓', 'L-R': '←→',
  'R-U': '→↑', 'R-D': '→↓', 'R-L': '→←',
  'U-L': '↑←', 'U-R': '↑→', 'U-D': '↑↓',
  'D-U': '↓↑', 'D-R': '↓→', 'D-L': '↓←',
};

// 动作类型标签
const ACTION_TYPE_LABELS: Record<number, string> = {
  0: '快捷键',
  1: 'Lua 脚本',
  2: '内置命令',
  3: '运行程序',
};

export const GesturePage: FC = () => {
  const [enabled, setEnabled] = useState(true);
  const [trailVisible, setTrailVisible] = useState(true);
  const [triggerButton, setTriggerButton] = useState('right');
  const [mappings, setMappings] = useState<GestureMapping[]>([]);
  const [loading, setLoading] = useState(true);

  useBridgeEvent('gesture.stateChanged', (data) => {
    const state = data as Partial<GestureState>;
    if (typeof state.enabled === 'boolean') {
      setEnabled(state.enabled);
    }
  });

  // 加载数据
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

        const defaultProfile = profiles?.find(p => p.name === 'default');
        if (defaultProfile) {
          setMappings(defaultProfile.mappings);
        }
      } catch (err) {
        console.error('Failed to load gesture config:', err);
      } finally {
        setLoading(false);
      }
    }
    loadData();
  }, []);

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

  if (loading) {
    return (
      <div className="page-loading">
        <div className="page-loading__spinner" />
        <span>加载中...</span>
      </div>
    );
  }

  return (
    <div className="gesture-page">
      {/* ── 全局开关 ──────────────────────────────────────────────── */}
      <SettingGroup title="基本设置" icon="🖱️">
        <Card>
          <div className={`gesture-status ${enabled ? 'gesture-status--active' : 'gesture-status--paused'}`}>
            <span className="gesture-status__dot" />
            <span className="gesture-status__text">
              {enabled ? '手势正在运行' : '手势已暂停'}
            </span>
            <kbd className="gesture-status__hotkey">Ctrl+Alt+Shift+W</kbd>
          </div>
          <Toggle
            id="gesture-enabled"
            label="启用鼠标手势"
            description="在全局范围内启用鼠标手势功能"
            checked={enabled}
            onChange={handleToggleEnabled}
          />
          <Toggle
            id="gesture-trail"
            label="显示手势轨迹"
            description="画手势时在屏幕上显示彩色轨迹"
            checked={trailVisible}
            onChange={handleToggleTrail}
          />
          <SettingRow label="触发按钮" description="选择用哪个鼠标按钮触发手势">
            <Select
              id="gesture-trigger"
              value={triggerButton}
              onChange={handleTriggerChange}
              options={[
                { value: 'right', label: '右键' },
                { value: 'middle', label: '中键' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>

      {/* ── 手势映射表 ────────────────────────────────────────────── */}
      <SettingGroup title="手势映射" icon="✋">
        <Card subtitle={`共 ${mappings.length} 个手势`}>
          <div className="gesture-table">
            <div className="gesture-table__header">
              <span className="gesture-table__col gesture-table__col--arrow">手势</span>
              <span className="gesture-table__col gesture-table__col--code">编码</span>
              <span className="gesture-table__col gesture-table__col--action">动作</span>
              <span className="gesture-table__col gesture-table__col--type">类型</span>
              <span className="gesture-table__col gesture-table__col--key">快捷键</span>
            </div>
            {mappings.map((m, i) => (
              <div
                key={m.gestureCode}
                className="gesture-table__row"
                style={{ animationDelay: `${i * 30}ms` }}
              >
                <span className="gesture-table__col gesture-table__col--arrow">
                  <span className="gesture-arrow">
                    {CODE_TO_ARROWS[m.gestureCode] ?? m.gestureCode}
                  </span>
                </span>
                <span className="gesture-table__col gesture-table__col--code">
                  <code>{m.gestureCode}</code>
                </span>
                <span className="gesture-table__col gesture-table__col--action">
                  {m.action.name}
                </span>
                <span className="gesture-table__col gesture-table__col--type">
                  <Badge
                    text={ACTION_TYPE_LABELS[m.action.type] ?? '未知'}
                    variant={m.action.type === 0 ? 'primary' : m.action.type === 2 ? 'success' : 'muted'}
                  />
                </span>
                <span className="gesture-table__col gesture-table__col--key">
                  {m.action.keyStroke && (
                    <kbd className="gesture-kbd">{m.action.keyStroke}</kbd>
                  )}
                </span>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>
    </div>
  );
};

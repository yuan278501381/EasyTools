/* ─────────────────────────────────────────────────────────────────────────────
 * HotCornerPage — 屏幕触发角可视化配置页
 *
 * 功能:
 *   - 中央屏幕示意图 (圆角矩形 + 渐变背景)
 *   - 四角各有一个 Corner Card（显示当前动作 / 允许选择新动作）
 *   - 触发延迟滑块 (100ms ~ 1000ms)
 *   - 全局启用 / 禁用 Toggle
 *   - IPC: hotcorner.getSettings / hotcorner.updateSettings
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { MonitorUp } from 'lucide-react';
import { BUILTIN_COMMANDS } from '../components/gestureModel';
import './HotCornerPage.css';

/* ── 类型定义 ─────────────────────────────────────────────────────────────── */

type CornerPosition = 'topLeft' | 'topRight' | 'bottomLeft' | 'bottomRight';

interface CornerAction {
  /** -1 = 未设置, 0..N = BUILTIN_COMMANDS 索引 */
  commandIndex: number;
}

interface HotCornerSettings {
  enabled: boolean;
  delay: number;
  corners: Record<CornerPosition, CornerAction>;
}

const CORNER_POSITIONS: { key: CornerPosition; cssClass: string; labelKey: string }[] = [
  { key: 'topLeft',     cssClass: 'tl', labelKey: 'hotcorner.cornerTL' },
  { key: 'topRight',    cssClass: 'tr', labelKey: 'hotcorner.cornerTR' },
  { key: 'bottomLeft',  cssClass: 'bl', labelKey: 'hotcorner.cornerBL' },
  { key: 'bottomRight', cssClass: 'br', labelKey: 'hotcorner.cornerBR' },
];

const DEFAULT_SETTINGS: HotCornerSettings = {
  enabled: false,
  delay: 300,
  corners: {
    topLeft:     { commandIndex: -1 },
    topRight:    { commandIndex: -1 },
    bottomLeft:  { commandIndex: -1 },
    bottomRight: { commandIndex: -1 },
  },
};

/* ── 组件 ─────────────────────────────────────────────────────────────────── */

export const HotCornerPage: FC = () => {
  const { t } = useTranslation();
  const [settings, setSettings] = useState<HotCornerSettings>(DEFAULT_SETTINGS);
  const [loading, setLoading] = useState(true);

  // ── 加载设置 ────────────────────────────────────────────────────────────
  useEffect(() => {
    bridgeRequest<HotCornerSettings>('hotcorner.getSettings').then(data => {
      setSettings(prev => ({ ...prev, ...data }));
      setLoading(false);
    });
  }, []);

  // ── 更新设置 ────────────────────────────────────────────────────────────
  const updateSetting = useCallback(<K extends keyof HotCornerSettings>(
    key: K, value: HotCornerSettings[K],
  ) => {
    setSettings(prev => ({ ...prev, [key]: value }));
    bridgeRequest('hotcorner.updateSettings', { [key]: value });
  }, []);

  const updateCorner = useCallback((position: CornerPosition, commandIndex: number) => {
    setSettings(prev => {
      const next = {
        ...prev,
        corners: {
          ...prev.corners,
          [position]: { commandIndex },
        },
      };
      bridgeRequest('hotcorner.updateSettings', { corners: next.corners });
      return next;
    });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading' as any, '加载中...')}</div>;
  }

  // ── 构建动作选项列表 ───────────────────────────────────────────────────
  const actionOptions = [
    { value: '-1', label: t('hotcorner.noAction' as any, '无动作') },
    ...BUILTIN_COMMANDS.map((label, i) => ({ value: String(i), label })),
  ];

  return (
    <div className="hotcorner-page">
      {/* ── 全局开关 ──────────────────────────────────────────────────── */}
      <SettingGroup title={t('hotcorner.title' as any)} icon={<MonitorUp size={20} strokeWidth={2.5} />}>
        <Card>
          <Toggle
            id="hotcorner-enabled"
            label={t('hotcorner.enabled' as any)}
            description={t('hotcorner.enabledDesc' as any)}
            checked={settings.enabled}
            onChange={(v) => updateSetting('enabled', v)}
          />
        </Card>
      </SettingGroup>

      {/* ── 屏幕示意图 ────────────────────────────────────────────────── */}
      <SettingGroup title={t('hotcorner.corners' as any, '触发角配置')} icon={<MonitorUp size={20} strokeWidth={2.5} />}>
        <Card>
          <div className="hotcorner-page__monitor-wrapper">
            <div className="hotcorner-page__monitor">
              {/* 屏幕中央标签 */}
              <div className="hotcorner-page__monitor-label">
                <MonitorUp size={32} className="hotcorner-page__monitor-label-icon" />
                <span className="hotcorner-page__monitor-label-text">
                  {t('hotcorner.screenHint' as any, '将鼠标移到角落触发动作')}
                </span>
              </div>

              {/* 四角装饰连接线 */}
              <div className="hotcorner-page__connector hotcorner-page__connector--tl" />
              <div className="hotcorner-page__connector hotcorner-page__connector--tr" />
              <div className="hotcorner-page__connector hotcorner-page__connector--bl" />
              <div className="hotcorner-page__connector hotcorner-page__connector--br" />

              {/* 四角卡片 */}
              {CORNER_POSITIONS.map(({ key, cssClass, labelKey }) => {
                const action = settings.corners[key];
                const hasAction = action.commandIndex >= 0;
                const actionName = hasAction
                  ? BUILTIN_COMMANDS[action.commandIndex] ?? t('hotcorner.unknown' as any, '未知')
                  : t('hotcorner.noAction' as any, '无动作');

                return (
                  <div
                    key={key}
                    className={`hotcorner-page__corner hotcorner-page__corner--${cssClass}`}
                  >
                    <div className="hotcorner-page__corner-label">
                      {t(labelKey as any)}
                    </div>
                    <div className={`hotcorner-page__corner-action ${!hasAction ? 'hotcorner-page__corner-action--empty' : ''}`}>
                      <span
                        className={`hotcorner-page__corner-indicator ${!hasAction ? 'hotcorner-page__corner-indicator--empty' : ''}`}
                      />
                      {actionName}
                    </div>
                    <div className="hotcorner-page__corner-select">
                      <Select
                        id={`corner-${key}`}
                        value={String(action.commandIndex)}
                        options={actionOptions}
                        onChange={(v) => updateCorner(key, parseInt(v))}
                      />
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        </Card>
      </SettingGroup>

      {/* ── 触发延迟 ──────────────────────────────────────────────────── */}
      <SettingGroup title={t('hotcorner.advanced' as any, '高级选项')} icon={<MonitorUp size={20} strokeWidth={2.5} />}>
        <Card>
          <SettingRow
            label={t('hotcorner.delay' as any)}
            description={t('hotcorner.delayDesc' as any)}
          >
            <div className="hotcorner-page__slider-wrapper">
              <input
                type="range"
                className="hotcorner-page__slider"
                min={100}
                max={1000}
                step={50}
                value={settings.delay}
                onChange={(e) => updateSetting('delay', parseInt(e.target.value))}
              />
              <span className="hotcorner-page__slider-value">{settings.delay}ms</span>
            </div>
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};

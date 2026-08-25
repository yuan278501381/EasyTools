/* ─────────────────────────────────────────────────────────────────────────────
 * SpotlightPage.tsx — 寻找鼠标与演示特效配置中心
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Toggle, SettingGroup, Button } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import {
  SunMedium,
  MousePointerClick,
  RotateCcw,
  Play,
  Activity,
  Sparkles,
} from 'lucide-react';
import './SpotlightPage.css';

interface SpotlightSettings {
  enabled: boolean;
  triggerDoubleCtrl: boolean;
  triggerShakeMouse: boolean;
  autoBypassFullscreen: boolean;
  spotlightColor: string;
  spotlightSize: number;
  animationDurationMs: number;
  holdDurationMs: number;
  shakeThreshold: number;

  clickRippleEnabled: boolean;
  mouseTrailEnabled: boolean;
  leftClickColor: string;
  rightClickColor: string;
  middleClickColor: string;
}

const DEFAULT_SETTINGS: SpotlightSettings = {
  enabled: true,
  triggerDoubleCtrl: true,
  triggerShakeMouse: false,
  autoBypassFullscreen: true,
  spotlightColor: 'auto',
  spotlightSize: 200,
  animationDurationMs: 1000,
  holdDurationMs: 800,
  shakeThreshold: 7,

  clickRippleEnabled: false,
  mouseTrailEnabled: false,
  leftClickColor: 'auto',
  rightClickColor: '#fb7185',
  middleClickColor: '#fbbf24',
};

const ACCENT_COLOR_MAP: Record<string, string> = {
  blue: '#3b82f6',
  cyan: '#06b6d4',
  amber: '#f59e0b',
  mint: '#10b981',
  coral: '#f43f5e',
  violet: '#8b5cf6',
};

export const SpotlightPage: FC = () => {
  const { t } = useTranslation();
  const [settings, setSettings] = useState<SpotlightSettings>(DEFAULT_SETTINGS);
  const [loading, setLoading] = useState(true);
  const [accent, setAccent] = useState<string>(() => {
    try {
      return localStorage.getItem('easytools:accent-color') || 'violet';
    } catch {
      return 'violet';
    }
  });

  useEffect(() => {
    const handleAccent = (e: Event) => {
      const detail = (e as CustomEvent<string>).detail;
      if (detail) setAccent(detail);
    };
    window.addEventListener('easytools:accent-changed', handleAccent);
    return () => window.removeEventListener('easytools:accent-changed', handleAccent);
  }, []);

  useEffect(() => {
    bridgeRequest<SpotlightSettings>('spotlight.getSettings')
      .then((res) => {
        if (res) {
          setSettings((prev) => ({ ...prev, ...res }));
        }
      })
      .catch((err) => {
        console.error('Failed to load spotlight settings:', err);
      })
      .finally(() => setLoading(false));
  }, []);

  const saveSetting = async <K extends keyof SpotlightSettings>(key: K, value: SpotlightSettings[K]) => {
    const previous = settings[key];
    const updated = { ...settings, [key]: value };
    setSettings(updated);
    try {
      await bridgeRequest('spotlight.updateSettings', { [key]: value });
    } catch (e) {
      setSettings((prev) => ({ ...prev, [key]: previous }));
      toast.error(t('spotlight.saveFailed', '保存设置失败'), { description: String(e) });
    }
  };

  const handleResetDefaults = async () => {
    try {
      await bridgeRequest('spotlight.resetDefaults', {});
      setSettings(DEFAULT_SETTINGS);
      toast.success(t('spotlight.resetSuccess', '已恢复默认设置'));
    } catch (e) {
      toast.error(t('spotlight.saveFailed', '恢复默认失败'), { description: String(e) });
    }
  };

  const handleTestSpotlight = async () => {
    try {
      await bridgeRequest('spotlight.trigger', {});
    } catch (e) {
      console.error('Failed to trigger spotlight:', e);
    }
  };

  const resolveColorHex = (val: string) => {
    if (!val || val === 'auto') {
      return ACCENT_COLOR_MAP[accent] || '#8b5cf6';
    }
    return val;
  };

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading', '加载中...')}</div>;
  }

  return (
    <div className="spotlight-page">
      {/* ── 顶部操作栏 ──────────────────────────────────────────────── */}
      <div className="spotlight-page__header">
        <div className="spotlight-page__title-wrap">
          <h2 className="spotlight-page__title">{t('spotlight.title', '寻找鼠标')}</h2>
        </div>
        <Button variant="ghost" onClick={handleResetDefaults} title={t('spotlight.resetDefaults', '恢复默认')}>
          <RotateCcw size={14} style={{ marginRight: 6 }} />
          <span>{t('spotlight.resetDefaults', '恢复默认')}</span>
        </Button>
      </div>

      {/* ── 1. 主开关与全屏免打扰 ────────────────────────────────────── */}
      <Card>
        <Toggle
          id="spotlight-main-enabled"
          label={t('spotlight.mainToggle', '寻找鼠标')}
          description={t('spotlight.mainToggleDesc', '在寻找光标时启用聚光灯效果。')}
          checked={settings.enabled}
          onChange={(v) => saveSetting('enabled', v)}
        />
        <Toggle
          id="spotlight-auto-bypass"
          label={t('spotlight.autoBypassFullscreen', '全屏游戏/视频免打扰')}
          description={t('spotlight.autoBypassFullscreenDesc', '前台处于全屏独占应用时自动免打扰忽略触发，防止 3D 游戏或观影时被干扰')}
          checked={settings.autoBypassFullscreen}
          onChange={(v) => saveSetting('autoBypassFullscreen', v)}
        />
      </Card>

      {/* ── 2. 触发方式 ────────────────────────────────────────────── */}
      <SettingGroup title={t('spotlight.triggerSection', '触发方式')} icon={<Activity size={18} />}>
        <Card>
          <Toggle
            id="spotlight-double-ctrl"
            label={t('spotlight.doubleCtrl', '双击 Ctrl')}
            description={t('spotlight.doubleCtrlDesc', '双击 Control键时触发。')}
            checked={settings.triggerDoubleCtrl}
            onChange={(v) => saveSetting('triggerDoubleCtrl', v)}
          />
          <Toggle
            id="spotlight-shake-mouse"
            label={t('spotlight.shakeMouse', '摇晃鼠标')}
            description={t('spotlight.shakeMouseDesc', '快速摇晃光标时触发。')}
            checked={settings.triggerShakeMouse}
            onChange={(v) => saveSetting('triggerShakeMouse', v)}
          />
        </Card>
      </SettingGroup>

      {/* ── 3. 外观样式 ────────────────────────────────────────────── */}
      <SettingGroup title={t('spotlight.appearanceSection', '外观样式')} icon={<SunMedium size={18} />}>
        <div className="spotlight-page__grid">
          {/* 聚光灯发光颜色 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.spotlightColor', '聚光灯颜色')}</span>
              <span className="spotlight-page__prop-desc">{t('spotlight.spotlightColorDesc', '外部发光颜色。')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <div className="spotlight-page__color-row">
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                  <span className="spotlight-page__color-hex">
                    {settings.spotlightColor === 'auto' ? `${t('spotlight.followBrandAccent')} (${resolveColorHex('auto')})` : settings.spotlightColor}
                  </span>
                  {settings.spotlightColor !== 'auto' && (
                    <button
                      type="button"
                      style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--brand-primary)', fontSize: '0.78rem', display: 'flex', alignItems: 'center', gap: 2 }}
                      onClick={() => saveSetting('spotlightColor', 'auto')}
                      title={t('spotlight.followBrandAccent')}
                    >
                      <Sparkles size={11} />
                      <span>{t('spotlight.followBrandAccent')}</span>
                    </button>
                  )}
                </div>
                <div className="spotlight-page__color-picker-wrap">
                  <div
                    className="spotlight-page__color-swatch"
                    style={{ backgroundColor: resolveColorHex(settings.spotlightColor) }}
                  />
                  <input
                    type="color"
                    className="spotlight-page__color-input"
                    value={resolveColorHex(settings.spotlightColor)}
                    onChange={(e) => saveSetting('spotlightColor', e.target.value)}
                    aria-label={t('spotlight.spotlightColor', '聚光灯颜色')}
                  />
                </div>
              </div>
            </div>
          </div>

          {/* 聚光灯大小 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.spotlightSize', '聚光灯大小')}</span>
              <span className="spotlight-page__prop-desc">{t('spotlight.spotlightSizeDesc', '圆圈直径（像素）。')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <input
                type="number"
                className="spotlight-page__number-input"
                min={80}
                max={600}
                step={20}
                value={settings.spotlightSize}
                onChange={(e) => saveSetting('spotlightSize', Number(e.target.value) || 200)}
                aria-label={t('spotlight.spotlightSize', '聚光灯大小')}
              />
            </div>
          </div>

          {/* 动画时长 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.animDuration', '动画时长')}</span>
              <span className="spotlight-page__prop-desc">{t('spotlight.animDurationDesc', '渐变显示/隐藏速度（ms）。')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <input
                type="number"
                className="spotlight-page__number-input"
                min={200}
                max={3000}
                step={100}
                value={settings.animationDurationMs}
                onChange={(e) => saveSetting('animationDurationMs', Number(e.target.value) || 1000)}
                aria-label={t('spotlight.animDuration', '动画时长')}
              />
            </div>
          </div>

          {/* 停留时长 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.holdDuration', '停留时长')}</span>
              <span className="spotlight-page__prop-desc">{t('spotlight.holdDurationDesc', '光圈完整显示后保留的时间（毫秒）。')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <input
                type="number"
                className="spotlight-page__number-input"
                min={100}
                max={5000}
                step={100}
                value={settings.holdDurationMs}
                onChange={(e) => saveSetting('holdDurationMs', Number(e.target.value) || 800)}
                aria-label={t('spotlight.holdDuration', '停留时长')}
              />
            </div>
          </div>

          {/* 摇晃阈值 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.shakeThreshold', '摇晃阈值')}</span>
              <span className="spotlight-page__prop-desc">{t('spotlight.shakeThresholdDesc', '摇晃检测灵敏度（默认 7）。')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <input
                type="number"
                className="spotlight-page__number-input"
                min={3}
                max={20}
                step={1}
                value={settings.shakeThreshold}
                onChange={(e) => saveSetting('shakeThreshold', Number(e.target.value) || 7)}
                aria-label={t('spotlight.shakeThreshold', '摇晃阈值')}
              />
            </div>
          </div>
        </div>
      </SettingGroup>

      {/* ── 4. 鼠标点击与轨迹特效 (演示辅助) ────────────────────────── */}
      <SettingGroup title={t('spotlight.mouseFxSection', '鼠标点击与轨迹特效')} icon={<MousePointerClick size={18} />}>
        <Card>
          <Toggle
            id="spotlight-click-ripple"
            label={t('spotlight.clickRipple', '显示点击光圈')}
            description={t('spotlight.clickRippleDesc', '在鼠标点击位置显示短暂的点击光圈。')}
            checked={settings.clickRippleEnabled}
            onChange={(v) => saveSetting('clickRippleEnabled', v)}
          />
          <Toggle
            id="spotlight-mouse-trail"
            label={t('spotlight.mouseTrail', '显示鼠标轨迹')}
            description={t('spotlight.mouseTrailDesc', '在鼠标移动路径上显示逐渐淡出的彩色轨迹。')}
            checked={settings.mouseTrailEnabled}
            onChange={(v) => saveSetting('mouseTrailEnabled', v)}
          />
        </Card>

        {/* 独立按键点击颜色 */}
        <div className="spotlight-page__grid" style={{ marginTop: '16px' }}>
          {/* 左键颜色 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.leftClickColor', '左键点击颜色')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <div className="spotlight-page__color-row">
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                  <span className="spotlight-page__color-hex">
                    {settings.leftClickColor === 'auto' ? `${t('spotlight.followBrandAccent')} (${resolveColorHex('auto')})` : settings.leftClickColor}
                  </span>
                  {settings.leftClickColor !== 'auto' && (
                    <button
                      type="button"
                      style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--brand-primary)', fontSize: '0.78rem', display: 'flex', alignItems: 'center', gap: 2 }}
                      onClick={() => saveSetting('leftClickColor', 'auto')}
                      title={t('spotlight.followBrandAccent')}
                    >
                      <Sparkles size={11} />
                      <span>{t('spotlight.followBrandAccent')}</span>
                    </button>
                  )}
                </div>
                <div className="spotlight-page__color-picker-wrap">
                  <div
                    className="spotlight-page__color-swatch"
                    style={{ backgroundColor: resolveColorHex(settings.leftClickColor) }}
                  />
                  <input
                    type="color"
                    className="spotlight-page__color-input"
                    value={resolveColorHex(settings.leftClickColor)}
                    onChange={(e) => saveSetting('leftClickColor', e.target.value)}
                    aria-label={t('spotlight.leftClickColor', '左键点击颜色')}
                  />
                </div>
              </div>
            </div>
          </div>

          {/* 右键颜色 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.rightClickColor', '右键点击颜色')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <div className="spotlight-page__color-row">
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                  <span className="spotlight-page__color-hex">
                    {settings.rightClickColor === 'auto' ? `${t('spotlight.followBrandAccent')} (${resolveColorHex('auto')})` : settings.rightClickColor}
                  </span>
                  {settings.rightClickColor !== 'auto' && (
                    <button
                      type="button"
                      style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--brand-primary)', fontSize: '0.78rem', display: 'flex', alignItems: 'center', gap: 2 }}
                      onClick={() => saveSetting('rightClickColor', 'auto')}
                      title={t('spotlight.followBrandAccent')}
                    >
                      <Sparkles size={11} />
                      <span>{t('spotlight.followBrandAccent')}</span>
                    </button>
                  )}
                </div>
                <div className="spotlight-page__color-picker-wrap">
                  <div
                    className="spotlight-page__color-swatch"
                    style={{ backgroundColor: resolveColorHex(settings.rightClickColor) }}
                  />
                  <input
                    type="color"
                    className="spotlight-page__color-input"
                    value={resolveColorHex(settings.rightClickColor)}
                    onChange={(e) => saveSetting('rightClickColor', e.target.value)}
                    aria-label={t('spotlight.rightClickColor', '右键点击颜色')}
                  />
                </div>
              </div>
            </div>
          </div>

          {/* 中键颜色 */}
          <div className="spotlight-page__prop-card">
            <div className="spotlight-page__prop-header">
              <span className="spotlight-page__prop-title">{t('spotlight.middleClickColor', '中键点击颜色')}</span>
            </div>
            <div className="spotlight-page__prop-body">
              <div className="spotlight-page__color-row">
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                  <span className="spotlight-page__color-hex">
                    {settings.middleClickColor === 'auto' ? `${t('spotlight.followBrandAccent')} (${resolveColorHex('auto')})` : settings.middleClickColor}
                  </span>
                  {settings.middleClickColor !== 'auto' && (
                    <button
                      type="button"
                      style={{ background: 'none', border: 'none', cursor: 'pointer', color: 'var(--brand-primary)', fontSize: '0.78rem', display: 'flex', alignItems: 'center', gap: 2 }}
                      onClick={() => saveSetting('middleClickColor', 'auto')}
                      title={t('spotlight.followBrandAccent')}
                    >
                      <Sparkles size={11} />
                      <span>{t('spotlight.followBrandAccent')}</span>
                    </button>
                  )}
                </div>
                <div className="spotlight-page__color-picker-wrap">
                  <div
                    className="spotlight-page__color-swatch"
                    style={{ backgroundColor: resolveColorHex(settings.middleClickColor) }}
                  />
                  <input
                    type="color"
                    className="spotlight-page__color-input"
                    value={resolveColorHex(settings.middleClickColor)}
                    onChange={(e) => saveSetting('middleClickColor', e.target.value)}
                    aria-label={t('spotlight.middleClickColor', '中键点击颜色')}
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
      </SettingGroup>

      {/* ── 5. 即刻体验 ────────────────────────────────────────────── */}
      <div className="spotlight-page__action-footer">
        <Button variant="primary" onClick={handleTestSpotlight}>
          <Play size={14} style={{ marginRight: 6 }} />
          <span>{t('spotlight.testSpotlight', '立即体验聚光灯')}</span>
        </Button>
      </div>
    </div>
  );
};

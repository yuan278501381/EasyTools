/* ─────────────────────────────────────────────────────────────────────────────
 * SpotlightPage.tsx — 寻找鼠标与演示特效配置中心 (世界级 UX 双态色彩体系)
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Toggle, SettingGroup, Button } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import {
  MousePointerClick,
  RotateCcw,
  Play,
  Activity,
  Sparkles,
  Palette,
  Waves,
  Crosshair,
  Flame,
  CircleDot,
  Rainbow,
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
  clickRippleStyle: string;
  mouseTrailEnabled: boolean;
  mouseTrailStyle: string;
  mouseTrailColorMode: string;
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
  clickRippleStyle: 'ripple_ring',
  mouseTrailEnabled: false,
  mouseTrailStyle: 'stardust_orbs',
  mouseTrailColorMode: 'rainbow',
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

/** 颜色双态分段胶囊组件 */
interface ColorSegmentControlProps {
  label: string;
  desc?: string;
  value: string;
  defaultCustomFallback: string;
  brandAccentHex: string;
  onChange: (val: string) => void;
}

const ColorSegmentControl: FC<ColorSegmentControlProps> = ({
  label,
  desc,
  value,
  defaultCustomFallback,
  brandAccentHex,
  onChange,
}) => {
  const { t } = useTranslation();
  const isAuto = value === 'auto';
  const displayHex = isAuto ? brandAccentHex : value;

  return (
    <div className="spotlight-page__prop-card spotlight-page__color-card">
      <div className="spotlight-page__prop-header">
        <span className="spotlight-page__prop-title">{label}</span>
        {desc && <span className="spotlight-page__prop-desc">{desc}</span>}
      </div>

      {/* 双态胶囊选择器 */}
      <div className="spotlight-page__capsule-wrap">
        <button
          type="button"
          className={`spotlight-page__capsule-btn ${isAuto ? 'active' : ''}`}
          onClick={() => onChange('auto')}
        >
          <Sparkles size={13} />
          <span>{t('spotlight.followBrandAccent', '跟随品牌色')}</span>
          <span className="spotlight-page__capsule-dot" style={{ backgroundColor: brandAccentHex }} />
        </button>

        <button
          type="button"
          className={`spotlight-page__capsule-btn ${!isAuto ? 'active' : ''}`}
          onClick={() => {
            if (isAuto) {
              onChange(defaultCustomFallback);
            }
          }}
        >
          <Palette size={13} />
          <span>{t('spotlight.customColor', '自定义颜色')}</span>
        </button>
      </div>

      {/* 色彩展示与调色盘操作 */}
      <div className="spotlight-page__prop-body">
        <div className="spotlight-page__color-row">
          <div className="spotlight-page__color-info">
            <span className="spotlight-page__color-hex">{displayHex.toUpperCase()}</span>
            {!isAuto && (
              <button
                type="button"
                className="spotlight-page__restore-capsule"
                onClick={() => onChange('auto')}
                title={t('spotlight.restoreFollowBrandDesc', '一键切回并实时联动 EasyTools 品牌主色')}
              >
                <RotateCcw size={11} />
                <span>{t('spotlight.restoreFollowBrand', '恢复跟随品牌色')}</span>
              </button>
            )}
          </div>

          <div className="spotlight-page__color-picker-wrap">
            <div
              className="spotlight-page__color-swatch"
              style={{ backgroundColor: displayHex }}
              title={isAuto ? t('spotlight.followBrandAccent') : displayHex}
            />
            {!isAuto && (
              <input
                type="color"
                className="spotlight-page__color-input"
                value={displayHex}
                onChange={(e) => onChange(e.target.value)}
                aria-label={label}
              />
            )}
          </div>
        </div>
      </div>
    </div>
  );
};

export const SpotlightPage: FC = () => {
  const { t } = useTranslation();
  const [settings, setSettings] = useState<SpotlightSettings>(DEFAULT_SETTINGS);
  const [loading, setLoading] = useState(true);
  const [accent, setAccent] = useState<string>(() => {
    try {
      return localStorage.getItem('easytools:accent-color') || 'blue';
    } catch {
      return 'blue';
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

  const currentBrandAccentHex = ACCENT_COLOR_MAP[accent] || '#3b82f6';

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading', '加载中...')}</div>;
  }

  return (
    <div className="spotlight-page">
      {/* ── 顶部操作栏 ──────────────────────────────────────────────── */}
      <div className="spotlight-page__header">
        <div className="spotlight-page__title-wrap">
          <h2 className="spotlight-page__title">{t('spotlight.title', '鼠标演示与特效')}</h2>
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
          label={t('spotlight.mainToggle', '启用鼠标演示与特效')}
          description={t('spotlight.mainToggleDesc', '开启后可使用光标聚光灯、点击水波纹动画及移动轨迹特效。')}
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
      <SettingGroup title={t('spotlight.appearanceSection', '外观样式')} icon={<Sparkles size={18} />}>
        <div className="spotlight-page__grid">
          {/* 聚光灯发光颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('spotlight.spotlightColor', '聚光灯颜色')}
            desc={t('spotlight.spotlightColorDesc', '外部发光颜色。')}
            value={settings.spotlightColor}
            defaultCustomFallback="#3b82f6"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('spotlightColor', val)}
          />

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
            description={t('spotlight.clickRippleDesc', '在鼠标点击位置显示短暂的流体点击光圈与冲击波。')}
            checked={settings.clickRippleEnabled}
            onChange={(v) => saveSetting('clickRippleEnabled', v)}
          />

          {settings.clickRippleEnabled && (
            <div style={{ marginTop: '14px', paddingTop: '14px', borderTop: '1px solid var(--card-border)' }}>
              <div style={{ marginBottom: '10px', display: 'flex', flexDirection: 'column', gap: '2px' }}>
                <span className="spotlight-page__prop-title">{t('spotlight.clickStyle', '点击动效风格')}</span>
                <span className="spotlight-page__prop-desc">{t('spotlight.clickStyleDesc', '选择鼠标点击时的视觉波纹与反馈形态')}</span>
              </div>
              <div className="spotlight-page__style-grid">
                {/* 1. 流体光圈冲击波 */}
                <div
                  className={`spotlight-page__style-card ${settings.clickRippleStyle === 'ripple_ring' ? 'spotlight-page__style-card--active' : ''}`}
                  onClick={() => saveSetting('clickRippleStyle', 'ripple_ring')}
                >
                  <div className="spotlight-page__style-card-header">
                    <Waves size={16} />
                    <span>{t('spotlight.styleRippleRing', '流体光圈冲击波')}</span>
                  </div>
                  <span className="spotlight-page__style-card-desc">{t('spotlight.styleRippleRingDesc', '双层半透明流体冲击波光环平滑向外扩散，经典直观 (推荐)')}</span>
                </div>

                {/* 2. 星芒微粒迸发 */}
                <div
                  className={`spotlight-page__style-card ${settings.clickRippleStyle === 'sparkle_burst' ? 'spotlight-page__style-card--active' : ''}`}
                  onClick={() => saveSetting('clickRippleStyle', 'sparkle_burst')}
                >
                  <div className="spotlight-page__style-card-header">
                    <Sparkles size={16} />
                    <span>{t('spotlight.styleSparkleBurst', '星芒微粒迸发')}</span>
                  </div>
                  <span className="spotlight-page__style-card-desc">{t('spotlight.styleSparkleBurstDesc', '点击瞬间向四周迸发数颗微型星芒光粒，灵动活泼')}</span>
                </div>

                {/* 3. 精密雷达靶心 */}
                <div
                  className={`spotlight-page__style-card ${settings.clickRippleStyle === 'target_pulse' ? 'spotlight-page__style-card--active' : ''}`}
                  onClick={() => saveSetting('clickRippleStyle', 'target_pulse')}
                >
                  <div className="spotlight-page__style-card-header">
                    <Crosshair size={16} />
                    <span>{t('spotlight.styleTargetPulse', '精密雷达靶心')}</span>
                  </div>
                  <span className="spotlight-page__style-card-desc">{t('spotlight.styleTargetPulseDesc', '极细科技感准星与向中心收缩的聚焦环，精准指引视线')}</span>
                </div>

                {/* 4. 柔光微晕气泡 */}
                <div
                  className={`spotlight-page__style-card ${settings.clickRippleStyle === 'soft_glow' ? 'spotlight-page__style-card--active' : ''}`}
                  onClick={() => saveSetting('clickRippleStyle', 'soft_glow')}
                >
                  <div className="spotlight-page__style-card-header">
                    <CircleDot size={16} />
                    <span>{t('spotlight.styleSoftGlow', '柔光微晕气泡')}</span>
                  </div>
                  <span className="spotlight-page__style-card-desc">{t('spotlight.styleSoftGlowDesc', '极简羽化径向微光晕，温润轻柔，零干扰不刺眼')}</span>
                </div>
              </div>
            </div>
          )}

          <div style={{ marginTop: '14px', paddingTop: '14px', borderTop: '1px solid var(--card-border)' }}>
            <Toggle
              id="spotlight-mouse-trail"
              label={t('spotlight.mouseTrail', '显示鼠标轨迹')}
              description={t('spotlight.mouseTrailDesc', '在鼠标移动路径上显示渐隐彩色轨迹。')}
              checked={settings.mouseTrailEnabled}
              onChange={(v) => saveSetting('mouseTrailEnabled', v)}
            />
          </div>

          {settings.mouseTrailEnabled && (
            <div style={{ marginTop: '14px', paddingTop: '14px', borderTop: '1px solid var(--card-border)', display: 'flex', flexDirection: 'column', gap: '14px' }}>
              {/* 轨迹风格 */}
              <div>
                <div style={{ marginBottom: '10px', display: 'flex', flexDirection: 'column', gap: '2px' }}>
                  <span className="spotlight-page__prop-title">{t('spotlight.trailStyle', '轨迹动效风格')}</span>
                  <span className="spotlight-page__prop-desc">{t('spotlight.trailStyleDesc', '选择适合您演示或日常工作习惯的鼠标轨迹形态')}</span>
                </div>
                <div className="spotlight-page__style-grid">
                  {/* 1. 七彩星尘光球 */}
                  <div
                    className={`spotlight-page__style-card ${settings.mouseTrailStyle === 'stardust_orbs' ? 'spotlight-page__style-card--active' : ''}`}
                    onClick={() => saveSetting('mouseTrailStyle', 'stardust_orbs')}
                  >
                    <div className="spotlight-page__style-card-header">
                      <Sparkles size={16} />
                      <span>{t('spotlight.styleStardustOrbs', '七彩星尘光球')}</span>
                    </div>
                    <span className="spotlight-page__style-card-desc">{t('spotlight.styleStardustOrbsDesc', '大中小错落的七彩微型浮动光球，轻盈通透不遮挡文字，守护专注心流 (推荐)')}</span>
                  </div>

                  {/* 2. 极光流体丝带 */}
                  <div
                    className={`spotlight-page__style-card ${settings.mouseTrailStyle === 'aurora_ribbon' ? 'spotlight-page__style-card--active' : ''}`}
                    onClick={() => saveSetting('mouseTrailStyle', 'aurora_ribbon')}
                  >
                    <div className="spotlight-page__style-card-header">
                      <Waves size={16} />
                      <span>{t('spotlight.styleAuroraRibbon', '极光流体丝带')}</span>
                    </div>
                    <span className="spotlight-page__style-card-desc">{t('spotlight.styleAuroraRibbonDesc', '极细半透明流体渐变丝带，优雅锐利')}</span>
                  </div>

                  {/* 3. 彩色声纳微环 */}
                  <div
                    className={`spotlight-page__style-card ${settings.mouseTrailStyle === 'sonar_pulses' ? 'spotlight-page__style-card--active' : ''}`}
                    onClick={() => saveSetting('mouseTrailStyle', 'sonar_pulses')}
                  >
                    <div className="spotlight-page__style-card-header">
                      <CircleDot size={16} />
                      <span>{t('spotlight.styleSonarPulses', '彩色声纳微环')}</span>
                    </div>
                    <span className="spotlight-page__style-card-desc">{t('spotlight.styleSonarPulsesDesc', '大间距彩色微光足迹环，95% 空间通透')}</span>
                  </div>

                  {/* 4. 经典彗星流光 */}
                  <div
                    className={`spotlight-page__style-card ${settings.mouseTrailStyle === 'classic_comet' ? 'spotlight-page__style-card--active' : ''}`}
                    onClick={() => saveSetting('mouseTrailStyle', 'classic_comet')}
                  >
                    <div className="spotlight-page__style-card-header">
                      <Flame size={16} />
                      <span>{t('spotlight.styleClassicComet', '经典彗星流光')}</span>
                    </div>
                    <span className="spotlight-page__style-card-desc">{t('spotlight.styleClassicCometDesc', '连贯高亮彗尾拖光效果')}</span>
                  </div>
                </div>
              </div>

              {/* 轨迹色彩模式 */}
              <div className="spotlight-page__prop-card" style={{ marginTop: '4px' }}>
                <div className="spotlight-page__prop-header">
                  <span className="spotlight-page__prop-title">{t('spotlight.trailColorMode', '轨迹色彩模式')}</span>
                  <span className="spotlight-page__prop-desc">{t('spotlight.trailColorModeDesc', '设置轨迹颜色流转规则')}</span>
                </div>
                <div className="spotlight-page__capsule-wrap" style={{ maxWidth: '380px' }}>
                  <button
                    type="button"
                    className={`spotlight-page__capsule-btn ${settings.mouseTrailColorMode === 'rainbow' ? 'spotlight-page__capsule-btn--active' : ''}`}
                    onClick={() => saveSetting('mouseTrailColorMode', 'rainbow')}
                  >
                    <Rainbow size={14} />
                    <span>{t('spotlight.trailColorModeRainbow', '七彩流光谱系 (推荐)')}</span>
                  </button>
                  <button
                    type="button"
                    className={`spotlight-page__capsule-btn ${settings.mouseTrailColorMode === 'accent' ? 'spotlight-page__capsule-btn--active' : ''}`}
                    onClick={() => saveSetting('mouseTrailColorMode', 'accent')}
                  >
                    <Palette size={14} />
                    <span>{t('spotlight.trailColorModeAccent', '跟随强调色 / 自定义')}</span>
                  </button>
                </div>
              </div>
            </div>
          )}
        </Card>

        {/* 独立按键点击颜色 (双态胶囊体系) */}
        <div className="spotlight-page__grid" style={{ marginTop: '16px' }}>
          {/* 左键颜色 */}
          <ColorSegmentControl
            label={t('spotlight.leftClickColor', '左键点击颜色')}
            value={settings.leftClickColor}
            defaultCustomFallback="#3b82f6"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('leftClickColor', val)}
          />

          {/* 右键颜色 */}
          <ColorSegmentControl
            label={t('spotlight.rightClickColor', '右键点击颜色')}
            value={settings.rightClickColor}
            defaultCustomFallback="#fb7185"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('rightClickColor', val)}
          />

          {/* 中键颜色 */}
          <ColorSegmentControl
            label={t('spotlight.middleClickColor', '中键点击颜色')}
            value={settings.middleClickColor}
            defaultCustomFallback="#fbbf24"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('middleClickColor', val)}
          />
        </div>
      </SettingGroup>

      {/* ── 底部即刻体验栏 ──────────────────────────────────────────── */}
      <div className="spotlight-page__action-footer">
        <Button variant="primary" onClick={handleTestSpotlight}>
          <Play size={14} style={{ marginRight: 6 }} />
          <span>{t('spotlight.testSpotlight', '立即体验聚光灯')}</span>
        </Button>
      </div>
    </div>
  );
};

/* ─────────────────────────────────────────────────────────────────────────────
 * OnboardingModal — 首次使用引导弹窗
 *
 * 4 步引导流程:
 *   1. 欢迎 — 功能亮点卡片
 *   2. 核心快捷键
 *   3. 手势入门
 *   4. 完成
 * ───────────────────────────────────────────────────────────────────────────── */

import { useEffect, useRef, useState, type FC } from 'react';
import { Button } from './UIKit';
import { useTranslation } from 'react-i18next';
import {
  Search, Camera, Mouse, Sparkles, FolderSymlink,
  Keyboard, Highlighter, ChevronRight, ChevronLeft, Check, Zap,
} from 'lucide-react';
import './OnboardingModal.css';

interface Props {
  onComplete: () => void;
}

const TOTAL_STEPS = 4;

export const OnboardingModal: FC<Props> = ({ onComplete }) => {
  const { t } = useTranslation();
  const [step, setStep] = useState(0);
  const cardRef = useRef<HTMLDivElement>(null);
  const titleRefs = useRef<Array<HTMLHeadingElement | null>>([]);

  useEffect(() => {
    titleRefs.current[step]?.focus();
  }, [step]);

  useEffect(() => {
    const previousFocus = document.activeElement instanceof HTMLElement
      ? document.activeElement : null;
    const initialFocusFrame = requestAnimationFrame(() => titleRefs.current[0]?.focus());
    const focusableSelector = 'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        onComplete();
      } else if (event.key === 'ArrowRight') {
        event.preventDefault();
        setStep((current) => Math.min(TOTAL_STEPS - 1, current + 1));
      } else if (event.key === 'ArrowLeft') {
        event.preventDefault();
        setStep((current) => Math.max(0, current - 1));
      } else if (event.key === 'Tab' && cardRef.current) {
        const focusable = Array.from(
          cardRef.current.querySelectorAll<HTMLElement>(focusableSelector));
        if (focusable.length === 0) return;
        const first = focusable[0];
        const last = focusable[focusable.length - 1];
        if (event.shiftKey && document.activeElement === first) {
          event.preventDefault();
          last.focus();
        } else if (!event.shiftKey && document.activeElement === last) {
          event.preventDefault();
          first.focus();
        }
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      cancelAnimationFrame(initialFocusFrame);
      window.removeEventListener('keydown', onKeyDown);
      previousFocus?.focus();
    };
  }, [onComplete]);

  const next = () => {
    if (step < TOTAL_STEPS - 1) setStep(step + 1);
    else onComplete();
  };

  const prev = () => {
    if (step > 0) setStep(step - 1);
  };

  const getStepClass = (idx: number) => {
    if (idx === step) return 'onboarding__step onboarding__step--active';
    if (idx < step) return 'onboarding__step onboarding__step--prev';
    return 'onboarding__step onboarding__step--next';
  };

  return (
    <div className="onboarding">
      <div
        ref={cardRef}
        className="onboarding__card"
        role="dialog"
        aria-modal="true"
        aria-labelledby={`onboarding-step-${step}-title`}
      >
        <div className="onboarding__glow" aria-hidden="true" />
        <button className="onboarding__skip" onClick={onComplete}>
          {t('onboarding.skip')}
        </button>

        <div className="onboarding__body">
          {/* ── Step 0: 欢迎 (6 宫格全旗舰功能矩阵) ───────────────── */}
          <div className={getStepClass(0)} aria-hidden={step !== 0}>
            <h2 id="onboarding-step-0-title" ref={(node) => { titleRefs.current[0] = node; }} tabIndex={-1} className="onboarding__title">{t('onboarding.welcomeTitle')}</h2>
            <p className="onboarding__subtitle">{t('onboarding.welcomeSubtitle')}</p>
            <div className="onboarding__features">
              <div className="onboarding__feature-card">
                <div className="onboarding__feature-icon">
                  <Search size={20} />
                </div>
                <span className="onboarding__feature-name">{t('onboarding.featureSearch')}</span>
                <span className="onboarding__feature-desc">{t('onboarding.featureSearchDesc')}</span>
              </div>
              <div className="onboarding__feature-card">
                <div className="onboarding__feature-icon">
                  <Mouse size={20} />
                </div>
                <span className="onboarding__feature-name">{t('onboarding.featureGesture')}</span>
                <span className="onboarding__feature-desc">{t('onboarding.featureGestureDesc')}</span>
              </div>
              <div className="onboarding__feature-card">
                <div className="onboarding__feature-icon">
                  <Camera size={20} />
                </div>
                <span className="onboarding__feature-name">{t('onboarding.featureCapture')}</span>
                <span className="onboarding__feature-desc">{t('onboarding.featureCaptureDesc')}</span>
              </div>
              <div className="onboarding__feature-card">
                <div className="onboarding__feature-icon">
                  <Highlighter size={20} />
                </div>
                <span className="onboarding__feature-name">{t('onboarding.featureSpotlight')}</span>
                <span className="onboarding__feature-desc">{t('onboarding.featureSpotlightDesc')}</span>
              </div>
              <div className="onboarding__feature-card">
                <div className="onboarding__feature-icon">
                  <Keyboard size={20} />
                </div>
                <span className="onboarding__feature-name">{t('onboarding.featureKeycast')}</span>
                <span className="onboarding__feature-desc">{t('onboarding.featureKeycastDesc')}</span>
              </div>
              <div className="onboarding__feature-card">
                <div className="onboarding__feature-icon">
                  <FolderSymlink size={20} />
                </div>
                <span className="onboarding__feature-name">{t('onboarding.featureDialogEnhancer')}</span>
                <span className="onboarding__feature-desc">{t('onboarding.featureDialogEnhancerDesc')}</span>
              </div>
            </div>
          </div>

          {/* ── Step 1: 核心快捷键矩阵 ───────────────────────── */}
          <div className={getStepClass(1)} aria-hidden={step !== 1}>
            <h2 id="onboarding-step-1-title" ref={(node) => { titleRefs.current[1] = node; }} tabIndex={-1} className="onboarding__title">{t('onboarding.shortcutsTitle')}</h2>
            <p className="onboarding__subtitle">{t('onboarding.shortcutsSubtitle')}</p>
            <div className="onboarding__shortcuts">
              <div className="onboarding__shortcut-row">
                <span className="onboarding__shortcut-name">
                  <Search size={14} style={{ color: 'var(--primary)', marginRight: 6 }} />
                  {t('onboarding.shortcutSearch')}
                </span>
                <kbd className="onboarding__shortcut-kbd">Alt + Space</kbd>
              </div>
              <div className="onboarding__shortcut-row">
                <span className="onboarding__shortcut-name">
                  <Camera size={14} style={{ color: 'var(--success, #34d399)', marginRight: 6 }} />
                  {t('onboarding.shortcutCapture')}
                </span>
                <kbd className="onboarding__shortcut-kbd">Ctrl + Shift + A</kbd>
              </div>
              <div className="onboarding__shortcut-row">
                <span className="onboarding__shortcut-name">
                  <Zap size={14} style={{ color: '#fbbf24', marginRight: 6 }} />
                  {t('onboarding.shortcutRecord')}
                </span>
                <kbd className="onboarding__shortcut-kbd">Ctrl + Shift + R</kbd>
              </div>
              <div className="onboarding__shortcut-row">
                <span className="onboarding__shortcut-name">
                  <Highlighter size={14} style={{ color: '#38bdf8', marginRight: 6 }} />
                  {t('onboarding.shortcutSpotlight')}
                </span>
                <kbd className="onboarding__shortcut-kbd">F2</kbd>
              </div>
              <div className="onboarding__shortcut-row">
                <span className="onboarding__shortcut-name">
                  <Sparkles size={14} style={{ color: '#ec4899', marginRight: 6 }} />
                  {t('onboarding.shortcutOcr')}
                </span>
                <kbd className="onboarding__shortcut-kbd">Ctrl + Shift + O</kbd>
              </div>
            </div>
          </div>

          {/* ── Step 2: 手势与屏幕演示特效入门 ───────────────── */}
          <div className={getStepClass(2)} aria-hidden={step !== 2}>
            <h2 id="onboarding-step-2-title" ref={(node) => { titleRefs.current[2] = node; }} tabIndex={-1} className="onboarding__title">{t('onboarding.gestureTitle')}</h2>
            <p className="onboarding__subtitle">{t('onboarding.gestureSubtitle')}</p>
            <div className="onboarding__showcase-grid">
              {/* 左侧：鼠标常用手势 */}
              <div className="onboarding__showcase-col">
                <div className="onboarding__showcase-col-header">
                  <Mouse size={16} />
                  <span>{t('onboarding.featureGesture')}</span>
                </div>
                <div className="onboarding__gestures">
                  <div className="onboarding__gesture-card">
                    <span className="onboarding__gesture-arrow">↓</span>
                    <span className="onboarding__gesture-label">{t('onboarding.gestureDown')}</span>
                  </div>
                  <div className="onboarding__gesture-card">
                    <span className="onboarding__gesture-arrow">↑</span>
                    <span className="onboarding__gesture-label">{t('onboarding.gestureUp')}</span>
                  </div>
                  <div className="onboarding__gesture-card">
                    <span className="onboarding__gesture-arrow">←</span>
                    <span className="onboarding__gesture-label">{t('onboarding.gestureLeft')}</span>
                  </div>
                  <div className="onboarding__gesture-card">
                    <span className="onboarding__gesture-arrow">→</span>
                    <span className="onboarding__gesture-label">{t('onboarding.gestureRight')}</span>
                  </div>
                  <div className="onboarding__gesture-card">
                    <span className="onboarding__gesture-arrow">↓ →</span>
                    <span className="onboarding__gesture-label">{t('onboarding.gestureCloseTab')}</span>
                  </div>
                  <div className="onboarding__gesture-card">
                    <span className="onboarding__gesture-arrow">→ ↓</span>
                    <span className="onboarding__gesture-label">{t('onboarding.gestureReopenTab')}</span>
                  </div>
                </div>
              </div>

              {/* 右侧：屏幕演示特效 */}
              <div className="onboarding__showcase-col">
                <div className="onboarding__showcase-col-header">
                  <Highlighter size={16} />
                  <span>{t('onboarding.featureSpotlight')}</span>
                </div>
                <div className="onboarding__spotlight-list">
                  <div className="onboarding__spotlight-item">
                    <div className="onboarding__spotlight-icon-wrap" style={{ background: 'rgba(56, 189, 248, 0.15)', color: '#38bdf8' }}>
                      <Zap size={14} />
                    </div>
                    <span>{t('onboarding.spotlightItem1')}</span>
                  </div>
                  <div className="onboarding__spotlight-item">
                    <div className="onboarding__spotlight-icon-wrap" style={{ background: 'rgba(239, 68, 68, 0.15)', color: '#ef4444' }}>
                      <Highlighter size={14} />
                    </div>
                    <span>{t('onboarding.spotlightItem2')}</span>
                  </div>
                  <div className="onboarding__spotlight-item">
                    <div className="onboarding__spotlight-icon-wrap" style={{ background: 'rgba(52, 211, 153, 0.15)', color: '#34d399' }}>
                      <Sparkles size={14} />
                    </div>
                    <span>{t('onboarding.spotlightItem3')}</span>
                  </div>
                  <div className="onboarding__spotlight-item">
                    <div className="onboarding__spotlight-icon-wrap" style={{ background: 'rgba(167, 139, 250, 0.15)', color: '#a78bfa' }}>
                      <Keyboard size={14} />
                    </div>
                    <span>{t('onboarding.spotlightItem4')}</span>
                  </div>
                </div>
              </div>
            </div>
          </div>

          {/* ── Step 3: 完成 ──────────────────────────────────── */}
          <div className={getStepClass(3)} aria-hidden={step !== 3}>
            <div className="onboarding__complete-icon">
              <Sparkles size={34} />
            </div>
            <h2 id="onboarding-step-3-title" ref={(node) => { titleRefs.current[3] = node; }} tabIndex={-1} className="onboarding__title">{t('onboarding.completeTitle')}</h2>
            <p className="onboarding__subtitle">{t('onboarding.completeSubtitle')}</p>
          </div>
        </div>

        {/* ── 底部导航 ─────────────────────────────────────────── */}
        <div className="onboarding__footer">
          <div className="onboarding__dots">
            {Array.from({ length: TOTAL_STEPS }).map((_, i) => (
              <button
                key={i}
                className={`onboarding__dot ${i === step ? 'onboarding__dot--active' : ''}`}
                onClick={() => setStep(i)}
                aria-label={t('onboarding.stepLabel', { current: i + 1, total: TOTAL_STEPS })}
                aria-current={i === step ? 'step' : undefined}
              />
            ))}
          </div>
          <div className="onboarding__actions">
            {step > 0 && step < TOTAL_STEPS - 1 && (
              <Button variant="ghost" size="sm" onClick={prev}>
                <ChevronLeft size={16} />
                {t('onboarding.prev')}
              </Button>
            )}
            {step < TOTAL_STEPS - 1 ? (
              <Button variant="primary" size="sm" onClick={next}>
                {t('onboarding.next')}
                <ChevronRight size={16} />
              </Button>
            ) : (
              <Button variant="primary" onClick={onComplete}>
                <Check size={16} />
                {t('onboarding.startButton')}
              </Button>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};

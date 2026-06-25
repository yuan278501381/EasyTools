import { useState, useEffect, type FC } from 'react';
import { Card, SettingGroup, Badge, Toggle } from '../components/UIKit';
import { useTranslation } from 'react-i18next';
import { Info, Zap, Layers, Cpu } from 'lucide-react';
import { bridgeRequest } from '../hooks/useBridge';
import './AboutPage.css';

interface DependencyInfo {
  name: string;
  version: string;
  purpose: string;
}

const DEP_PURPOSE_KEYS: string[] = [
  'about.depPurpose.cppStandard',
  'about.depPurpose.webview2',
  'about.depPurpose.react',
  'about.depPurpose.spdlog',
  'about.depPurpose.nlohmannJson',
  'about.depPurpose.luaSol2',
  'about.depPurpose.opencv',
  'about.depPurpose.ffmpeg',
  'about.depPurpose.paddleOcr',
];

const DEPENDENCIES: DependencyInfo[] = [
  { name: 'C++20',          version: 'MSVC 17.x', purpose: DEP_PURPOSE_KEYS[0] },
  { name: 'WebView2',       version: 'Evergreen',  purpose: DEP_PURPOSE_KEYS[1] },
  { name: 'React',          version: '19.x',       purpose: DEP_PURPOSE_KEYS[2] },
  { name: 'spdlog',         version: '1.15+',      purpose: DEP_PURPOSE_KEYS[3] },
  { name: 'nlohmann/json',  version: '3.11+',      purpose: DEP_PURPOSE_KEYS[4] },
  { name: 'Lua 5.4 + sol2', version: '5.4.7',      purpose: DEP_PURPOSE_KEYS[5] },
  { name: 'OpenCV',         version: '4.10+',      purpose: DEP_PURPOSE_KEYS[6] },
  { name: 'FFmpeg',         version: '7.1+',       purpose: DEP_PURPOSE_KEYS[7] },
  { name: 'PaddleOCR',      version: 'Lite',       purpose: DEP_PURPOSE_KEYS[8] },
];

interface PerfMetrics {
  memoryMB: number;
  cpuPercent: number;
  screenshotLatencyMs: number;
  gestureLatencyMs: number;
  uiRenderLatencyMs: number;
}

export const AboutPage: FC = () => {
  const { t } = useTranslation();
  const [geekMode, setGeekMode] = useState(false);
  const [metrics, setMetrics] = useState<PerfMetrics | null>(null);

  useEffect(() => {
    if (!geekMode) {
      setMetrics(null);
      return;
    }
    const fetchMetrics = async () => {
      try {
        const data = await bridgeRequest<PerfMetrics>('perf.getMetrics');
        setMetrics(data);
      } catch (e) {
        console.error('Failed to fetch perf metrics', e);
      }
    };
    fetchMetrics();
    const timer = setInterval(fetchMetrics, 1000);
    return () => clearInterval(timer);
  }, [geekMode]);

  return (
    <div className="about-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('about.title')} icon={<Info size={20} strokeWidth={2.5} />}>
        <Card>
          <div className="about-hero">
            <span className="about-hero__icon"><Zap size={24} fill="var(--primary)" stroke="var(--primary)" /></span>
            <div className="about-hero__info">
              <h2 className="about-hero__title">EasyTools</h2>
              <p className="about-hero__subtitle">{t('about.subtitle')}</p>
              <div className="about-hero__badges">
                <Badge text="v0.1.0" variant="primary" />
                <Badge text="Windows 10+" variant="muted" />
                <Badge text="C++ & React" variant="success" />
              </div>
            </div>
          </div>
          <p className="about-desc">
            {t('about.description')}
          </p>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('about.techStack')} icon={<Layers size={20} strokeWidth={2.5} />}>
        <Card>
          <div className="about-deps">
            {DEPENDENCIES.map((dep) => (
              <div key={dep.name} className="about-dep-item">
                <span className="about-dep-name">{dep.name}</span>
                <Badge text={dep.version} variant="muted" />
                <span className="about-dep-purpose">{t(dep.purpose as any)}</span>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('about.geekMode' as any, '极客模式')} icon={<Cpu size={20} strokeWidth={2.5} />}>
        <Card>
          <Toggle
            id="geek-mode-toggle"
            label={t('about.enablePerfMonitor' as any, '开启性能监控')}
            description={t('about.enablePerfMonitorDesc' as any, '实时显示内存、CPU以及子系统延迟')}
            checked={geekMode}
            onChange={setGeekMode}
          />
          {geekMode && metrics && (
            <div className="about-perf-panel" style={{ marginTop: '1rem', padding: '1rem', background: 'var(--bg-secondary)', borderRadius: '8px' }}>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1rem' }}>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfMemory' as any, '内存使用')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.memoryMB.toFixed(1)} MB</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfCpu' as any, 'CPU 使用率')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.cpuPercent.toFixed(1)} %</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfGestureLatency' as any, '手势延迟')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.gestureLatencyMs.toFixed(1)} ms</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfUiLatency' as any, 'UI 渲染延迟')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.uiRenderLatencyMs.toFixed(1)} ms</div>
                </div>
              </div>
            </div>
          )}
        </Card>
      </SettingGroup>
    </div>
  );
};

import { useState, useEffect, type FC } from 'react';
import { Card, SettingGroup, Badge, Toggle, Button } from '../components/UIKit';
import { useTranslation } from 'react-i18next';
import { ExternalLink, Info, Zap, Layers, Cpu, RefreshCw } from 'lucide-react';
import { bridgeRequest, useBridgeEvent } from '../hooks/useBridge';
import { toast } from 'sonner';
import './AboutPage.css';

interface DependencyInfo {
  name: string;
  version: string;
  purpose: typeof DEP_PURPOSE_KEYS[number];
}

const DEP_PURPOSE_KEYS = [
  'about.depPurpose.cppStandard',
  'about.depPurpose.webview2',
  'about.depPurpose.react',
  'about.depPurpose.spdlog',
  'about.depPurpose.nlohmannJson',
  'about.depPurpose.luaSol2',
  'about.depPurpose.opencv',
  'about.depPurpose.ffmpeg',
  'about.depPurpose.windowsOcr',
] as const;

const DEPENDENCIES: DependencyInfo[] = [
  { name: 'C++20',          version: 'MSVC 17.x', purpose: DEP_PURPOSE_KEYS[0] },
  { name: 'WebView2',       version: 'Evergreen',  purpose: DEP_PURPOSE_KEYS[1] },
  { name: 'React',          version: '19.x',       purpose: DEP_PURPOSE_KEYS[2] },
  { name: 'spdlog',         version: '1.15+',      purpose: DEP_PURPOSE_KEYS[3] },
  { name: 'nlohmann/json',  version: '3.11+',      purpose: DEP_PURPOSE_KEYS[4] },
  { name: 'Lua 5.4 + sol2', version: '5.4.7',      purpose: DEP_PURPOSE_KEYS[5] },
  { name: 'OpenCV',         version: '4.10+',      purpose: DEP_PURPOSE_KEYS[6] },
  { name: 'FFmpeg',         version: '7.1+',       purpose: DEP_PURPOSE_KEYS[7] },
  { name: 'Windows.Media.Ocr', version: 'System',  purpose: DEP_PURPOSE_KEYS[8] },
];

interface PerfMetrics {
  memoryMB: number;
  cpuPercent: number;
  screenshotLatencyMs: number;
  gestureLatencyMs: number;
  uiRenderLatencyMs: number;
}

interface UpdateResult {
  status: 'available' | 'upToDate' | 'unavailable' | 'error';
  currentVersion?: string;
  latestVersion?: string;
  releaseUrl?: string;
  error?: string;
}

const safeMetric = (value: unknown) =>
  typeof value === 'number' && Number.isFinite(value) ? value : 0;

export const AboutPage: FC = () => {
  const { t } = useTranslation();
  const [geekMode, setGeekMode] = useState(false);
  const [metrics, setMetrics] = useState<PerfMetrics | null>(null);
  const [version, setVersion] = useState('1.0.0');
  const [checkingUpdate, setCheckingUpdate] = useState(false);
  const [updateResult, setUpdateResult] = useState<UpdateResult | null>(null);

  useBridgeEvent('update.result', (data) => {
    if (!data || typeof data !== 'object') return;
    const result = data as UpdateResult;
    if (!['available', 'upToDate', 'unavailable', 'error'].includes(result.status)) return;
    setCheckingUpdate(false);
    setUpdateResult(result);
    if (result.status === 'available') {
      toast.success(t('about.updateAvailable', { version: result.latestVersion ?? '' }));
    } else if (result.status === 'upToDate') {
      toast.success(t('about.upToDate'));
    } else if (result.status === 'unavailable') {
      toast.info(t('about.noPublishedRelease'));
    } else {
      toast.error(t('about.updateFailed'));
    }
  });

  useEffect(() => {
    void bridgeRequest<{ version?: unknown }>('app.getSystemInfo')
      .then((info) => {
        if (typeof info.version === 'string' && info.version.trim()) setVersion(info.version);
      })
      .catch(() => undefined);
  }, []);

  useEffect(() => {
    if (!geekMode) return;
    let active = true;
    const fetchMetrics = async () => {
      try {
        const data = await bridgeRequest<PerfMetrics>('perf.getMetrics');
        if (active) setMetrics({
          memoryMB: safeMetric(data?.memoryMB),
          cpuPercent: safeMetric(data?.cpuPercent),
          screenshotLatencyMs: safeMetric(data?.screenshotLatencyMs),
          gestureLatencyMs: safeMetric(data?.gestureLatencyMs),
          uiRenderLatencyMs: safeMetric(data?.uiRenderLatencyMs),
        });
      } catch (e) {
        console.error('Failed to fetch perf metrics', e);
      }
    };
    fetchMetrics();
    const timer = setInterval(fetchMetrics, 1000);
    return () => {
      active = false;
      clearInterval(timer);
    };
  }, [geekMode]);

  const checkForUpdates = async () => {
    if (checkingUpdate) return;
    setCheckingUpdate(true);
    try {
      const response = await bridgeRequest<{ success: boolean; started?: boolean }>('app.checkForUpdates');
      if (!response.success) throw new Error('check failed');
      if (response.started === false) {
        setCheckingUpdate(false);
        toast.info(t('about.updateCheckBusy'));
      }
    } catch {
      setCheckingUpdate(false);
      toast.error(t('about.updateFailed'));
    }
  };

  const openReleasePage = async () => {
    const url = updateResult?.releaseUrl;
    if (!url?.startsWith('https://github.com/yuan278501381/easyTools/')) return;
    const response = await bridgeRequest<{ success: boolean }>('system.openFile', { path: url });
    if (!response.success) toast.error(t('about.openReleaseFailed'));
  };

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
                <Badge text={`v${version}`} variant="primary" />
                <Badge text="Windows 10+" variant="muted" />
                <Badge text="C++ & React" variant="success" />
              </div>
            </div>
          </div>
          <p className="about-desc">
            {t('about.description')}
          </p>
          <div className="about-update-row">
            <Button variant="ghost" onClick={() => void checkForUpdates()} disabled={checkingUpdate}>
              <RefreshCw size={16} className={checkingUpdate ? 'about-update-spin' : undefined} />
              <span>{checkingUpdate ? t('about.checkingUpdate') : t('about.checkUpdate')}</span>
            </Button>
            {updateResult?.status === 'available' && updateResult.releaseUrl && (
              <Button variant="primary" onClick={() => void openReleasePage()}>
                <ExternalLink size={16} />
                <span>{t('about.openRelease')}</span>
              </Button>
            )}
          </div>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('about.techStack')} icon={<Layers size={20} strokeWidth={2.5} />}>
        <Card>
          <div className="about-deps">
            {DEPENDENCIES.map((dep) => (
              <div key={dep.name} className="about-dep-item">
                <span className="about-dep-name">{dep.name}</span>
                <Badge text={dep.version} variant="muted" />
                <span className="about-dep-purpose">{t(dep.purpose)}</span>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('about.geekMode')} icon={<Cpu size={20} strokeWidth={2.5} />}>
        <Card>
          <Toggle
            id="geek-mode-toggle"
            label={t('about.enablePerfMonitor')}
            description={t('about.enablePerfMonitorDesc')}
            checked={geekMode}
            onChange={(enabled) => {
              setGeekMode(enabled);
              if (!enabled) setMetrics(null);
            }}
          />
          {geekMode && metrics && (
            <div className="about-perf-panel" style={{ marginTop: '1rem', padding: '1rem', background: 'var(--bg-secondary)', borderRadius: '8px' }}>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1rem' }}>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfMemory')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.memoryMB.toFixed(1)} MB</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfCpu')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.cpuPercent.toFixed(1)} %</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfGestureLatency')}</div>
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{metrics.gestureLatencyMs.toFixed(1)} ms</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfUiLatency')}</div>
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

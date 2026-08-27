import { useState, useEffect, type FC } from 'react';
import { Card, SettingGroup, Badge, Toggle, Button } from '../components/UIKit';
import { useTranslation } from 'react-i18next';
import { ExternalLink, Info, Layers, Cpu, RefreshCw, User, ShieldCheck, Download } from 'lucide-react';
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
  'about.depPurpose.ntfsEngine',
  'about.depPurpose.pinyin',
  'about.depPurpose.direct2d',
  'about.depPurpose.llHooks',
  'about.depPurpose.gtest',
  'about.depPurpose.vite',
  'about.depPurpose.innoSetup',
] as const;

const DEPENDENCIES: DependencyInfo[] = [
  { name: 'C++20',                 version: 'MSVC 19.4x', purpose: DEP_PURPOSE_KEYS[0] },
  { name: 'NTFS MFT / USN',        version: 'Win32 API',  purpose: DEP_PURPOSE_KEYS[9] },
  { name: 'PinyinEngine',          version: 'Native',     purpose: DEP_PURPOSE_KEYS[10] },
  { name: 'Win32 LL Hooks',        version: 'Native',     purpose: DEP_PURPOSE_KEYS[12] },
  { name: 'Direct2D & GDI+',       version: 'Hardware',   purpose: DEP_PURPOSE_KEYS[11] },
  { name: 'WebView2',              version: 'Evergreen',  purpose: DEP_PURPOSE_KEYS[1] },
  { name: 'React & TypeScript',    version: '19.x / 5.8+',purpose: DEP_PURPOSE_KEYS[2] },
  { name: 'Vite',                  version: '6.x',        purpose: DEP_PURPOSE_KEYS[14] },
  { name: 'OpenCV',                version: '4.12+',      purpose: DEP_PURPOSE_KEYS[6] },
  { name: 'FFmpeg',                version: '8.x',        purpose: DEP_PURPOSE_KEYS[7] },
  { name: 'Windows.Media.Ocr',     version: 'System',     purpose: DEP_PURPOSE_KEYS[8] },
  { name: 'Lua 5.4 + sol2',        version: '5.4.7',      purpose: DEP_PURPOSE_KEYS[5] },
  { name: 'spdlog',                version: '1.17+',      purpose: DEP_PURPOSE_KEYS[3] },
  { name: 'nlohmann/json',         version: '3.12+',      purpose: DEP_PURPOSE_KEYS[4] },
  { name: 'Google Test (GTest)',   version: '1.15+',      purpose: DEP_PURPOSE_KEYS[13] },
  { name: 'Inno Setup',            version: '6.7+',       purpose: DEP_PURPOSE_KEYS[15] },
];

interface PerfMetrics {
  memoryMB: number;
  privateMemoryMB: number;
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
  const [version, setVersion] = useState(__EASYTOOLS_VERSION__);
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
          privateMemoryMB: safeMetric(data?.privateMemoryMB),
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

  const handleExportLogs = async () => {
    try {
      const res = await bridgeRequest<{ success: boolean; cancelled?: boolean; error?: string }>('app.exportLogs');
      if (res.cancelled) return;
      if (!res.success) throw new Error(res.error || '导出失败');
      toast.success(t('about.exportLogsSuccess', 'Diagnostic logs exported and highlighted successfully'));
    } catch (e) {
      toast.error(t('about.exportLogsFailed', 'Failed to export diagnostic logs'), { description: String(e) });
    }
  };

  return (
    <div className="about-page" style={{ animation: 'fadeIn 0.3s ease' }}>

      <div className="about-grand-showcase">
        <div className="about-grand-showcase__aura"></div>
        <div className="about-grand-showcase__logo-container">
          <img
            src="/Logo_Origin.png"
            width={180}
            height={180}
            className="about-grand-showcase__logo"
            alt="EasyTools"
            draggable={false}
            decoding="async"
          />
        </div>
        <div className="about-grand-showcase__text">
          <div className="about-grand-showcase__title-row">
            <h1 className="about-grand-showcase__title">EasyTools</h1>
            <span className="about-grand-showcase__version">v{version}</span>
          </div>
          <p className="about-grand-showcase__subtitle">The Ultimate Windows Productivity Suite</p>
        </div>
      </div>

      <SettingGroup title={t('about.title')} icon={<Info size={20} strokeWidth={2.5} />}>
        <Card className="about-card-hero">
          <div className="about-hero">
            <div className="about-hero__brand">
              
              <div className="about-hero__info">
                <div className="about-hero__title-row">
                  <h2 className="about-hero__title">EasyTools</h2>
                  <Badge text={`v${version}`} variant="primary" />
                  <Badge text="C++20 & Direct2D" variant="success" />
                  <Badge text={`作者 · Yy1 (@yuan278501381)`} variant="muted" />
                </div>
                <p className="about-hero__subtitle">{t('about.subtitle')}</p>
              </div>
            </div>

            <div className="about-hero__actions">
              <Button variant="secondary" onClick={() => void checkForUpdates()} disabled={checkingUpdate}>
                <RefreshCw size={14} className={checkingUpdate ? 'about-update-spin' : undefined} />
                <span>{checkingUpdate ? t('about.checkingUpdate') : t('about.checkUpdate')}</span>
              </Button>
              <Button variant="secondary" onClick={() => void handleExportLogs()} title="导出系统诊断日志与环境报告">
                <Download size={14} />
                <span>{t('about.exportLogs', 'Export Logs')}</span>
              </Button>
              {updateResult?.status === 'available' && updateResult.releaseUrl && (
                <Button variant="primary" onClick={() => void openReleasePage()}>
                  <ExternalLink size={14} />
                  <span>{t('about.openRelease')}</span>
                </Button>
              )}
            </div>
          </div>

          <div className="about-desc-box">
            <p className="about-desc">
              {t('about.description')}
            </p>
          </div>

          <div className="about-meta-row">
            <div className="about-meta-item">
              <User size={14} className="about-meta-icon" />
              <span className="about-meta-label">{t('about.author')}:</span>
              <a
                href="https://github.com/yuan278501381"
                className="about-meta-link"
                onClick={(e) => {
                  e.preventDefault();
                  void bridgeRequest('system.openFile', { path: 'https://github.com/yuan278501381' });
                }}
                title="访问原作者 GitHub 主页"
              >
                <strong className="about-meta-val">Yy1</strong>
                <span className="about-meta-handle">(@yuan278501381)</span>
                <ExternalLink size={12} className="about-meta-ext" />
              </a>
            </div>
            <div className="about-meta-item">
              <ShieldCheck size={14} className="about-meta-icon" />
              <span className="about-meta-label">{t('about.license')}:</span>
              <strong className="about-meta-val">MIT License</strong>
            </div>
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
                  <div style={{ fontSize: '1.25rem', fontWeight: 600 }}>{((metrics.privateMemoryMB && metrics.privateMemoryMB > 0) ? metrics.privateMemoryMB : metrics.memoryMB).toFixed(1)} MB</div>
                </div>
                <div>
                  <div style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>{t('about.perfWorkingSet')}</div>
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
              </div>
            </div>
          )}
        </Card>
      </SettingGroup>

      <div className="about-footer-copyright">
        <p>Copyright © 2026 <strong>Yy1 (GitHub yuan278501381)</strong> & EasyTools Contributors.</p>
        <p className="about-footer-sub">Licensed under the <strong>MIT License</strong>. Designed with passion for extreme productivity.</p>
      </div>
    </div>
  );
};

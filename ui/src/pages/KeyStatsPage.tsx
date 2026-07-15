import { useEffect, useState, useMemo, type FC } from 'react';
import { Card, SettingGroup, Badge } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { KeyboardHeatmap } from '../components/KeyboardHeatmap';
import { LineChart as LineChartIcon, Flame, Trophy } from 'lucide-react';
import './KeyStatsPage.css';

interface DailyStat {
  date: string;
  keystrokes: number;
  mouseClicks: number;
  mouseDistance: number;
  keyMap: Record<number, number>;
}

interface TotalStat {
  totalKeystrokes: number;
}

interface HistoryStat {
  totalKeys?: number;
  leftClicks?: number;
  rightClicks?: number;
  mouseDistance?: number;
  keyMap?: Record<number, number>;
}

const safeCount = (value: unknown): number =>
  typeof value === 'number' && Number.isFinite(value) && value >= 0 ? value : 0;

const localDateKey = (date = new Date()): string => {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  return `${year}-${month}-${day}`;
};

const normalizeKeyMap = (value: unknown): Record<number, number> => {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return {};

  return Object.fromEntries(
    Object.entries(value)
      .map(([key, count]) => [Number(key), safeCount(count)] as const)
      .filter(([key]) => Number.isInteger(key) && key >= 0 && key <= 255)
  );
};

interface ChartPoint {
  date: string;
  keys: number;
}

const ActivityChart: FC<{ data: ChartPoint[]; label: string }> = ({ data, label }) => {
  const width = 720;
  const height = 260;
  const padding = { top: 18, right: 20, bottom: 36, left: 54 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;
  const rawMax = Math.max(1, ...data.map(item => item.keys));
  const magnitude = 10 ** Math.max(0, Math.floor(Math.log10(rawMax)) - 1);
  const yMax = Math.max(magnitude * 4, Math.ceil(rawMax / (magnitude * 4)) * magnitude * 4);
  const xAt = (index: number) => padding.left + (data.length <= 1 ? plotWidth / 2 : index * plotWidth / (data.length - 1));
  const yAt = (value: number) => padding.top + plotHeight - (value / yMax) * plotHeight;
  const points = data.map((item, index) => `${xAt(index)},${yAt(item.keys)}`).join(' ');
  const areaPoints = data.length > 0
    ? `${padding.left},${padding.top + plotHeight} ${points} ${xAt(data.length - 1)},${padding.top + plotHeight}`
    : '';
  const yTicks = Array.from({ length: 5 }, (_, index) => yMax * index / 4);

  return (
    <svg className="activity-chart" viewBox={`0 0 ${width} ${height}`} role="img" aria-label={label}>
      <defs>
        <linearGradient id="activity-area-gradient" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#8b5cf6" stopOpacity="0.42" />
          <stop offset="100%" stopColor="#8b5cf6" stopOpacity="0.03" />
        </linearGradient>
      </defs>
      {yTicks.map(value => {
        const y = yAt(value);
        return (
          <g key={value}>
            <line className="activity-chart__grid" x1={padding.left} x2={width - padding.right} y1={y} y2={y} />
            <text className="activity-chart__axis" x={padding.left - 10} y={y + 4} textAnchor="end">
              {Math.round(value).toLocaleString()}
            </text>
          </g>
        );
      })}
      {areaPoints && <polygon points={areaPoints} fill="url(#activity-area-gradient)" />}
      {points && <polyline className="activity-chart__line" points={points} />}
      {data.map((item, index) => (
        <g key={`${item.date}-${index}`}>
          <circle className="activity-chart__hit" cx={xAt(index)} cy={yAt(item.keys)} r="14">
            <title>{`${item.date}: ${item.keys.toLocaleString()} ${label}`}</title>
          </circle>
          <circle className="activity-chart__point" cx={xAt(index)} cy={yAt(item.keys)} r="4" />
          <text className="activity-chart__axis" x={xAt(index)} y={height - 10} textAnchor="middle">{item.date}</text>
        </g>
      ))}
    </svg>
  );
};

export const KeyStatsPage: FC = () => {
  const [dailyStats, setDailyStats] = useState<DailyStat[]>([]);
  const [totalStats, setTotalStats] = useState<TotalStat>({ totalKeystrokes: 0 });
  const [loading, setLoading] = useState(true);
  const { t } = useTranslation();

  const chartData = useMemo(() => dailyStats.map(s => ({
    date: s.date.split('-').slice(1).join('/'),
    keys: s.keystrokes
  })), [dailyStats]);

  const todayStats = useMemo(() => {
    const today = localDateKey();
    return dailyStats.find(s => s.date === today) || { keystrokes: 0, mouseClicks: 0, mouseDistance: 0, keyMap: {} };
  }, [dailyStats]);

  const totalOverview = useMemo(() => ({
    totalKeystrokes: totalStats.totalKeystrokes
  }), [totalStats]);

  useEffect(() => {
    Promise.all([
      bridgeRequest<Record<string, HistoryStat>>('stats.getHistory', { days: 7 }),
      bridgeRequest<TotalStat>('stats.getTotal')
    ]).then(([historyMap, total]) => {
      const history = historyMap && typeof historyMap === 'object' && !Array.isArray(historyMap)
        ? historyMap
        : {};
      const daily = Object.entries(history).map(([date, data]) => ({
        date,
        keystrokes: safeCount(data?.totalKeys),
        mouseClicks: safeCount(data?.leftClicks) + safeCount(data?.rightClicks),
        mouseDistance: safeCount(data?.mouseDistance),
        keyMap: normalizeKeyMap(data?.keyMap)
      })).sort((a, b) => a.date.localeCompare(b.date));

      setDailyStats(daily);
      setTotalStats({ totalKeystrokes: safeCount(total?.totalKeystrokes) });
      setLoading(false);
    }).catch(err => {
      console.error('Failed to load stats:', err);
      setLoading(false);
    });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading')}</div>;
  }

  return (
    <div className="stats-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('stats.dailyTitle')} icon={<LineChartIcon size={20} strokeWidth={2.5} />}>
        <Card>
          <div style={{ height: '300px', width: '100%', marginTop: '16px' }}>
            <ActivityChart data={chartData} label={t('stats.keys')} />
          </div>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('stats.heatmapTitle')} icon={<Flame size={20} strokeWidth={2.5} />}>
        <KeyboardHeatmap keyMap={todayStats.keyMap || {}} />
      </SettingGroup>

      <SettingGroup title={t('stats.totalOverview')} icon={<Trophy size={20} strokeWidth={2.5} />}>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: '16px' }}>
          <Card>
            <div style={{ padding: '16px', textAlign: 'center' }}>
              <div style={{ fontSize: '2rem', fontWeight: 700, color: 'var(--primary)' }}>{totalOverview.totalKeystrokes.toLocaleString()}</div>
              <div style={{ fontSize: '0.9rem', color: 'var(--text-muted)', marginTop: '8px' }}>{t('stats.allTimeKeys')}</div>
            </div>
          </Card>
          <Card>
            <div style={{ padding: '16px', textAlign: 'center' }}>
              <div style={{ fontSize: '2rem', fontWeight: 700, color: '#34d399' }}>{todayStats.mouseClicks.toLocaleString()}</div>
              <div style={{ fontSize: '0.9rem', color: 'var(--text-muted)', marginTop: '8px' }}>{t('stats.mouseClicks')}</div>
            </div>
          </Card>
          <Card>
            <div style={{ padding: '16px', textAlign: 'center' }}>
              <div style={{ fontSize: '2rem', fontWeight: 700, color: '#60a5fa' }}>{Math.round(todayStats.mouseDistance).toLocaleString()}</div>
              <div style={{ fontSize: '0.9rem', color: 'var(--text-muted)', marginTop: '8px' }}>{t('stats.mouseDistance')} ({t('stats.pixels')})</div>
            </div>
          </Card>
        </div>
      </SettingGroup>

      <div style={{ textAlign: 'center', marginTop: '2rem' }}>
        <p style={{ color: 'var(--text-muted)', fontSize: '0.9rem' }}>
          {t('stats.allTimeKeysSummary')}: <Badge text={totalStats.totalKeystrokes.toLocaleString() + ' ' + t('stats.times')} variant="primary" />
        </p>
      </div>
    </div>
  );
};

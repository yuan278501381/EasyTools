import { useEffect, useState, useMemo, type FC } from 'react';
import { Card, SettingGroup, Badge } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { useTranslation } from 'react-i18next';
import { KeyboardHeatmap } from '../components/KeyboardHeatmap';
import { LineChart as LineChartIcon, Flame, Trophy } from 'lucide-react';

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
    const today = new Date().toISOString().split('T')[0];
    return dailyStats.find(s => s.date === today) || { keystrokes: 0, mouseClicks: 0, mouseDistance: 0, keyMap: {} };
  }, [dailyStats]);

  const totalOverview = useMemo(() => ({
    totalKeystrokes: totalStats.totalKeystrokes
  }), [totalStats]);

  useEffect(() => {
    Promise.all([
      bridgeRequest<DailyStat[]>('stats.getDaily', { days: 7 }),
      bridgeRequest<TotalStat>('stats.getTotal')
    ]).then(([daily, total]) => {
      setDailyStats(daily);
      setTotalStats(total);
      setLoading(false);
    });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading' as any, '加载数据中...')}</div>;
  }

  return (
    <div className="stats-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('stats.dailyTitle')} icon={<LineChartIcon size={20} strokeWidth={2.5} />}>
        <Card>
          <div style={{ height: '300px', width: '100%', marginTop: '16px' }}>
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={chartData} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                <defs>
                  <linearGradient id="colorKeys" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="#8b5cf6" stopOpacity={0.8}/>
                    <stop offset="95%" stopColor="#8b5cf6" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <XAxis dataKey="date" stroke="var(--text-muted)" fontSize={12} tickLine={false} axisLine={false} />
                <YAxis stroke="var(--text-muted)" fontSize={12} tickLine={false} axisLine={false} />
                <CartesianGrid strokeDasharray="3 3" stroke="var(--card-border)" vertical={false} />
                <Tooltip
                  contentStyle={{ backgroundColor: 'var(--bg-elevated)', border: '1px solid var(--card-border)', borderRadius: '8px' }}
                  itemStyle={{ color: 'var(--text-primary)' }}
                />
                <Area type="monotone" dataKey="keys" name={t('stats.keys')} stroke="#8b5cf6" fillOpacity={1} fill="url(#colorKeys)" />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('stats.heatmapTitle' as any, '按键热力图')} icon={<Flame size={20} strokeWidth={2.5} />}>
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
          历史累计按键总数: <Badge text={totalStats.totalKeystrokes.toLocaleString() + ' 次'} variant="primary" />
        </p>
      </div>
    </div>
  );
};

import { useState, useEffect, type FC } from 'react';
import { Card, SettingGroup, Badge } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { Activity, MousePointer2, Keyboard, Navigation } from 'lucide-react';

interface DailyStat {
  date: string;
  keystrokes: number;
  mouseClicks: number;
  mouseDistance: number;
}

interface TotalStat {
  totalKeystrokes: number;
}

export const KeyStatsPage: FC = () => {
  const [dailyStats, setDailyStats] = useState<DailyStat[]>([]);
  const [totalStats, setTotalStats] = useState<TotalStat>({ totalKeystrokes: 0 });
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    Promise.all([
      bridgeRequest<DailyStat[]>('stats.getDaily', { days: 7 }),
      bridgeRequest<TotalStat>('stats.getTotal')
    ]).then(([daily, total]) => {
      // 补充缺失的日期，保证图表连续（真实环境中建议在后端补全）
      setDailyStats(daily);
      setTotalStats(total);
      setLoading(false);
    });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>加载数据中...</div>;
  }

  // 计算今日数据
  const today = new Date().toISOString().split('T')[0];
  const todayStat = dailyStats.find(s => s.date === today) || { keystrokes: 0, mouseClicks: 0, mouseDistance: 0 };

  return (
    <div className="stats-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title="今日总览" icon="📈">
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: 'var(--spacing-md)' }}>
          <Card>
            <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
              <div style={{ padding: '12px', background: 'var(--primary-dim)', borderRadius: '12px', color: 'var(--primary)' }}>
                <Keyboard size={24} />
              </div>
              <div>
                <p style={{ color: 'var(--text-muted)', fontSize: '0.85rem' }}>今日按键</p>
                <h2 style={{ fontSize: '1.8rem', fontWeight: 700, margin: 0 }}>{todayStat.keystrokes} <span style={{ fontSize: '0.9rem', color: 'var(--text-muted)', fontWeight: 400 }}>次</span></h2>
              </div>
            </div>
          </Card>

          <Card>
            <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
              <div style={{ padding: '12px', background: 'rgba(52, 211, 153, 0.15)', borderRadius: '12px', color: 'var(--success)' }}>
                <MousePointer2 size={24} />
              </div>
              <div>
                <p style={{ color: 'var(--text-muted)', fontSize: '0.85rem' }}>鼠标点击</p>
                <h2 style={{ fontSize: '1.8rem', fontWeight: 700, margin: 0 }}>{todayStat.mouseClicks} <span style={{ fontSize: '0.9rem', color: 'var(--text-muted)', fontWeight: 400 }}>次</span></h2>
              </div>
            </div>
          </Card>

          <Card>
            <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
              <div style={{ padding: '12px', background: 'rgba(96, 165, 250, 0.15)', borderRadius: '12px', color: 'var(--info)' }}>
                <Navigation size={24} />
              </div>
              <div>
                <p style={{ color: 'var(--text-muted)', fontSize: '0.85rem' }}>鼠标移动</p>
                <h2 style={{ fontSize: '1.8rem', fontWeight: 700, margin: 0 }}>{(todayStat.mouseDistance / 1000).toFixed(1)} <span style={{ fontSize: '0.9rem', color: 'var(--text-muted)', fontWeight: 400 }}>米</span></h2>
              </div>
            </div>
          </Card>
        </div>
      </SettingGroup>

      <SettingGroup title="最近 7 天按键趋势" icon={<Activity size={18} />}>
        <Card>
          <div style={{ height: '300px', width: '100%', marginTop: '1rem' }}>
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={dailyStats} margin={{ top: 10, right: 10, left: -20, bottom: 0 }}>
                <defs>
                  <linearGradient id="colorKeystrokes" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="var(--primary)" stopOpacity={0.3}/>
                    <stop offset="95%" stopColor="var(--primary)" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="var(--card-border)" />
                <XAxis 
                  dataKey="date" 
                  axisLine={false} 
                  tickLine={false} 
                  tick={{ fill: 'var(--text-muted)', fontSize: 12 }} 
                  tickFormatter={(val) => val.split('-').slice(1).join('/')}
                  dy={10}
                />
                <YAxis 
                  axisLine={false} 
                  tickLine={false} 
                  tick={{ fill: 'var(--text-muted)', fontSize: 12 }} 
                />
                <Tooltip 
                  contentStyle={{ backgroundColor: 'var(--card-bg)', border: '1px solid var(--card-border)', borderRadius: '8px', color: 'var(--text-primary)' }}
                  itemStyle={{ color: 'var(--primary)', fontWeight: 600 }}
                  labelStyle={{ color: 'var(--text-muted)', marginBottom: '4px' }}
                />
                <Area 
                  type="monotone" 
                  dataKey="keystrokes" 
                  name="按键次数"
                  stroke="var(--primary)" 
                  strokeWidth={3}
                  fillOpacity={1} 
                  fill="url(#colorKeystrokes)" 
                />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </Card>
      </SettingGroup>

      <div style={{ textAlign: 'center', marginTop: '2rem' }}>
        <p style={{ color: 'var(--text-muted)', fontSize: '0.9rem' }}>
          历史累计按键总数: <Badge text={totalStats.totalKeystrokes.toLocaleString() + ' 次'} variant="primary" />
        </p>
      </div>
    </div>
  );
};

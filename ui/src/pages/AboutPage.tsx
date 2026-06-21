/* ─────────────────────────────────────────────────────────────────────────────
 * AboutPage — 关于页
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC } from 'react';
import { Card, SettingGroup, Badge } from '../components/UIKit';
import { Info } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import './AboutPage.css';

interface DependencyInfo {
  name: string;
  version: string;
  purpose: string;
}

const DEPENDENCIES: DependencyInfo[] = [
  { name: 'C++20',          version: 'MSVC 17.x', purpose: '核心语言标准' },
  { name: 'WebView2',       version: 'Evergreen',  purpose: '设置界面渲染' },
  { name: 'React',          version: '19.x',       purpose: '前端 UI 框架' },
  { name: 'spdlog',         version: '1.15+',      purpose: '高性能日志' },
  { name: 'nlohmann/json',  version: '3.11+',      purpose: 'JSON 处理' },
  { name: 'Lua 5.4 + sol2', version: '5.4.7',      purpose: '脚本扩展' },
  { name: 'OpenCV',         version: '4.10+',      purpose: '图像处理/拼接' },
  { name: 'FFmpeg',         version: '7.1+',       purpose: '视频编码/录屏' },
  { name: 'PaddleOCR',      version: 'Lite',       purpose: '文字识别' },
];

export const AboutPage: FC = () => {
  const { t } = useTranslation();
  return (
    <div className="about-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('about.title')} icon={<Info size={18} />}>
        <Card>
          <div className="about-hero">
            <span className="about-hero__icon">⚡</span>
            <div className="about-hero__info">
              <h2 className="about-hero__title">EasyTools</h2>
              <p className="about-hero__subtitle">桌面效率工具</p>
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

      <SettingGroup title={t('about.techStack' as any)} icon="🧱">
        <Card>
          <div className="about-deps">
            {DEPENDENCIES.map((dep) => (
              <div key={dep.name} className="about-dep-item">
                <span className="about-dep-name">{dep.name}</span>
                <Badge text={dep.version} variant="muted" />
                <span className="about-dep-purpose">{dep.purpose}</span>
              </div>
            ))}
          </div>
        </Card>
      </SettingGroup>
    </div>
  );
};

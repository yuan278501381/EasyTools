/* ─────────────────────────────────────────────────────────────────────────────
 * AboutPage — 关于页
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC } from 'react';
import { Card, SettingGroup, Badge } from '../components/UIKit';
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

export const AboutPage: FC = () => (
  <div className="about-page" style={{ animation: 'fadeIn 0.3s ease' }}>
    <SettingGroup title="关于 EasyTools" icon="ℹ️">
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
          EasyTools 是一款高性能桌面效率工具，集成鼠标手势、截图贴图、
          屏幕录制、OCR 文字识别等功能。采用 C++ 核心引擎 + WebView2 设置界面架构，
          追求极致性能与优雅体验的统一。
        </p>
      </Card>
    </SettingGroup>

    <SettingGroup title="技术栈" icon="🧱">
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

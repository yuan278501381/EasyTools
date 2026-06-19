/* ─────────────────────────────────────────────────────────────────────────────
 * GeneralPage — 通用设置页
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';

export const GeneralPage: FC = () => {
  const [autoStart, setAutoStart] = useState(true);
  const [minimizeToTray, setMinimizeToTray] = useState(true);
  const [showNotification, setShowNotification] = useState(true);
  const [language, setLanguage] = useState('zh-CN');
  const [logLevel, setLogLevel] = useState('info');

  return (
    <div className="general-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title="启动与运行" icon="🚀">
        <Card>
          <Toggle
            id="auto-start"
            label="开机自动启动"
            description="设置 EasyTools 在系统启动时自动运行"
            checked={autoStart}
            onChange={setAutoStart}
          />
          <Toggle
            id="minimize-to-tray"
            label="关闭窗口时最小化到托盘"
            description="关闭设置窗口不会退出程序，而是最小化到系统托盘"
            checked={minimizeToTray}
            onChange={setMinimizeToTray}
          />
          <Toggle
            id="show-notification"
            label="显示气泡通知"
            description="操作完成时在系统托盘显示气泡通知"
            checked={showNotification}
            onChange={setShowNotification}
          />
        </Card>
      </SettingGroup>

      <SettingGroup title="界面与语言" icon="🌐">
        <Card>
          <SettingRow label="界面语言" description="设置界面显示的语言">
            <Select
              id="language"
              value={language}
              onChange={setLanguage}
              options={[
                { value: 'zh-CN', label: '简体中文' },
                { value: 'en-US', label: 'English' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>

      <SettingGroup title="高级设置" icon="🔧">
        <Card>
          <SettingRow label="日志级别" description="控制日志输出的详细程度">
            <Select
              id="log-level"
              value={logLevel}
              onChange={setLogLevel}
              options={[
                { value: 'trace', label: 'Trace' },
                { value: 'debug', label: 'Debug' },
                { value: 'info', label: 'Info' },
                { value: 'warn', label: 'Warning' },
                { value: 'error', label: 'Error' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};

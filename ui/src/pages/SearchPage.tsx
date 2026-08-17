/* ─────────────────────────────────────────────────────────────────────────────
 * SearchPage.tsx — 文件搜索配置与语法手册
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select, Button, Tabs, type TabItem } from '../components/UIKit';
import { HotkeyRecorder } from '../components/HotkeyRecorder';
import { HotkeyStatusBadge, type HotkeyEntry } from '../components/HotkeyStatusBadge';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import { useTranslation } from 'react-i18next';
import {
  Search,
  HardDrive,
  Code2,
  BookOpen,
  CheckCircle2,
  FolderSearch,
  Sparkles,
  HelpCircle,
  Play,
  RotateCw
} from 'lucide-react';
import './SearchPage.css';

interface SearchSettings {
  hotkey: string;
  maxResults: number;
  defaultCategory: string;
  caseSensitive: boolean;
  matchPath: boolean;
  pinyinEnabled: boolean;
}

interface ServiceStatus {
  available: boolean;
  pipeName: string;
}

export const SearchPage: FC = () => {
  const { t } = useTranslation();
  const [activeTab, setActiveTab] = useState<'settings' | 'syntax' | 'regex' | 'status'>('settings');
  const [hotkeys, setHotkeys] = useState<HotkeyEntry[]>([]);
  const [settings, setSettings] = useState<SearchSettings>({
    hotkey: 'Alt+Space',
    maxResults: 50,
    defaultCategory: 'all',
    caseSensitive: false,
    matchPath: false,
    pinyinEnabled: true,
  });
  const [serviceStatus, setServiceStatus] = useState<ServiceStatus>({
    available: false,
    pipeName: '\\\\.\\pipe\\EasyToolsSearchPipe',
  });
  const [checking, setChecking] = useState(false);

  const getHotkey = (name: string) => hotkeys.find(h => h.name === name);

  const loadData = () => {
    bridgeRequest<SearchSettings>('search.getSettings')
      .then(res => setSettings(prev => ({ ...prev, ...res })))
      .catch(console.error);

    bridgeRequest<HotkeyEntry[]>('hotkey.getAll')
      .then(res => setHotkeys(Array.isArray(res) ? res : []))
      .catch(console.error);

    bridgeRequest<ServiceStatus>('search.getServiceStatus')
      .then(res => setServiceStatus(res))
      .catch(console.error);
  };

  useEffect(() => {
    loadData();
  }, []);

  const saveSetting = async <K extends keyof SearchSettings>(key: K, value: SearchSettings[K]) => {
    const updated = { ...settings, [key]: value };
    setSettings(updated);
    try {
      await bridgeRequest('search.saveSettings', { [key]: value });
      toast.success(t('searchPage.saved', '搜索设置已保存'));
    } catch {
      toast.error(t('searchPage.saveFailed', '保存设置失败'));
    }
  };

  const handleHotkeyChange = async (newKey: string) => {
    try {
      const res = await bridgeRequest<{ success: boolean; error?: string; shortcut?: string }>('hotkey.rebind', {
        name: 'Toggle Search',
        hotkey: newKey,
      });
      if (res.success) {
        await saveSetting('hotkey', res.shortcut ?? newKey);
        const refreshed = await bridgeRequest<HotkeyEntry[]>('hotkey.getAll');
        if (Array.isArray(refreshed)) setHotkeys(refreshed);
        toast.success(t('searchPage.hotkeySaved', '搜索快捷键已更新'));
      } else {
        toast.error(t('searchPage.hotkeyFailed', '快捷键可能已被其他程序占用'));
      }
    } catch {
      toast.error(t('searchPage.hotkeyFailed', '快捷键注册失败'));
    }
  };

  const checkService = async () => {
    setChecking(true);
    try {
      const res = await bridgeRequest<ServiceStatus>('search.getServiceStatus');
      setServiceStatus(res);
      if (res.available) {
        toast.success(t('searchPage.serviceOk', '文件索引管道连接正常，服务运行中'));
      } else {
        toast.error(t('searchPage.serviceDown', '索引服务未响应，请检查管理员权限'));
      }
    } catch {
      toast.error(t('searchPage.serviceDown', '连接索引服务超时'));
    } finally {
      setChecking(false);
    }
  };

  const launchSearch = () => {
    void bridgeRequest('search.toggle').catch(console.error);
  };

  const tabs: TabItem<'settings' | 'syntax' | 'regex' | 'status'>[] = [
    { id: 'settings', label: t('searchPage.tabSettings', '基础与热键'), icon: <Search size={16} /> },
    { id: 'syntax', label: t('searchPage.tabSyntax', '搜索语法手册'), icon: <BookOpen size={16} /> },
    { id: 'regex', label: t('searchPage.tabRegex', '正则表达式语法'), icon: <Code2 size={16} /> },
    { id: 'status', label: t('searchPage.tabStatus', '服务与索引'), icon: <HardDrive size={16} /> },
  ];

  return (
    <div className="search-page">
      {/* ── 顶部选项卡 ─────────────────────────────────────────────── */}
      <Tabs
        tabs={tabs}
        activeId={activeTab}
        onChange={(id) => setActiveTab(id as typeof activeTab)}
        ariaLabel={t('search.title', '文件搜索')}
      />

      {/* ── 1. 基础设置 ────────────────────────────────────────────── */}
      {activeTab === 'settings' && (
        <div className="search-page__content">
          <SettingGroup title={t('searchPage.hotkeyConfig', '快捷键与触发')} icon={<Sparkles size={18} />}>
            <Card>
              <SettingRow
                label={
                  <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                    <span>{t('searchPage.toggleHotkey', '呼出/隐藏搜索框快捷键')}</span>
                    <HotkeyStatusBadge entry={getHotkey('Toggle Search')} />
                  </div>
                }
                description={t('searchPage.toggleHotkeyDesc', '默认 Alt + Space，全局即刻唤起 Fluent 浮空搜索条')}
              >
                <HotkeyRecorder
                  id="search-toggle-hotkey"
                  value={settings.hotkey}
                  onChange={handleHotkeyChange}
                  placeholder="Alt+Space"
                />
              </SettingRow>

              <SettingRow
                label={t('searchPage.quickTest', '立即体验搜索')}
                description={t('searchPage.quickTestDesc', '测试浮动搜索条的动画弹出与响应速度')}
              >
                <Button variant="primary" onClick={launchSearch}>
                  <Play size={14} style={{ marginRight: 6 }} />
                  {t('searchPage.openNow', '唤起搜索框')}
                </Button>
              </SettingRow>
            </Card>
          </SettingGroup>

          <SettingGroup title={t('searchPage.searchBehavior', '搜索偏好与行为')} icon={<FolderSearch size={18} />}>
            <Card>
              <SettingRow
                label={t('searchPage.pinyinTitle', '智能拼音搜索')}
                description={t('searchPage.pinyinDesc', '支持中文拼音首字母（如 wx 搜微信）和全拼检索')}
              >
                <Toggle
                  id="search-pinyin-toggle"
                  checked={settings.pinyinEnabled}
                  onChange={v => saveSetting('pinyinEnabled', v)}
                />
              </SettingRow>

              <SettingRow
                label={t('searchPage.matchPathTitle', '默认匹配完整路径')}
                description={t('searchPage.matchPathDesc', '开启后搜索词将在文件绝对路径中检索，而非仅文件名')}
              >
                <Toggle
                  id="search-matchpath-toggle"
                  checked={settings.matchPath}
                  onChange={v => saveSetting('matchPath', v)}
                />
              </SettingRow>

              <SettingRow
                label={t('searchPage.caseTitle', '区分大小写')}
                description={t('searchPage.caseDesc', '默认不区分大小写；可随时在搜索词中使用 case: 前缀临时开启')}
              >
                <Toggle
                  id="search-case-toggle"
                  checked={settings.caseSensitive}
                  onChange={v => saveSetting('caseSensitive', v)}
                />
              </SettingRow>

              <SettingRow
                label={t('searchPage.maxResultsTitle', '最大返回结果数')}
                description={t('searchPage.maxResultsDesc', '限制单次查询返回条目数以保证极致流畅度')}
              >
                <Select
                  value={String(settings.maxResults)}
                  options={[
                    { value: '20', label: '20 条 (极速)' },
                    { value: '50', label: '50 条 (推荐)' },
                    { value: '100', label: '100 条' },
                    { value: '200', label: '200 条 (完整)' },
                  ]}
                  onChange={v => saveSetting('maxResults', Number(v))}
                />
              </SettingRow>
            </Card>
          </SettingGroup>
        </div>
      )}

      {/* ── 2. 搜索语法手册 ────────────────────────────────────────── */}
      {activeTab === 'syntax' && (
        <div className="search-page__content">
          <Card title={t('searchPage.operatorsTitle', '操作符与组合')}>
            <div className="syntax-table-wrapper">
              <table className="syntax-table">
                <thead>
                  <tr>
                    <th>操作符 / 语法</th>
                    <th>功能说明</th>
                    <th>示例</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td><code>space (空格)</code></td>
                    <td>逻辑 <strong>与 (AND)</strong>，同时匹配多个关键词</td>
                    <td><code>report 2026</code></td>
                  </tr>
                  <tr>
                    <td><code>|</code> 或 <code>OR</code></td>
                    <td>逻辑 <strong>或 (OR)</strong>，匹配任一条件</td>
                    <td><code>ext:jpg | ext:png</code></td>
                  </tr>
                  <tr>
                    <td><code>!</code> 或 <code>NOT</code></td>
                    <td>逻辑 <strong>非 (NOT)</strong>，排除包含特定词的文件</td>
                    <td><code>*.cpp !test</code></td>
                  </tr>
                  <tr>
                    <td><code>" "</code></td>
                    <td><strong>双引号短语</strong>，精确匹配包含空格的完整文本</td>
                    <td><code>"Program Files"</code></td>
                  </tr>
                  <tr>
                    <td><code>*</code></td>
                    <td><strong>通配符</strong>，匹配 0 个或多个任意字符</td>
                    <td><code>*.pdf</code>, <code>report_*</code></td>
                  </tr>
                  <tr>
                    <td><code>?</code></td>
                    <td><strong>通配符</strong>，匹配 1 个任意字符</td>
                    <td><code>img_??.png</code></td>
                  </tr>
                </tbody>
              </table>
            </div>
          </Card>

          <Card title={t('searchPage.modifiersTitle', '过滤器与修饰符')}>
            <div className="syntax-table-wrapper">
              <table className="syntax-table">
                <thead>
                  <tr>
                    <th>前缀 / 修饰符</th>
                    <th>功能说明</th>
                    <th>示例</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td><code>ext:&lt;后缀列表&gt;</code></td>
                    <td>指定文件扩展名，支持分号或逗号多选</td>
                    <td><code>ext:jpg;png;webp</code></td>
                  </tr>
                  <tr>
                    <td><code>file:</code></td>
                    <td>仅匹配普通文件（排除所有文件夹）</td>
                    <td><code>file: *.txt</code></td>
                  </tr>
                  <tr>
                    <td><code>folder:</code> / <code>dir:</code></td>
                    <td>仅匹配文件夹 / 目录</td>
                    <td><code>folder: project</code></td>
                  </tr>
                  <tr>
                    <td><code>path:&lt;路径&gt;</code></td>
                    <td>在文件完整绝对路径中检索</td>
                    <td><code>path:windows\system32</code></td>
                  </tr>
                  <tr>
                    <td><code>parent:&lt;目录&gt;</code> / <code>p:</code></td>
                    <td>在直接父级文件夹名称中匹配</td>
                    <td><code>parent:easytools</code></td>
                  </tr>
                  <tr>
                    <td><code>c:</code> / <code>d:</code></td>
                    <td>限定在指定磁盘驱动器下检索</td>
                    <td><code>d: *.zip</code></td>
                  </tr>
                  <tr>
                    <td><code>exact:&lt;名称&gt;</code></td>
                    <td>严格全字精确匹配文件名</td>
                    <td><code>exact:README.md</code></td>
                  </tr>
                  <tr>
                    <td><code>content:&lt;关键词&gt;</code> / <code>内容:</code></td>
                    <td><strong>全文穿透内容检索</strong>，支持代码全家桶(C/C++/Rust/Python/SQL等)、Office文档(Word/Excel/PPT)、设计稿(PSD/AI/CDR/脑图)与AutoCAD图纸</td>
                    <td><code>content:SELECT</code>, <code>ext:docx content:合同</code></td>
                  </tr>
                  <tr>
                    <td><code>case:&lt;文本&gt;</code> / <code>cs:</code></td>
                    <td>强制区分大小写匹配</td>
                    <td><code>case:EasyTools</code></td>
                  </tr>
                  <tr>
                    <td><code>pinyin:&lt;拼音&gt;</code> / <code>py:</code></td>
                    <td>显式进行拼音检索（全拼或首字母）</td>
                    <td><code>py:wx</code></td>
                  </tr>
                  <tr>
                    <td><code>nopy:&lt;文本&gt;</code></td>
                    <td>禁用拼音转换，仅匹配纯文本</td>
                    <td><code>nopy:wx</code></td>
                  </tr>
                </tbody>
              </table>
            </div>
          </Card>
        </div>
      )}

      {/* ── 3. 正则表达式手册 ──────────────────────────────────────── */}
      {activeTab === 'regex' && (
        <div className="search-page__content">
          <Card title={t('searchPage.regexTitle', '正则表达式检索 (regex: 或 r:)')}>
            <p className="syntax-hint-p">
              使用 <code>regex:&lt;表达式&gt;</code> 或 <code>r:&lt;表达式&gt;</code> 前缀即可无缝启用标准 C++ ECMAScript 正则表达式检索。
            </p>
            <div className="syntax-table-wrapper">
              <table className="syntax-table">
                <thead>
                  <tr>
                    <th>正则语法</th>
                    <th>匹配规则说明</th>
                    <th>实战应用示例</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td><code>a|b</code></td>
                    <td>匹配表达式 a 或 b</td>
                    <td><code>r:test|debug</code></td>
                  </tr>
                  <tr>
                    <td><code>.</code></td>
                    <td>匹配除换行符外的任意单个字符</td>
                    <td><code>r:a.c\.txt</code></td>
                  </tr>
                  <tr>
                    <td><code>[abc]</code></td>
                    <td>匹配括号内字符集的任意一个字符</td>
                    <td><code>r:log_[0-9]\.txt</code></td>
                  </tr>
                  <tr>
                    <td><code>[^abc]</code></td>
                    <td>排除括号内字符集中的字符</td>
                    <td><code>r:file_[^0-9]\.dat</code></td>
                  </tr>
                  <tr>
                    <td><code>[a-z]</code> / <code>[0-9]</code></td>
                    <td>匹配指定区间范围内的字符</td>
                    <td><code>r:^[a-z]{3}_[0-9]{4}</code></td>
                  </tr>
                  <tr>
                    <td><code>^</code></td>
                    <td>匹配文件名的起始位置（头部锚点）</td>
                    <td><code>r:^EasyTools.*\.exe$</code></td>
                  </tr>
                  <tr>
                    <td><code>$</code></td>
                    <td>匹配文件名的结束位置（尾部锚点）</td>
                    <td><code>r:\.min\.js$</code></td>
                  </tr>
                  <tr>
                    <td><code>*</code> / <code>+</code> / <code>?</code></td>
                    <td>匹配 0 次或多次 / 1 次或多次 / 0 或 1 次</td>
                    <td><code>r:v\d+\.\d+</code></td>
                  </tr>
                  <tr>
                    <td><code>&#123;n&#125;</code> / <code>&#123;n,m&#125;</code></td>
                    <td>精确指定匹配次数或范围区间</td>
                    <td><code>r:\d&#123;4&#125;-\d&#123;2&#125;-\d&#123;2&#125;</code></td>
                  </tr>
                  <tr>
                    <td><code>\</code></td>
                    <td>转义特殊字符（例如 <code>\.</code> 匹配字符点）</td>
                    <td><code>r:archive\.(tar\.gz|zip)</code></td>
                  </tr>
                </tbody>
              </table>
            </div>
          </Card>
        </div>
      )}

      {/* ── 4. 磁盘与服务状态 ──────────────────────────────────────── */}
      {activeTab === 'status' && (
        <div className="search-page__content">
          <Card title={t('searchPage.serviceMonitor', '全盘 MFT 索引引擎状态')}>
            <div className="service-status-card">
              <div className="service-status-header">
                <div className={`status-indicator ${serviceStatus.available ? 'status-indicator--online' : 'status-indicator--offline'}`}>
                  {serviceStatus.available ? <CheckCircle2 size={24} /> : <HelpCircle size={24} />}
                </div>
                <div className="status-info">
                  <h3 className="status-title">
                    {serviceStatus.available ? t('searchPage.serviceRunning', '索引引擎运行正常 (Local Named Pipe)') : t('searchPage.serviceStopped', '索引引擎未连接')}
                  </h3>
                  <p className="status-desc">
                    {serviceStatus.available
                      ? t('searchPage.serviceRunningDesc', 'NTFS 主文件表 (MFT) 与 USN 日志监听已挂载，毫秒级检索已就绪')
                      : t('searchPage.serviceStoppedDesc', '正在尝试后台自动静默拉起；亦可点击下方按钮重新检测')}
                  </p>
                </div>
                <Button variant="ghost" onClick={checkService} disabled={checking}>
                  <RotateCw size={14} className={checking ? 'animate-spin' : ''} style={{ marginRight: 6 }} />
                  {t('searchPage.checkNow', '重新检测')}
                </Button>
              </div>

              <div className="service-details-grid">
                <div className="detail-item">
                  <span className="detail-label">{t('searchPage.pipeAddr', '跨进程通信管道')}</span>
                  <span className="detail-value"><code>{serviceStatus.pipeName}</code></span>
                </div>
                <div className="detail-item">
                  <span className="detail-label">{t('searchPage.indexTech', '索引技术架构')}</span>
                  <span className="detail-value">NTFS MFT 内存树 + USN Journal 增量监听</span>
                </div>
                <div className="detail-item">
                  <span className="detail-label">{t('searchPage.pinyinTech', '拼音检索引擎')}</span>
                  <span className="detail-value">零堆分配 GBK/Unicode 双向全拼与首字母表</span>
                </div>
                <div className="detail-item">
                  <span className="detail-label">{t('searchPage.daemonMode', '运行架构模式')}</span>
                  <span className="detail-value">双模自适应（Windows 系统服务 / 独立无窗后台守护）</span>
                </div>
              </div>
            </div>
          </Card>
        </div>
      )}
    </div>
  );
};

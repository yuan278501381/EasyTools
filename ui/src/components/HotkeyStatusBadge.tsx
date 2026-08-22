/* ─────────────────────────────────────────────────────────────────────────────
 * HotkeyStatusBadge — 全局统一的快捷键冲突与生效状态徽章
 * ───────────────────────────────────────────────────────────────────────────── */

import type { FC } from 'react';
import { CheckCircle2, AlertTriangle, AlertOctagon } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import './HotkeyStatusBadge.css';

export interface HotkeyEntry {
  name: string;
  shortcut: string;
  registered?: boolean;
  armed?: boolean;
  conflict?: boolean;
  conflictType?: 'none' | 'internal' | 'external' | 'invalid' | string;
  conflictWith?: string;
}

interface HotkeyStatusBadgeProps {
  entry?: HotkeyEntry;
  shortcut?: string;
  registered?: boolean;
  conflictType?: string;
  conflictWith?: string;
}

export const HotkeyStatusBadge: FC<HotkeyStatusBadgeProps> = ({
  entry,
  shortcut: customShortcut,
  registered: customRegistered,
  conflictType: customConflictType,
  conflictWith: customConflictWith,
}) => {
  const { t } = useTranslation();

  const shortcut = entry?.shortcut ?? customShortcut ?? '';
  const registered = entry?.registered ?? customRegistered;
  const conflictType = entry?.conflictType ?? customConflictType;
  const conflictWith = entry?.conflictWith ?? customConflictWith;

  if (!shortcut) {
    return (
      <span className="hotkey-badge badge-disabled">
        <span>{t('general.shortcutDisabled', '未绑定')}</span>
      </span>
    );
  }

  const armed = entry?.armed !== false;
  const isInternal = conflictType === 'internal';
  const isExternal = conflictType === 'external' ||
    (registered === false && Boolean(shortcut) && armed);
  const isSessionOnly = Boolean(shortcut) && !armed && !isInternal && conflictType !== 'external';
  const isActive = (registered !== false) && armed && !entry?.conflict && !isInternal && !isExternal;

  if (isSessionOnly) {
    return (
      <span className="hotkey-badge badge-active" title={t('general.shortcutRecordingOnly')}>
        <CheckCircle2 size={12} />
        <span>{t('general.shortcutRecordingOnly')}</span>
      </span>
    );
  }

  if (isInternal) {
    return (
      <span className="hotkey-badge badge-warning" title={conflictWith || t('general.shortcutConflictInternal', '内部快捷键冲突')}>
        <AlertTriangle size={12} />
        <span>{conflictWith || t('general.shortcutConflictInternal', '内部冲突')}</span>
      </span>
    );
  }

  if (isExternal) {
    return (
      <span className="hotkey-badge badge-danger" title={conflictWith || t('general.shortcutConflictExternal', '已被系统或其他软件占用')}>
        <AlertOctagon size={12} />
        <span>{conflictWith || t('general.shortcutConflictExternal', '已被外部软件占用')}</span>
      </span>
    );
  }

  if (isActive) {
    return (
      <span className="hotkey-badge badge-active" title="快捷键已成功注册并生效">
        <CheckCircle2 size={12} />
        <span>{t('general.shortcutActive', '正常生效')}</span>
      </span>
    );
  }

  return null;
};

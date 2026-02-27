import React, { useRef } from 'react';
import {
  Play, Pause, Trash2, Download, ArrowDownToLine,
  Search, Regex, Clock, Wifi, WifiOff, Keyboard,
  RotateCcw,
} from 'lucide-react';
import type { LogLevel, ConnectionStatus } from '../../types';
import { ALL_LOG_LEVELS, LOG_LEVEL_BADGE_COLORS, countByLevel } from '../../utils';
import type { LogEntry } from '../../types';

interface ToolbarProps {
  isPaused: boolean;
  autoScroll: boolean;
  filterText: string;
  isRegex: boolean;
  showTimestamps: boolean;
  showShortcuts: boolean;
  connectionStatus: ConnectionStatus;
  activeLevels: Set<LogLevel>;
  entries: LogEntry[];
  onFilterChange: (text: string) => void;
  onToggleRegex: () => void;
  onTogglePause: () => void;
  onClear: () => void;
  onDownload: () => void;
  onAutoScroll: () => void;
  onToggleTimestamps: () => void;
  onToggleShortcuts: () => void;
  onToggleLevel: (level: LogLevel) => void;
  onReconnect: () => void;
}

export const Toolbar: React.FC<ToolbarProps> = ({
  isPaused,
  autoScroll,
  filterText,
  isRegex,
  showTimestamps,
  showShortcuts,
  connectionStatus,
  activeLevels,
  entries,
  onFilterChange,
  onToggleRegex,
  onTogglePause,
  onClear,
  onDownload,
  onAutoScroll,
  onToggleTimestamps,
  onToggleShortcuts,
  onToggleLevel,
  onReconnect,
}) => {
  const filterRef = useRef<HTMLInputElement>(null);
  const counts = countByLevel(entries);

  const connectionIcon = connectionStatus === 'connected' ? (
    <Wifi size={14} className="text-green-500" />
  ) : (
    <WifiOff size={14} className="text-red-400" />
  );

  return (
    <div className="bg-slate-800 border-b border-slate-700 shadow-sm">
      {/* Top row */}
      <div className="flex items-center justify-between px-4 py-2.5">
        <div className="flex items-center space-x-4">
          {/* Status indicator + title */}
          <div className="flex items-center space-x-2">
            <div className={`w-2.5 h-2.5 rounded-full ${
              connectionStatus === 'connected'
                ? isPaused ? 'bg-yellow-500' : 'bg-green-500 animate-pulse'
                : connectionStatus === 'connecting' ? 'bg-blue-500 animate-pulse'
                : 'bg-red-500'
            }`} />
            <h1 className="text-base font-semibold tracking-tight text-slate-100">COM1 Serial Monitor</h1>
            {connectionIcon}
          </div>

          {/* Search / Filter */}
          <div className="relative flex items-center">
            <Search size={14} className="absolute left-2.5 text-slate-500 pointer-events-none" />
            <input
              ref={filterRef}
              id="filter-input"
              type="text"
              placeholder={isRegex ? 'Regex filter...' : 'Filter logs...'}
              value={filterText}
              onChange={(e) => onFilterChange(e.target.value)}
              className={`pl-8 pr-20 py-1.5 bg-slate-900 border rounded-md text-sm text-slate-200 placeholder-slate-500 focus:outline-none focus:ring-1 w-64 transition-all ${
                isRegex
                  ? 'border-purple-500/50 focus:border-purple-500 focus:ring-purple-500'
                  : 'border-slate-700 focus:border-blue-500 focus:ring-blue-500'
              }`}
            />
            <button
              onClick={onToggleRegex}
              className={`absolute right-2 px-1.5 py-0.5 rounded text-xs font-mono transition-colors ${
                isRegex
                  ? 'bg-purple-500/20 text-purple-400 border border-purple-500/40'
                  : 'text-slate-500 hover:text-slate-300'
              }`}
              title="Toggle regex mode (R)"
            >
              <Regex size={14} />
            </button>
          </div>
        </div>
        
        {/* Action buttons */}
        <div className="flex items-center space-x-1.5">
          <ToolbarButton
            onClick={onTogglePause}
            active={isPaused}
            variant={isPaused ? 'success' : 'warning'}
            icon={isPaused ? <Play size={15} /> : <Pause size={15} />}
            label={isPaused ? 'Resume' : 'Pause'}
            shortcut="Space"
          />

          <ToolbarButton
            onClick={onClear}
            icon={<Trash2 size={15} />}
            label="Clear"
            shortcut="C"
          />

          <ToolbarButton
            onClick={onDownload}
            icon={<Download size={15} />}
            label="Save"
            shortcut="S"
          />

          <div className="w-px h-6 bg-slate-700 mx-1" />

          <ToolbarButton
            onClick={onAutoScroll}
            active={autoScroll}
            variant="primary"
            icon={<ArrowDownToLine size={15} />}
            label="Auto-scroll"
          />

          <ToolbarButton
            onClick={onToggleTimestamps}
            active={showTimestamps}
            variant="primary"
            icon={<Clock size={15} />}
            label="Timestamps"
          />

          <ToolbarButton
            onClick={onToggleShortcuts}
            active={showShortcuts}
            variant="primary"
            icon={<Keyboard size={15} />}
            label=""
            shortcut="?"
          />

          {connectionStatus !== 'connected' && (
            <ToolbarButton
              onClick={onReconnect}
              icon={<RotateCcw size={15} />}
              label="Reconnect"
              variant="danger"
            />
          )}
        </div>
      </div>

      {/* Level filter chips */}
      <div className="flex items-center px-4 py-1.5 space-x-1.5 border-t border-slate-700/50">
        <span className="text-xs text-slate-500 mr-1">Levels:</span>
        {ALL_LOG_LEVELS.map((level) => {
          const isActive = activeLevels.has(level);
          const badgeColors = LOG_LEVEL_BADGE_COLORS[level];
          const count = counts[level];
          return (
            <button
              key={level}
              onClick={() => onToggleLevel(level)}
              className={`px-2 py-0.5 rounded text-xs font-medium border transition-all ${
                isActive
                  ? badgeColors
                  : 'bg-slate-800 text-slate-600 border-slate-700 hover:border-slate-600'
              }`}
            >
              {level}
              {count > 0 && (
                <span className="ml-1 opacity-70">{count}</span>
              )}
            </button>
          );
        })}
      </div>
    </div>
  );
};

// ─── Sub-components ──────────────────────────────────────

interface ToolbarButtonProps {
  onClick: () => void;
  icon: React.ReactNode;
  label: string;
  shortcut?: string;
  active?: boolean;
  variant?: 'default' | 'primary' | 'success' | 'warning' | 'danger';
}

const ToolbarButton: React.FC<ToolbarButtonProps> = ({
  onClick,
  icon,
  label,
  shortcut,
  active = false,
  variant = 'default',
}) => {
  const variantClasses = {
    default: 'bg-slate-700 hover:bg-slate-600 text-slate-300',
    primary: active ? 'bg-blue-600 text-white' : 'bg-slate-700 hover:bg-slate-600 text-slate-300',
    success: 'bg-green-600 hover:bg-green-700 text-white',
    warning: 'bg-yellow-600 hover:bg-yellow-700 text-white',
    danger: 'bg-red-600/80 hover:bg-red-600 text-white',
  };

  return (
    <button
      onClick={onClick}
      className={`flex items-center space-x-1.5 px-2.5 py-1.5 rounded-md text-xs font-medium transition-colors ${variantClasses[variant]}`}
      title={shortcut ? `${label || 'Toggle'} (${shortcut})` : label}
    >
      {icon}
      {label && <span>{label}</span>}
    </button>
  );
};

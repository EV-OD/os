import React from 'react';
import type { ConnectionStatus, LogEntry, LogLevel } from '../../types';
import { countByLevel } from '../../utils';

interface StatusBarProps {
  isPaused: boolean;
  connectionStatus: ConnectionStatus;
  entries: LogEntry[];
  filteredCount: number;
  filterText: string;
  isRegex: boolean;
  activeLevels: Set<LogLevel>;
}

export const StatusBar: React.FC<StatusBarProps> = ({
  isPaused,
  connectionStatus,
  entries,
  filteredCount,
  filterText,
  isRegex,
  activeLevels,
}) => {
  const counts = countByLevel(entries);
  const hasActiveFilter = filterText.trim().length > 0 || activeLevels.size < 6;

  const statusText = () => {
    if (connectionStatus === 'connecting') return <span className="text-blue-400">Connecting...</span>;
    if (connectionStatus === 'error') return <span className="text-red-400">Disconnected</span>;
    if (connectionStatus === 'disconnected') return <span className="text-slate-500">Disconnected</span>;
    if (isPaused) return <span className="text-yellow-400">Paused</span>;
    return <span className="text-green-400">Listening</span>;
  };

  return (
    <div className="flex items-center justify-between px-4 py-1 bg-slate-800 border-t border-slate-700 text-xs text-slate-500 select-none">
      <div className="flex items-center space-x-4">
        <span>Status: {statusText()}</span>
        <span className="text-slate-600">|</span>
        <span>
          Lines: <span className="text-slate-300">{entries.length}</span>
          {hasActiveFilter && (
            <span className="text-slate-600"> (showing {filteredCount})</span>
          )}
        </span>
        {isRegex && filterText && (
          <>
            <span className="text-slate-600">|</span>
            <span className="text-purple-400">Regex active</span>
          </>
        )}
      </div>

      <div className="flex items-center space-x-3">
        {counts.ERROR > 0 && (
          <span className="text-red-400">
            {counts.ERROR} error{counts.ERROR !== 1 ? 's' : ''}
          </span>
        )}
        {counts.WARN > 0 && (
          <span className="text-yellow-400">
            {counts.WARN} warning{counts.WARN !== 1 ? 's' : ''}
          </span>
        )}
        <span className="text-slate-600">
          COM1 @ 115200
        </span>
      </div>
    </div>
  );
};

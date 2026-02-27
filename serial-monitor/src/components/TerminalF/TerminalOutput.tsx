import React, { forwardRef, useMemo, Fragment } from 'react';
import type { LogEntry, LogLevel } from '../../types';
import { LOG_LEVEL_COLORS, filterEntries } from '../../utils';

interface TerminalOutputProps {
  entries: LogEntry[];
  filterText: string;
  isRegex: boolean;
  levels: Set<LogLevel>;
  subsystems: Set<string>;
  showTimestamps: boolean;
  onScroll: () => void;
}

/**
 * Renders the terminal log output with line numbers, level-based coloring,
 * search highlighting, and hover interactions.
 */
export const TerminalOutput = forwardRef<HTMLDivElement, TerminalOutputProps>(
  ({ entries, filterText, isRegex, levels, subsystems, showTimestamps, onScroll }, ref) => {
    const filtered = useMemo(
      () => filterEntries(entries, filterText, isRegex, levels, subsystems),
      [entries, filterText, isRegex, levels, subsystems],
    );

    const highlightMatch = (text: string): React.ReactNode => {
      if (!filterText.trim()) return text;

      try {
        const pattern = isRegex ? filterText : escapeRegex(filterText);
        const re = new RegExp(`(${pattern})`, 'gi');
        const parts = text.split(re);

        return parts.map((part, i) =>
          re.test(part) ? (
            <mark key={i} className="bg-yellow-500/30 text-yellow-200 rounded px-0.5">
              {part}
            </mark>
          ) : (
            <Fragment key={i}>{part}</Fragment>
          ),
        );
      } catch {
        return text;
      }
    };

    const getLineColor = (level: LogLevel): string => {
      return LOG_LEVEL_COLORS[level] || 'text-green-400';
    };

    return (
      <div
        ref={ref}
        onScroll={onScroll}
        className="flex-1 overflow-y-auto bg-[#0a0a0a]"
        role="log"
        aria-label="Serial monitor output"
        aria-live="polite"
      >
        {filtered.length === 0 ? (
          <div className="flex items-center justify-center h-full">
            <div className="text-center">
              <div className="text-slate-600 text-4xl mb-3">&#9002;</div>
              <p className="text-slate-500 text-sm italic">
                {entries.length === 0
                  ? 'Waiting for serial data...'
                  : 'No lines match current filters'}
              </p>
            </div>
          </div>
        ) : (
          <table className="w-full font-mono text-sm border-collapse">
            <tbody>
              {filtered.map((entry) => (
                <tr
                  key={entry.lineNumber}
                  className="group hover:bg-slate-800/40 transition-colors duration-75"
                >
                  {/* Line number */}
                  <td className="text-right text-slate-600 select-none pr-3 pl-3 py-0 align-top w-12 border-r border-slate-800/60 group-hover:text-slate-500 transition-colors">
                    {entry.lineNumber}
                  </td>

                  {/* Timestamp */}
                  {showTimestamps && (
                    <td className="text-slate-600 px-3 py-0 align-top whitespace-nowrap text-xs">
                      {formatTimestamp(entry.receivedAt)}
                    </td>
                  )}

                  {/* Level badge */}
                  <td className="px-2 py-0 align-top w-16">
                    {entry.level !== 'UNKNOWN' && (
                      <span
                        className={`text-xs font-semibold px-1.5 py-0.5 rounded ${getLevelBadge(entry.level)}`}
                      >
                        {entry.level}
                      </span>
                    )}
                  </td>

                  {/* Subsystem */}
                  <td className="text-cyan-600 px-2 py-0 align-top whitespace-nowrap w-24">
                    {entry.subsystem && (
                      <span className="text-xs">[{entry.subsystem}]</span>
                    )}
                  </td>

                  {/* Message */}
                  <td
                    className={`py-0 pl-2 pr-4 align-top whitespace-pre-wrap break-words ${getLineColor(entry.level)}`}
                  >
                    {highlightMatch(entry.level !== 'UNKNOWN' ? entry.message : entry.raw)}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    );
  },
);

TerminalOutput.displayName = 'TerminalOutput';

// ─── Helpers ─────────────────────────────────────────────

function escapeRegex(str: string): string {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function formatTimestamp(iso: string): string {
  try {
    const d = new Date(iso);
    return d.toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' })
      + '.' + String(d.getMilliseconds()).padStart(3, '0');
  } catch {
    return '';
  }
}

function getLevelBadge(level: LogLevel): string {
  const map: Record<LogLevel, string> = {
    INFO:    'bg-blue-500/15 text-blue-400',
    WARN:    'bg-yellow-500/15 text-yellow-400',
    ERROR:   'bg-red-500/15 text-red-400',
    DEBUG:   'bg-purple-500/15 text-purple-400',
    TRACE:   'bg-slate-500/15 text-slate-400',
    UNKNOWN: '',
  };
  return map[level] || '';
}

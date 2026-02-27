import type { LogLevel, LogEntry } from '../types';

/**
 * Regex to parse kernel log lines.
 * Matches: [LEVEL]    [subsystem] message
 * Also handles lines with no level/subsystem prefix.
 */
const LOG_LINE_RE = /^\[(\w+)\]\s+\[(\w+)\]\s*(.*)$/;

/** Map of log levels to their Tailwind text color classes */
export const LOG_LEVEL_COLORS: Record<LogLevel, string> = {
  INFO:    'text-blue-400',
  WARN:    'text-yellow-400',
  ERROR:   'text-red-400',
  DEBUG:   'text-purple-400',
  TRACE:   'text-slate-400',
  UNKNOWN: 'text-green-400',
};

/** Map of log levels to their badge/chip bg colors */
export const LOG_LEVEL_BADGE_COLORS: Record<LogLevel, string> = {
  INFO:    'bg-blue-500/20 text-blue-400 border-blue-500/40',
  WARN:    'bg-yellow-500/20 text-yellow-400 border-yellow-500/40',
  ERROR:   'bg-red-500/20 text-red-400 border-red-500/40',
  DEBUG:   'bg-purple-500/20 text-purple-400 border-purple-500/40',
  TRACE:   'bg-slate-500/20 text-slate-400 border-slate-500/40',
  UNKNOWN: 'bg-green-500/20 text-green-400 border-green-500/40',
};

/** All supported log levels */
export const ALL_LOG_LEVELS: LogLevel[] = ['INFO', 'WARN', 'ERROR', 'DEBUG', 'TRACE', 'UNKNOWN'];

/**
 * Parse a single raw log line into a structured LogEntry.
 */
export function parseLogLine(raw: string, lineNumber: number): LogEntry {
  const match = raw.match(LOG_LINE_RE);

  if (match) {
    const [, levelStr, subsystem, message] = match;
    const level = normalizeLevel(levelStr);
    return {
      lineNumber,
      raw,
      level,
      subsystem,
      message: message.trim(),
      receivedAt: new Date().toISOString(),
    };
  }

  return {
    lineNumber,
    raw,
    level: 'UNKNOWN',
    subsystem: '',
    message: raw,
    receivedAt: new Date().toISOString(),
  };
}

/**
 * Parse an entire raw log string into an array of LogEntry objects.
 */
export function parseLogContent(content: string): LogEntry[] {
  if (!content) return [];
  const lines = content.split('\n');
  return lines
    .filter((line) => line.length > 0)
    .map((line, i) => parseLogLine(line, i + 1));
}

function normalizeLevel(str: string): LogLevel {
  const upper = str.toUpperCase();
  if (upper === 'INFO' || upper === 'WARN' || upper === 'WARNING' || upper === 'ERROR' || upper === 'ERR' || upper === 'DEBUG' || upper === 'TRACE') {
    if (upper === 'WARNING' || upper === 'WARN') return 'WARN';
    if (upper === 'ERR' || upper === 'ERROR') return 'ERROR';
    return upper as LogLevel;
  }
  return 'UNKNOWN';
}

/**
 * Filter log entries based on filter config.
 */
export function filterEntries(
  entries: LogEntry[],
  text: string,
  isRegex: boolean,
  levels: Set<LogLevel>,
): LogEntry[] {
  let result = entries;

  // Level filter
  if (levels.size > 0 && levels.size < ALL_LOG_LEVELS.length) {
    result = result.filter((e) => levels.has(e.level));
  }

  // Text/regex filter
  if (text.trim()) {
    if (isRegex) {
      try {
        const re = new RegExp(text, 'i');
        result = result.filter((e) => re.test(e.raw));
      } catch {
        // Invalid regex — fall back to plain text
        const lower = text.toLowerCase();
        result = result.filter((e) => e.raw.toLowerCase().includes(lower));
      }
    } else {
      const lower = text.toLowerCase();
      result = result.filter((e) => e.raw.toLowerCase().includes(lower));
    }
  }

  return result;
}

/**
 * Count entries per log level.
 */
export function countByLevel(entries: LogEntry[]): Record<LogLevel, number> {
  const counts: Record<LogLevel, number> = {
    INFO: 0, WARN: 0, ERROR: 0, DEBUG: 0, TRACE: 0, UNKNOWN: 0,
  };
  for (const e of entries) {
    counts[e.level]++;
  }
  return counts;
}

/**
 * Download text content as a file.
 */
export function downloadAsFile(content: string, filename: string): void {
  const blob = new Blob([content], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

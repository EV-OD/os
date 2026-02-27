import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Toolbar } from './Toolbar';
import { TerminalOutput } from './TerminalOutput';
import { StatusBar } from './StatusBar';
import { ShortcutsHelp } from './ShortcutsHelp';
import { useSerialStream, useKeyboardShortcuts } from '../../hooks';
import { downloadAsFile, filterEntries, ALL_LOG_LEVELS, getUniqueSubsystems } from '../../utils';
import type { LogLevel } from '../../types';

/**
 * Root terminal component. Orchestrates hooks, state, and child components.
 */
export const Terminal: React.FC = () => {
  // ─── State ──────────────────────────────────────────────
  const [isPaused, setIsPaused] = useState(false);
  const [autoScroll, setAutoScroll] = useState(true);
  const [filterText, setFilterText] = useState('');
  const [isRegex, setIsRegex] = useState(false);
  const [showTimestamps, setShowTimestamps] = useState(false);
  const [showShortcuts, setShowShortcuts] = useState(false);
  const [activeLevels, setActiveLevels] = useState<Set<LogLevel>>(
    () => new Set(ALL_LOG_LEVELS),
  );
  // Empty set means "show all subsystems" (no filter active)
  const [activeSubsystems, setActiveSubsystems] = useState<Set<string>>(() => new Set());
  const terminalRef = useRef<HTMLDivElement>(null);

  // ─── Hooks ──────────────────────────────────────────────
  const { entries, rawContent, connectionStatus, reconnect, clear } =
    useSerialStream(isPaused);

  // Scroll to bottom on new entries (when auto-scroll is on)
  useEffect(() => {
    if (autoScroll && terminalRef.current) {
      terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
    }
  }, [entries, autoScroll]);

  // ─── Handlers ───────────────────────────────────────────
  const handleScroll = useCallback(() => {
    if (!terminalRef.current) return;
    const { scrollTop, scrollHeight, clientHeight } = terminalRef.current;
    const isAtBottom = Math.abs(scrollHeight - clientHeight - scrollTop) < 10;
    setAutoScroll(isAtBottom);
  }, []);

  const handleDownload = useCallback(() => {
    const filename = `serial-log-${new Date().toISOString().replace(/[:.]/g, '-')}.txt`;
    downloadAsFile(rawContent, filename);
  }, [rawContent]);

  const handleToggleLevel = useCallback((level: LogLevel) => {
    setActiveLevels((prev) => {
      const next = new Set(prev);
      if (next.has(level)) {
        next.delete(level);
      } else {
        next.add(level);
      }
      return next;
    });
  }, []);

  const handleToggleSubsystem = useCallback((subsystem: string) => {
    setActiveSubsystems((prev) => {
      const next = new Set(prev);
      if (next.has(subsystem)) {
        next.delete(subsystem);
      } else {
        next.add(subsystem);
      }
      return next;
    });
  }, []);

  const handleClearSubsystems = useCallback(() => {
    setActiveSubsystems(new Set());
  }, []);

  const uniqueSubsystems = useMemo(() => getUniqueSubsystems(entries), [entries]);

  const filteredCount = useMemo(
    () => filterEntries(entries, filterText, isRegex, activeLevels, activeSubsystems).length,
    [entries, filterText, isRegex, activeLevels, activeSubsystems],
  );

  // ─── Keyboard shortcuts ─────────────────────────────────
  useKeyboardShortcuts(
    useMemo(
      () => [
        { key: ' ',  handler: () => setIsPaused((p) => !p),        description: 'Pause/Resume' },
        { key: 'c',  handler: clear,                                description: 'Clear' },
        { key: 's',  handler: handleDownload,                       description: 'Save log' },
        { key: 'r',  handler: () => setIsRegex((r) => !r),         description: 'Toggle regex' },
        { key: '/',  handler: () => document.getElementById('filter-input')?.focus(), description: 'Focus filter' },
        { key: 'f',  handler: () => document.getElementById('filter-input')?.focus(), description: 'Focus filter' },
        { key: '?',  shift: true, handler: () => setShowShortcuts((s) => !s), description: 'Shortcuts help' },
      ],
      [clear, handleDownload],
    ),
  );

  // ─── Render ─────────────────────────────────────────────
  return (
    <div className="relative flex flex-col h-screen bg-slate-900 text-slate-100 font-mono">
      <Toolbar
        isPaused={isPaused}
        autoScroll={autoScroll}
        filterText={filterText}
        isRegex={isRegex}
        showTimestamps={showTimestamps}
        showShortcuts={showShortcuts}
        connectionStatus={connectionStatus}
        activeLevels={activeLevels}
        activeSubsystems={activeSubsystems}
        uniqueSubsystems={uniqueSubsystems}
        entries={entries}
        onFilterChange={setFilterText}
        onToggleRegex={() => setIsRegex((r) => !r)}
        onTogglePause={() => setIsPaused((p) => !p)}
        onClear={clear}
        onDownload={handleDownload}
        onAutoScroll={() => {
          setAutoScroll(true);
          if (terminalRef.current) {
            terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
          }
        }}
        onToggleTimestamps={() => setShowTimestamps((t) => !t)}
        onToggleShortcuts={() => setShowShortcuts((s) => !s)}
        onToggleLevel={handleToggleLevel}
        onToggleSubsystem={handleToggleSubsystem}
        onClearSubsystems={handleClearSubsystems}
        onReconnect={reconnect}
      />

      <TerminalOutput
        ref={terminalRef}
        entries={entries}
        filterText={filterText}
        isRegex={isRegex}
        levels={activeLevels}
        subsystems={activeSubsystems}
        showTimestamps={showTimestamps}
        onScroll={handleScroll}
      />
      
      <StatusBar
        isPaused={isPaused}
        connectionStatus={connectionStatus}
        entries={entries}
        filteredCount={filteredCount}
        filterText={filterText}
        isRegex={isRegex}
        activeLevels={activeLevels}
      />

      <ShortcutsHelp
        visible={showShortcuts}
        onClose={() => setShowShortcuts(false)}
      />
    </div>
  );
};

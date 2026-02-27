import { useEffect } from 'react';

interface KeyboardShortcut {
  key: string;
  ctrl?: boolean;
  shift?: boolean;
  handler: () => void;
  description: string;
}

/**
 * Custom hook that registers global keyboard shortcuts.
 * Automatically cleans up on unmount.
 */
export function useKeyboardShortcuts(shortcuts: KeyboardShortcut[]): void {
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // Don't trigger shortcuts when typing in an input
      if (
        e.target instanceof HTMLInputElement ||
        e.target instanceof HTMLTextAreaElement
      ) {
        // Allow Escape to blur inputs
        if (e.key === 'Escape') {
          (e.target as HTMLElement).blur();
          return;
        }
        return;
      }

      for (const shortcut of shortcuts) {
        const ctrlMatch = shortcut.ctrl ? (e.ctrlKey || e.metaKey) : true;
        const shiftMatch = shortcut.shift ? e.shiftKey : !e.shiftKey;
        
        if (e.key.toLowerCase() === shortcut.key.toLowerCase() && ctrlMatch && shiftMatch) {
          e.preventDefault();
          shortcut.handler();
          return;
        }
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [shortcuts]);
}

/** Get the list of available shortcuts for the help panel */
export function getShortcutsList(): { key: string; description: string }[] {
  return [
    { key: 'Space', description: 'Pause / Resume' },
    { key: 'C', description: 'Clear output' },
    { key: 'S', description: 'Save / Download log' },
    { key: 'F', description: 'Focus filter input' },
    { key: 'R', description: 'Toggle regex mode' },
    { key: 'Esc', description: 'Blur filter input' },
    { key: '?', description: 'Toggle shortcuts help' },
  ];
}

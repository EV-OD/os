import React from 'react';
import { X } from 'lucide-react';
import { getShortcutsList } from '../../hooks';

interface ShortcutsHelpProps {
  visible: boolean;
  onClose: () => void;
}

export const ShortcutsHelp: React.FC<ShortcutsHelpProps> = ({ visible, onClose }) => {
  if (!visible) return null;

  const shortcuts = getShortcutsList();

  return (
    <div className="absolute top-14 right-4 z-50 bg-slate-800 border border-slate-700 rounded-lg shadow-2xl p-4 w-64">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-sm font-semibold text-slate-200">Keyboard Shortcuts</h3>
        <button
          onClick={onClose}
          className="text-slate-400 hover:text-slate-200 transition-colors"
        >
          <X size={14} />
        </button>
      </div>

      <div className="space-y-1.5">
        {shortcuts.map(({ key, description }) => (
          <div key={key} className="flex items-center justify-between text-xs">
            <span className="text-slate-400">{description}</span>
            <kbd className="px-1.5 py-0.5 bg-slate-900 border border-slate-700 rounded text-slate-300 font-mono text-xs min-w-[2rem] text-center">
              {key}
            </kbd>
          </div>
        ))}
      </div>
    </div>
  );
};

# Serial Monitor

A real-time serial output viewer for RandomOS. This React + TypeScript web application connects to the kernel's COM1 serial port via a Server-Sent Events (SSE) backend and displays structured, color-coded kernel logs in a terminal-like interface.

## Features

- **Live streaming** — receives kernel log output in real time via SSE (`/api/serial`).
- **Structured log parsing** — recognises `[LEVEL]` prefixes (INFO, WARN, ERROR, DEBUG, TRACE) and optional `[subsystem]` tags (e.g. `[paging]`, `[module]`).
- **Color-coded output** — each log level has distinct foreground and badge colors.
- **Level filtering** — toggle individual log levels on/off with count badges.
- **Text / regex search** — filter visible lines by plain text or regular expression.
- **Pause / resume** — freeze the display while the buffer keeps accumulating.
- **Auto-scroll** — follows new output; toggleable.
- **Timestamps** — optional received-at timestamps on each line.
- **Download logs** — save the current buffer to a file.
- **Keyboard shortcuts** — Space (pause), C (clear), S (save), R (regex toggle), / (focus filter), ? (help).
- **Auto-reconnect** — reconnects to the SSE endpoint after 3 seconds on connection loss.
- **Connection status** — visual indicator (green / yellow / red) in the toolbar and status bar.

## Tech Stack

- React 19 + TypeScript
- Vite (build tooling)
- Tailwind CSS v4 (styling)
- pnpm (package manager)

## Project Structure

```
serial-monitor/
├── src/
│   ├── components/
│   │   ├── Terminal.tsx             # Re-export wrapper
│   │   └── TerminalF/
│   │       ├── Container.tsx        # Main orchestrator (state, hooks, layout)
│   │       ├── Toolbar.tsx          # Top bar: status, filter, actions, level chips
│   │       ├── TerminalOutput.tsx   # Scrollable log table with highlighting
│   │       ├── StatusBar.tsx        # Bottom bar: connection, counts, baud rate
│   │       └── ShortcutsHelp.tsx    # Keyboard shortcut popover
│   ├── hooks/
│   │   ├── useSerialStream.ts       # SSE connection, parsing, reconnection
│   │   └── useKeyboardShortcuts.ts  # Global shortcut registration
│   ├── types/
│   │   └── serial.ts               # LogEntry, LogLevel, ConnectionStatus, etc.
│   └── utils/
│       └── logParser.ts             # Line parsing, filtering, level counting, download
├── package.json
└── vite.config.ts
```

## Getting Started

```bash
pnpm install
pnpm dev
```

The app expects a backend SSE endpoint at `/api/serial` that streams JSON messages:

```json
{ "type": "init",   "content": "...full buffer..." }
{ "type": "update", "content": "...new line(s)..." }
```

## Log Format

The parser expects kernel serial output in this format:

```
[INFO]    [subsystem] Message text here
[ERROR]   Description of the error
[DEBUG]   [paging] CR3=0x00104000  PSE=on(4MB)
```

Lines without a recognised level prefix are classified as `UNKNOWN`.

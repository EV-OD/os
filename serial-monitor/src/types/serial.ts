/** Log severity levels matching kernel output format */
export type LogLevel = 'INFO' | 'WARN' | 'ERROR' | 'DEBUG' | 'TRACE' | 'UNKNOWN';

/** A single parsed log entry */
export interface LogEntry {
  /** Original line number (1-based) from the raw output */
  lineNumber: number;
  /** Raw text of the line */
  raw: string;
  /** Parsed log level */
  level: LogLevel;
  /** Subsystem tag extracted from brackets, e.g. "paging" */
  subsystem: string;
  /** The message body after level/subsystem prefix */
  message: string;
  /** ISO timestamp when this line was received by the monitor */
  receivedAt: string;
}

/** SSE message payload from the backend */
export interface SerialEvent {
  type: 'init' | 'update';
  content: string;
}

/** Connection states for the SSE stream */
export type ConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

/** Filter configuration */
export interface FilterConfig {
  text: string;
  isRegex: boolean;
  levels: Set<LogLevel>;
}

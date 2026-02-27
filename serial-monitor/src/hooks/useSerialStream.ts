import { useEffect, useRef, useState, useCallback } from 'react';
import type { ConnectionStatus, SerialEvent, LogEntry } from '../types';
import { parseLogContent } from '../utils';

interface UseSerialStreamReturn {
  entries: LogEntry[];
  rawContent: string;
  connectionStatus: ConnectionStatus;
  reconnect: () => void;
  clear: () => void;
}

/**
 * Custom hook to manage the SSE connection to the serial monitor backend.
 * Handles connection lifecycle, parsing, and reconnection.
 */
export function useSerialStream(isPaused: boolean): UseSerialStreamReturn {
  const [entries, setEntries] = useState<LogEntry[]>([]);
  const [rawContent, setRawContent] = useState('');
  const [connectionStatus, setConnectionStatus] = useState<ConnectionStatus>('connecting');
  const eventSourceRef = useRef<EventSource | null>(null);
  const pausedRef = useRef(isPaused);

  // Keep paused ref in sync without re-creating the EventSource
  useEffect(() => {
    pausedRef.current = isPaused;
  }, [isPaused]);

  const connect = useCallback(() => {
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
    }

    setConnectionStatus('connecting');
    const es = new EventSource('/api/serial');
    eventSourceRef.current = es;

    es.onopen = () => {
      setConnectionStatus('connected');
    };

    es.onmessage = (event: MessageEvent) => {
      if (pausedRef.current) return;

      try {
        const data: SerialEvent = JSON.parse(event.data);
        if (data.type === 'init' || data.type === 'update') {
          setRawContent(data.content);
          setEntries(parseLogContent(data.content));
        }
      } catch (err) {
        console.error('[SerialStream] Failed to parse event:', err);
      }
    };

    es.onerror = () => {
      setConnectionStatus('error');
      es.close();
      // Auto-reconnect after 3 seconds
      setTimeout(() => {
        if (eventSourceRef.current === es) {
          connect();
        }
      }, 3000);
    };
  }, []);

  useEffect(() => {
    connect();
    return () => {
      eventSourceRef.current?.close();
      eventSourceRef.current = null;
    };
  }, [connect]);

  const reconnect = useCallback(() => {
    connect();
  }, [connect]);

  const clear = useCallback(() => {
    setEntries([]);
    setRawContent('');
  }, []);

  return { entries, rawContent, connectionStatus, reconnect, clear };
}

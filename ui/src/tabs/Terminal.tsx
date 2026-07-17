import { useState, useEffect, useRef } from 'preact/hooks';
import { Terminal as TerminalIcon, Search, ArrowDownToLine } from 'lucide-preact';
import { api } from '../api';
import { haptic } from '../utils';

export const Terminal = ({ showToast }: { showToast: (m: string) => void }) => {
  const [logs, setLogs] = useState('Loading...');
  const [filter, setFilter] = useState('');
  const [autoScroll, setAutoScroll] = useState(true);
  const logRef = useRef<HTMLTextAreaElement>(null);
  const prevLenRef = useRef(0);

  useEffect(() => {
    const poll = setInterval(() => {
      api.logs().then(txt => {
        setLogs(txt);
        if (autoScroll && logRef.current && txt.length !== prevLenRef.current)
          logRef.current.scrollTop = logRef.current.scrollHeight;
        prevLenRef.current = txt.length;
      }).catch(() => {});
    }, 2000);
    return () => clearInterval(poll);
  }, [autoScroll]);

  const displayed = filter
    ? logs.split('\n').filter(l => l.toLowerCase().includes(filter.toLowerCase())).join('\n')
    : logs;

  return (
    <div className="h-full flex flex-col pb-24 w-full space-y-4">
      <div className="flex flex-col md:flex-row justify-between items-start md:items-center gap-3">
        <h2 className="text-2xl font-bold flex items-center gap-2 text-gray-900 dark:text-white">
          <TerminalIcon className="text-gray-400" /> System Logs
        </h2>
        <div className="flex gap-2 w-full md:w-auto">
          <div className="relative flex-1 md:w-48">
            <Search size={16} className="absolute left-2.5 top-2 text-gray-400" />
            <input type="text" placeholder="Filter logs..." value={filter}
              onChange={e => setFilter((e.target as HTMLInputElement).value)}
              className="w-full bg-white dark:bg-void-900 border border-gray-200 dark:border-white/10 rounded-lg pl-8 pr-3 py-1.5 text-xs focus:border-neon-purple focus:outline-none text-gray-900 dark:text-white font-mono" />
          </div>
          <button onClick={() => { haptic(); setAutoScroll(v => !v); }}
            className={`text-xs px-3 py-1.5 rounded-lg font-bold transition-colors ${autoScroll ? 'bg-neon-green/20 text-neon-green' : 'bg-gray-200 dark:bg-void-900 text-gray-500'}`}
            title="Auto-scroll">
            <ArrowDownToLine size={16} />
          </button>
          <button onClick={() => { haptic(); api.clearLogs(); showToast('Logs Cleared'); setLogs(''); }}
            className="text-xs bg-red-500/20 text-red-400 px-3 py-1.5 rounded-lg font-bold hover:bg-red-500/30 transition-colors">
            Clear
          </button>
        </div>
      </div>
      {filter && <div className="text-xs text-neon-purple font-mono px-1">Showing {displayed.split('\n').filter(Boolean).length} matching lines</div>}
      <div className="flex-1 bg-gray-900 rounded-2xl border border-black/10 dark:border-white/10 p-4 overflow-hidden shadow-inner min-h-0">
        <textarea ref={logRef} readOnly value={displayed}
          className="w-full h-full bg-transparent font-mono text-xs text-green-400 focus:outline-none resize-none" />
      </div>
    </div>
  );
};

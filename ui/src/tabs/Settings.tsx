import { useState, useEffect, useMemo } from 'preact/hooks';
import { Settings as SettingsIcon, Plus, X, Trash2, Globe, XCircle, Wind, Shield, Bell, Moon, Sun } from 'lucide-preact';
import type { FunctionComponent } from 'preact';
import { api, type Game } from '../api';
import { haptic, parseINI, stringifyINI } from '../utils';
import { useTheme } from '../theme';
import { Skeleton } from '../components/Skeleton';

// ── Toggle ────────────────────────────────────────────────────────────────
const Toggle = ({ label, checked, onChange, icon: Icon }: {
  label: string; checked: boolean;
  onChange: (v: boolean) => void;
  icon?: FunctionComponent<{ size?: number; className?: string }>;
}) => (
  <div className="glass p-4 rounded-xl flex items-center justify-between cursor-pointer hover:bg-black/5 dark:hover:bg-white/5 transition-colors"
    onClick={() => { haptic(); onChange(!checked); }}>
    <div className="flex items-center gap-3">
      {Icon && <Icon size={20} className="text-gray-500" />}
      <span className="font-bold text-gray-700 dark:text-gray-300 text-sm">{label}</span>
    </div>
    <div className={`w-10 h-5 rounded-full relative transition-colors duration-200 ${checked ? 'bg-neon-green' : 'bg-gray-300 dark:bg-gray-700'}`}>
      <div className={`absolute top-1 w-3 h-3 bg-white rounded-full transition-all duration-200 ${checked ? 'left-6' : 'left-1'}`}></div>
    </div>
  </div>
);

// ── ListEditor ─────────────────────────────────────────────────────────────
const ListEditor = ({ label, section, data, onChange, library, type = 'list' }: {
  label: string; section: string; data: Record<string, string>;
  onChange: (section: string, data: Record<string, string>) => void;
  library: Game[]; type?: 'list' | 'map';
}) => {
  const [isAdding, setIsAdding] = useState(false);
  const [search, setSearch] = useState('');
  const [delayVal, setDelayVal] = useState(10000);

  const filteredLibrary = useMemo(() => {
    if (!library || !search) return [];
    return library.filter(g =>
      g.name.toLowerCase().includes(search.toLowerCase()) ||
      g.id.toLowerCase().includes(search.toLowerCase())
    ).slice(0, 5);
  }, [library, search]);

  const removeItem = (key: string) => { const n = { ...data }; delete n[key]; onChange(section, n); };
  const addItem = (game: Game) => {
    const n = { ...data };
    n[game.id] = type === 'map' ? delayVal.toString() : '';
    onChange(section, n); setIsAdding(false); setSearch('');
  };
  const addManual = () => {
    if (!search) return;
    const n = { ...data };
    n[search] = type === 'map' ? delayVal.toString() : '';
    onChange(section, n); setIsAdding(false); setSearch('');
  };
  const getName = (id: string) => library?.find(g => g.id === id)?.name ?? id;

  return (
    <div className="space-y-2">
      <div className="flex justify-between items-center">
        <span className="font-bold text-gray-500 dark:text-gray-400 text-xs uppercase ml-1">{label}</span>
        <button onClick={() => setIsAdding(!isAdding)} className="text-neon-blue hover:text-black dark:hover:text-white text-xs font-bold flex items-center gap-1 transition-colors">
          {isAdding ? <X size={12} /> : <Plus size={12} />} {isAdding ? 'Cancel' : 'Add'}
        </button>
      </div>
      <div className="glass rounded-xl p-2 min-h-[100px] max-h-[300px] overflow-y-auto space-y-2">
        {isAdding && (
          <div className="bg-gray-100 dark:bg-void-900 p-3 rounded-lg border border-neon-blue/50 mb-3 animate-[fadeIn_0.2s]">
            <input autoFocus type="text" placeholder="Search Game or Type ID..." value={search}
              onChange={e => setSearch((e.target as HTMLInputElement).value)}
              className="w-full bg-white dark:bg-void-800 p-2 rounded text-sm mb-2 focus:outline-none focus:ring-1 focus:ring-neon-blue text-gray-900 dark:text-white" />
            {type === 'map' && (
              <div className="flex items-center gap-2 mb-2">
                <span className="text-xs text-gray-500">Delay (ms):</span>
                <input type="number" value={delayVal} onChange={e => setDelayVal(Number((e.target as HTMLInputElement).value))}
                  className="bg-white dark:bg-void-800 p-1 rounded text-sm w-20 text-gray-900 dark:text-white" />
              </div>
            )}
            <div className="space-y-1 max-h-40 overflow-y-auto">
              {filteredLibrary.map(g => (
                <div key={g.id} onClick={() => addItem(g)} className="flex items-center gap-2 p-2 hover:bg-black/5 dark:hover:bg-white/10 rounded cursor-pointer">
                  <img src={`/assets/icon?id=${g.id}`} className="w-6 h-6 rounded" />
                  <div className="truncate text-xs">
                    <div className="font-bold text-gray-800 dark:text-gray-200">{g.name}</div>
                    <div className="text-gray-500 font-mono">{g.id}</div>
                  </div>
                </div>
              ))}
              {filteredLibrary.length === 0 && search && (
                <div onClick={addManual} className="p-2 text-xs text-neon-blue cursor-pointer hover:bg-black/5 dark:hover:bg-white/5 rounded">
                  Add "{search}" manually
                </div>
              )}
            </div>
          </div>
        )}
        {Object.keys(data).length === 0 && !isAdding && <div className="text-center py-6 text-gray-500 text-xs italic">List is empty</div>}
        {Object.keys(data).map(key => (
          <div key={key} className="flex justify-between items-center bg-black/5 dark:bg-white/5 p-2 px-3 rounded-lg group">
            <div className="flex items-center gap-3 overflow-hidden">
              <div className="font-mono text-xs text-neon-purple shrink-0">{key}</div>
              <div className="text-sm text-gray-700 dark:text-gray-300 truncate">{getName(key)}</div>
            </div>
            <div className="flex items-center gap-3">
              {type === 'map' && <span className="text-xs font-mono text-neon-yellow">{data[key]}ms</span>}
              <button onClick={() => removeItem(key)} className="text-gray-400 hover:text-red-400 transition-colors"><Trash2 size={16} /></button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

// ── Settings (exported) ───────────────────────────────────────────────────
export const Settings = ({ showToast, library }: { showToast: (m: string) => void; library: Game[] | null }) => {
  const { isDark, toggle } = useTheme();
  const [config, setConfig] = useState<Record<string, Record<string, string>>>({
    Settings: {}, FanControl: {}, Sentinel: {}, SentinelWhitelist: {}, SentinelGames: {}, CustomPaths: {}, Blacklist: {}
  });
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    api.configRaw().then(t => {
      const parsed = parseINI(t);
      ['Settings', 'FanControl', 'Sentinel', 'SentinelWhitelist', 'SentinelGames', 'CustomPaths', 'Blacklist']
        .forEach(s => { if (!parsed[s]) parsed[s] = {}; });
      setConfig(parsed); setLoading(false);
    }).catch(() => setLoading(false));
  }, []);

  const updateConfig = (section: string, key: string, value: string) =>
    setConfig(prev => ({ ...prev, [section]: { ...prev[section], [key]: value } }));
  const updateListSection = (section: string, newData: Record<string, string>) =>
    setConfig(prev => ({ ...prev, [section]: newData }));
  const updateRawList = (section: string, value: string) => {
    const newSection: Record<string, string> = {};
    value.split('\n').filter(l => l.trim()).forEach(l => { newSection[l.trim()] = ''; });
    setConfig(prev => ({ ...prev, [section]: newSection }));
  };
  const getRawListString = (section: string) => Object.keys(config[section] || {}).join('\n');
  const save = () => api.saveIni(stringifyINI(config)).then(() => showToast('Configuration Saved')).catch(() => showToast('Save Failed'));

  if (loading) return <div className="p-10 text-center"><Skeleton className="h-96 w-full" /></div>;

  return (
    <div className="h-full flex flex-col pb-24 space-y-8 animate-in fade-in w-full">
      <div className="flex justify-between items-center">
        <h2 className="text-2xl font-bold flex items-center gap-2 text-gray-900 dark:text-white">
          <SettingsIcon className="text-gray-400" /> Settings
        </h2>
        <button onClick={save} className="bg-neon-purple text-white px-6 py-2 rounded-xl font-bold text-sm shadow-lg shadow-neon-purple/20 active:scale-95 transition-transform">Save Changes</button>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6 w-full">
        <div className="space-y-4">
          <h3 className="text-sm font-bold text-gray-500 uppercase tracking-wider ml-1">General</h3>
          <Toggle label="Dark Mode" checked={isDark} onChange={toggle} icon={isDark ? Moon : Sun} />
          <Toggle label="Web Server" checked={config.Settings?.EnableWebServer === 'true'} onChange={v => updateConfig('Settings', 'EnableWebServer', String(v))} icon={Globe} />
          <Toggle label="Auto-Kill Prev. App" checked={config.Settings?.AutoKill === 'true'} onChange={v => updateConfig('Settings', 'AutoKill', String(v))} icon={XCircle} />
          <div className="glass p-4 rounded-xl flex items-center justify-between">
            <span className="font-bold text-gray-700 dark:text-gray-300 text-sm">Scan Interval (sec)</span>
            <input type="number" value={config.Settings?.ScanInterval || 5}
              onChange={e => updateConfig('Settings', 'ScanInterval', (e.target as HTMLInputElement).value)}
              className="bg-gray-100 dark:bg-void-900 border border-gray-300 dark:border-white/10 rounded-lg p-2 w-20 text-center text-sm text-gray-900 dark:text-white" />
          </div>
        </div>

        <div className="space-y-4">
          <h3 className="text-sm font-bold text-gray-500 uppercase tracking-wider ml-1">Fan Control</h3>
          <Toggle label="Enable Override" checked={config.FanControl?.Enabled === 'true'} onChange={v => updateConfig('FanControl', 'Enabled', String(v))} icon={Wind} />
          <div className="glass p-6 rounded-xl space-y-4">
            <div className="flex justify-between font-bold text-gray-700 dark:text-gray-300 text-sm">
              <span>Target Temp</span><span className="text-neon-blue">{config.FanControl?.TargetTemp || 60}°C</span>
            </div>
            <input type="range" min="40" max="80" step="1" value={config.FanControl?.TargetTemp || 60}
              onChange={e => updateConfig('FanControl', 'TargetTemp', (e.target as HTMLInputElement).value)}
              className="w-full accent-neon-blue h-2 bg-gray-200 dark:bg-void-900 rounded-lg appearance-none cursor-pointer" />
          </div>
        </div>

        <div className="space-y-4 lg:col-span-2">
          <h3 className="text-sm font-bold text-gray-500 uppercase tracking-wider ml-1">Sentinel Warden</h3>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <Toggle label="Enable Protection" checked={config.Sentinel?.EnableSentinel === 'true'} onChange={v => updateConfig('Sentinel', 'EnableSentinel', String(v))} icon={Shield} />
            <Toggle label="Show Notifications" checked={config.Sentinel?.ShowDebugNotify === 'true'} onChange={v => updateConfig('Sentinel', 'ShowDebugNotify', String(v))} icon={Bell} />
          </div>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <div className="glass p-4 rounded-xl flex items-center justify-between">
              <span className="font-bold text-gray-700 dark:text-gray-300 text-sm">Default Game Delay (ms)</span>
              <input type="number" value={config.Sentinel?.DefaultDelay || 10000}
                onChange={e => updateConfig('Sentinel', 'DefaultDelay', (e.target as HTMLInputElement).value)}
                className="bg-gray-100 dark:bg-void-900 border border-gray-300 dark:border-white/10 rounded-lg p-2 w-24 text-center text-sm text-gray-900 dark:text-white" />
            </div>
            <div className="glass p-4 rounded-xl flex items-center justify-between">
              <span className="font-bold text-gray-700 dark:text-gray-300 text-sm">Resume Delay (ms)</span>
              <input type="number" value={config.Sentinel?.ResumeDelay || 3000}
                onChange={e => updateConfig('Sentinel', 'ResumeDelay', (e.target as HTMLInputElement).value)}
                className="bg-gray-100 dark:bg-void-900 border border-gray-300 dark:border-white/10 rounded-lg p-2 w-24 text-center text-sm text-gray-900 dark:text-white" />
            </div>
          </div>
        </div>

        <div className="space-y-6 lg:col-span-2">
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            <ListEditor label="Sentinel Whitelist" section="SentinelWhitelist" data={config.SentinelWhitelist || {}} onChange={updateListSection} library={library || []} type="list" />
            <ListEditor label="Custom Delays" section="SentinelGames" data={config.SentinelGames || {}} onChange={updateListSection} library={library || []} type="map" />
          </div>
          <ListEditor label="Blacklist (Hidden Games)" section="Blacklist" data={config.Blacklist || {}} onChange={updateListSection} library={library || []} type="list" />
          <div className="space-y-2">
            <span className="font-bold text-gray-500 dark:text-gray-400 text-xs uppercase ml-1">Custom Scan Paths (One per line)</span>
            <textarea defaultValue={getRawListString('CustomPaths')}
              onBlur={e => updateRawList('CustomPaths', (e.target as HTMLTextAreaElement).value)}
              placeholder="/mnt/usb0/games..."
              className="w-full bg-gray-100 dark:bg-void-800 font-mono text-xs text-gray-700 dark:text-gray-300 p-4 rounded-xl border border-gray-300 dark:border-white/10 focus:border-neon-purple focus:outline-none resize-y min-h-[100px]" />
          </div>
        </div>
      </div>
    </div>
  );
};

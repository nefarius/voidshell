import { useState, useEffect } from 'preact/hooks';
import { Ghost, WifiOff, Info } from 'lucide-preact';
import { api, APP_VERSION, PKG_HOST, type Stats, type Game } from './api';
import { Navigation } from './components/Navigation';
import { ConnectionBadge } from './components/ConnectionBadge';
import { Dashboard } from './tabs/Dashboard';
import { PKGManager } from './tabs/PKGManager';
import { Payloads } from './tabs/Payloads';
import { Images } from './tabs/Images';
import { Settings } from './tabs/Settings';
import { Terminal } from './tabs/Terminal';
import { haptic } from './utils';

const defaultStats: Stats = {
  cpu: 0, soc: 0, active_game: '', fan_active: false,
  total: 0, ps5: 0, ps4: 0, session_count: 0,
  version: APP_VERSION, sys_uptime: ''
};

interface UploadStats { progress: number; speed: string; currentFile: string; }

export const App = () => {
  const [tab, setTab] = useState('dashboard');
  const [stats, setStats] = useState<Stats>(defaultStats);
  const [library, setLibrary] = useState<Game[] | null>(null);
  const [toast, setToast] = useState<string | null>(null);
  const [online, setOnline] = useState(true);
  const [uploading, setUploading] = useState(false);
  const [uploadStats, setUploadStats] = useState<UploadStats>({ progress: 0, speed: '0 KB/s', currentFile: '' });

  const showToast = (msg: string) => { setToast(msg); setTimeout(() => setToast(null), 3000); };
  const fetchLibrary = () => api.library().then(setLibrary).catch(() => setLibrary([]));

  useEffect(() => {
    let controller = new AbortController();
    const fetchStats = async () => {
      try { const data = await api.stats(controller.signal); setStats(data); setOnline(true); }
      catch (e: unknown) { if ((e as Error).name !== 'AbortError') setOnline(false); }
    };
    fetchStats();
    const interval = setInterval(fetchStats, 3000);
    fetchLibrary();
    return () => { clearInterval(interval); controller.abort(); };
  }, []);

  const launch = (id: string) => { api.launch(id); showToast(`Launching ${id}...`); };
  const handleAction = (action: string) => {
    haptic();
    if (action === 'rescan') { api.rescan(); showToast('Rescanning Library...'); setTimeout(fetchLibrary, 3000); }
    if (action === 'repair') { api.repair(); showToast('Repairing Icons...'); }
    if (action === 'pause')  { api.pause();  showToast('Scanner Toggled'); }
  };

  const processUpload = async (files: File[]) => {
    if (!files.length) return;
    setUploading(true);
    let lastLoaded = 0, lastTime = Date.now();
    for (const file of files) {
      setUploadStats(prev => ({ ...prev, currentFile: file.name, progress: 0, speed: '0 MB/s' }));
      try {
        await new Promise<void>((resolve, reject) => {
          const xhr = new XMLHttpRequest();
          xhr.open('POST', `${PKG_HOST}/upload?name=${encodeURIComponent(file.name)}`);
          xhr.upload.onprogress = ev => {
            if (!ev.lengthComputable) return;
            const now = Date.now(), diffTime = (now - lastTime) / 1000;
            if (diffTime >= 0.5) {
              const speed = ((ev.loaded - lastLoaded) / diffTime / 1024 / 1024).toFixed(1);
              setUploadStats({ progress: Math.round((ev.loaded / ev.total) * 100), speed: `${speed} MB/s`, currentFile: file.name });
              lastTime = now; lastLoaded = ev.loaded;
            } else {
              setUploadStats(prev => ({ ...prev, progress: Math.round((ev.loaded / ev.total) * 100) }));
            }
          };
          xhr.onload = () => xhr.status < 300 ? resolve() : reject();
          xhr.onerror = reject;
          xhr.send(file);
        });
      } catch (e) { showToast(`Failed: ${file.name}`); }
    }
    setUploading(false);
    showToast('Upload Complete');
    fetchLibrary();
  };

  return (
    <div className="flex flex-col md:flex-row h-screen">
      {!online && (
        <div className="fixed inset-0 z-[300] bg-void-900/80 backdrop-blur-md flex flex-col items-center justify-center gap-4 animate-[fadeIn_0.4s_ease-out]">
          <WifiOff size={48} className="text-neon-red animate-pulse" />
          <div className="text-xl font-black text-white">Connecting to VoidShell...</div>
          <div className="text-sm text-gray-400 font-mono">Make sure the PS5 is on and VoidShell is running</div>
          <div className="flex gap-1 mt-2">
            {[0, 1, 2].map(i => <div key={i} className="w-2 h-2 rounded-full bg-neon-purple animate-bounce" style={{ animationDelay: `${i * 0.15}s` }}></div>)}
          </div>
        </div>
      )}
      {toast && (
        <div className="fixed top-4 right-4 z-[200] animate-[slideIn_0.3s_ease-out]">
          <div className="glass px-6 py-3 rounded-xl border-l-4 border-neon-purple shadow-2xl flex items-center gap-3">
            <Info size={20} className="text-neon-purple" />
            <span className="font-bold text-sm text-gray-900 dark:text-white">{toast}</span>
          </div>
        </div>
      )}
      <Navigation tab={tab} setTab={setTab} onAction={handleAction} version={stats.version || APP_VERSION} />
      <main className="flex-1 overflow-y-auto p-4 md:p-8 w-full md:ml-20"
        style={{ paddingBottom: 'max(96px, calc(72px + env(safe-area-inset-bottom)))' }}>
        <div className="md:hidden flex justify-between items-center mb-6">
          <div className="text-neon-purple"><Ghost size={24} /></div>
          <ConnectionBadge online={online} />
        </div>
        {tab === 'dashboard' && <Dashboard activeGame={stats.active_game} library={library} stats={stats} onLaunch={launch} showToast={showToast} />}
        {tab === 'pkg'       && <PKGManager showToast={showToast} uploading={uploading} uploadStats={uploadStats} processUpload={processUpload} />}
        {tab === 'payloads'  && <Payloads showToast={showToast} />}
        {tab === 'images'    && <Images showToast={showToast} />}
        {tab === 'settings'  && <Settings showToast={showToast} library={library} />}
        {tab === 'logs'      && <Terminal showToast={showToast} />}
      </main>
    </div>
  );
};

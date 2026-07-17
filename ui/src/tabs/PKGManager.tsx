import { useState, useEffect, useRef } from 'preact/hooks';
import { Package, RefreshCw, DownloadCloud, FilePlus, FolderPlus, PlayCircle, Box, ListPlus, Check, Trash2, PackageCheck } from 'lucide-preact';
import { api, type PkgFile } from '../api';
import { haptic } from '../utils';

interface UploadStats { progress: number; speed: string; currentFile: string; }

interface PKGManagerProps {
  showToast: (m: string) => void;
  uploading: boolean;
  uploadStats: UploadStats;
  processUpload: (files: File[]) => void;
}

export const PKGManager = ({ showToast, uploading, uploadStats, processUpload }: PKGManagerProps) => {
  const [pkgs, setPkgs] = useState<PkgFile[]>([]);
  const [installQueue, setInstallQueue] = useState<string[]>([]);
  const [remoteUrl, setRemoteUrl] = useState('');
  const [installStatus, setInstallStatus] = useState('');
  const fileInputRef = useRef<HTMLInputElement>(null);
  const folderInputRef = useRef<HTMLInputElement>(null);

  const loadPkgs = () => api.pkg.list().then(setPkgs).catch(() => setPkgs([]));

  useEffect(() => { loadPkgs(); }, []);
  useEffect(() => {
    const poll = setInterval(() => api.pkg.status().then(setInstallStatus).catch(() => {}), 2000);
    return () => clearInterval(poll);
  }, []);

  const installRemoteUrl = () => {
    if (!remoteUrl) return;
    haptic();
    api.pkg.installUrl(remoteUrl)
      .then(d => showToast(d.status === 'ok' ? 'Remote Install Started' : 'Error: ' + d.msg))
      .catch(() => showToast('Connection Failed'));
    setRemoteUrl('');
  };
  const toggleQueue = (path: string) => { haptic(); setInstallQueue(q => q.includes(path) ? q.filter(p => p !== path) : [...q, path]); };
  const queueAll = () => { haptic(); setInstallQueue(pkgs.map(p => p.path)); showToast(`Queued ${pkgs.length} packages`); };
  const startQueue = async () => {
    haptic();
    if (!installQueue.length) return;
    showToast(`Installing ${installQueue.length} items...`);
    for (const path of installQueue) {
      try { await api.pkg.installExisting(path); } catch (e) { console.error(e); }
      await new Promise(r => setTimeout(r, 1000));
    }
    setInstallQueue([]);
    showToast('Installations Started');
  };
  const installSingle = (path: string) => { haptic(); api.pkg.installExisting(path); showToast('Install Started'); };
  const deletePkg = (path: string) => {
    haptic();
    if (confirm('Delete PKG?')) api.pkg.delete(path).then(() => { loadPkgs(); showToast('Package Deleted'); });
  };

  return (
    <div className="space-y-6 animate-in fade-in slide-in-from-bottom-4 pb-24 w-full">
      <div className="flex justify-between items-center">
        <h2 className="text-2xl font-bold flex items-center gap-2 text-gray-900 dark:text-white">
          <Package className="text-neon-blue" /> Package Manager
        </h2>
        <div className="flex gap-2">
          {installQueue.length > 0 && (
            <button onClick={startQueue} className="bg-neon-yellow text-black px-4 py-2 rounded-lg font-bold text-sm flex gap-2 animate-pulse hover:bg-yellow-400 transition-colors">
              <PlayCircle size={16} /> Install {installQueue.length}
            </button>
          )}
          <button onClick={queueAll} className="bg-black/10 dark:bg-white/10 hover:bg-black/20 dark:hover:bg-white/20 px-3 py-2 rounded-lg text-sm font-bold transition-colors text-gray-700 dark:text-white">Queue All</button>
          <button onClick={() => { haptic(); loadPkgs(); }} className="p-2 hover:bg-black/10 dark:hover:bg-white/10 rounded-full transition-colors"><RefreshCw size={20} className="text-neon-blue" /></button>
        </div>
      </div>

      <div className="glass p-4 rounded-2xl border border-black/5 dark:border-white/10 flex flex-col md:flex-row gap-3 items-center">
        <div className="flex-1 w-full">
          <label className="text-xs font-bold text-gray-500 dark:text-gray-400 uppercase ml-1 mb-1 block">Remote Install (URL)</label>
          <div className="flex gap-2">
            <input type="text" value={remoteUrl} onChange={e => setRemoteUrl((e.target as HTMLInputElement).value)}
              placeholder="http://192.168.1.X/game.pkg"
              className="flex-1 bg-white dark:bg-void-900 border border-gray-200 dark:border-white/10 rounded-xl px-4 py-2 text-sm focus:border-neon-blue focus:outline-none font-mono text-gray-900 dark:text-white" />
            <button onClick={installRemoteUrl} className="bg-neon-blue hover:bg-neon-blue/80 text-white px-4 rounded-xl font-bold text-sm transition-colors flex items-center gap-2">
              <DownloadCloud size={16} /> Install
            </button>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <button onClick={() => { haptic(); !uploading && fileInputRef.current?.click(); }}
          className={`glass border-2 border-dashed ${uploading ? 'border-neon-purple' : 'border-gray-300 dark:border-white/10 hover:border-neon-purple'} rounded-2xl p-6 text-center cursor-pointer transition-colors`}>
          <input type="file" multiple accept=".pkg" ref={fileInputRef} className="hidden"
            onChange={e => processUpload(Array.from((e.target as HTMLInputElement).files || []))} />
          <FilePlus size={32} className="mx-auto text-gray-500 mb-2" />
          <h3 className="font-bold text-gray-900 dark:text-white">Select Files</h3>
          <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">Upload multiple .pkg files</p>
        </button>
        <button onClick={() => { haptic(); !uploading && folderInputRef.current?.click(); }}
          className={`glass border-2 border-dashed ${uploading ? 'border-neon-purple' : 'border-gray-300 dark:border-white/10 hover:border-neon-purple'} rounded-2xl p-6 text-center cursor-pointer transition-colors`}>
          <input type="file" multiple ref={folderInputRef} className="hidden"
            onChange={e => processUpload(Array.from((e.target as HTMLInputElement).files || []).filter(f => f.name.endsWith('.pkg')))} />
          <FolderPlus size={32} className="mx-auto text-gray-500 mb-2" />
          <h3 className="font-bold text-gray-900 dark:text-white">Select Folder</h3>
          <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">Upload entire folders recursively</p>
        </button>
      </div>

      {uploading && (
        <div className="glass p-4 rounded-xl animate-[slideIn_0.3s_ease-out]">
          <div className="flex justify-between items-center mb-2">
            <span className="font-bold text-xs uppercase tracking-wider text-neon-purple animate-pulse">Uploading...</span>
            <div className="flex gap-4 font-mono text-xs text-gray-500"><span>{uploadStats.speed}</span><span>{uploadStats.progress}%</span></div>
          </div>
          <div className="font-bold text-sm truncate text-gray-900 dark:text-white mb-2">{uploadStats.currentFile}</div>
          <div className="w-full bg-gray-200 dark:bg-void-900 h-2 rounded-full overflow-hidden">
            <div className="bg-neon-purple h-full transition-all duration-300 ease-out" style={{ width: `${uploadStats.progress}%` }}></div>
          </div>
        </div>
      )}

      {installStatus && installStatus !== 'Idle' && (
        <div className="glass p-4 rounded-xl border border-neon-green/30 animate-[fadeIn_0.3s]">
          <div className="flex items-center gap-2 mb-1">
            <PackageCheck size={16} className="text-neon-green animate-pulse" />
            <span className="text-xs font-bold uppercase tracking-wider text-neon-green">Install Status</span>
          </div>
          <div className="font-mono text-xs text-gray-300 whitespace-pre-wrap">{installStatus}</div>
        </div>
      )}

      <div className="space-y-2">
        <div className="flex items-center justify-between text-xs font-bold text-gray-500 uppercase px-2">
          <span>Available Packages ({pkgs.length})</span><span>Actions</span>
        </div>
        {pkgs.map((pkg, i) => {
          const isQueued = installQueue.includes(pkg.path);
          return (
            <div key={i} className={`glass p-3 rounded-xl flex items-center justify-between transition-colors ${isQueued ? 'border-neon-yellow/50 bg-neon-yellow/5' : 'hover:bg-black/5 dark:hover:bg-white/5'}`}>
              <div className="flex items-center gap-3 overflow-hidden">
                <div className="bg-gray-200 dark:bg-void-900 p-2.5 rounded-lg"><Box size={20} className="text-gray-500" /></div>
                <div className="truncate">
                  <div className="font-bold truncate text-sm text-gray-900 dark:text-gray-200" title={pkg.name}>{pkg.name}</div>
                  <div className="text-[10px] text-gray-500 font-mono">{pkg.size}</div>
                </div>
              </div>
              <div className="flex gap-2 shrink-0">
                <button onClick={() => toggleQueue(pkg.path)}
                  className={`p-2 rounded-lg transition-colors ${isQueued ? 'bg-neon-yellow text-black' : 'bg-gray-200 dark:bg-void-900 text-gray-500 hover:text-black dark:hover:text-white border border-black/5 dark:border-white/10'}`}>
                  {isQueued ? <Check size={16} /> : <ListPlus size={16} />}
                </button>
                <button onClick={() => installSingle(pkg.path)} className="bg-neon-green/20 text-neon-green hover:bg-neon-green/30 px-3 py-1.5 rounded-lg font-bold text-xs transition-colors">Install</button>
                <button onClick={() => deletePkg(pkg.path)} className="bg-neon-red/10 text-neon-red hover:bg-neon-red/20 p-2 rounded-lg transition-colors"><Trash2 size={16} /></button>
              </div>
            </div>
          );
        })}
        {pkgs.length === 0 && <div className="text-center text-gray-500 py-10 font-mono">No .pkg files found in scan paths.</div>}
      </div>
    </div>
  );
};

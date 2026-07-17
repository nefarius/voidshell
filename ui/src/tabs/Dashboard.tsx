import { useState, useEffect, useRef, useMemo } from 'preact/hooks';
import { Play, PlayCircle, Search, Loader, Sparkles, AlertTriangle, Gamepad2 } from 'lucide-preact';
import type { Game } from '../api';
import { api } from '../api';
import { haptic } from '../utils';
import { Skeleton } from '../components/Skeleton';
import { TempWidget } from '../components/TempWidget';
import { UptimeWidget } from '../components/UptimeWidget';
import { SessionWidget } from '../components/SessionWidget';
import { LibraryWidget } from '../components/LibraryWidget';

const API_BASE = '';
const PLACEHOLDER_IMG = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=';

// ── Hero ──────────────────────────────────────────────────────────────────
const Hero = ({ activeGame, library, onLaunch }: { activeGame: string; library: Game[] | null; onLaunch: (id: string) => void }) => {
  const [slideIndex, setSlideIndex] = useState(0);
  const validGames = useMemo(() => {
    if (!library) return [];
    return library.filter(g => g.exists !== false && g.exists !== 'false').sort((a, b) => a.name.localeCompare(b.name));
  }, [library]);
  const isGameRunning = activeGame && activeGame !== 'MENU' && activeGame !== '';

  useEffect(() => {
    if (isGameRunning || validGames.length === 0) return;
    const interval = setInterval(() => setSlideIndex(p => (p + 1) % validGames.length), 8000);
    return () => clearInterval(interval);
  }, [activeGame, validGames.length, isGameRunning]);

  const gameData = isGameRunning
    ? (library?.find(g => g.id === activeGame) || { name: 'Unknown Application', id: activeGame })
    : (validGames[slideIndex] || { name: 'System Ready', id: '' });
  const visibilityClass = isGameRunning ? 'block' : 'hidden md:block';

  if (!gameData.id) return (
    <div className={`${visibilityClass} h-64 rounded-3xl relative overflow-hidden border border-black/5 dark:border-white/5 items-center justify-center bg-gray-100 dark:bg-void-800 flex mb-6`}>
      <div className="text-center">
        <div className="bg-neon-purple/10 w-16 h-16 rounded-full flex items-center justify-center mx-auto mb-4">
          <Gamepad2 size={32} className="text-neon-purple" />
        </div>
        <h2 className="text-xl font-bold text-gray-500 dark:text-gray-300">System Ready</h2>
      </div>
    </div>
  );

  return (
    <div className={`${visibilityClass} md:h-80 rounded-3xl relative overflow-hidden border border-black/5 dark:border-white/10 group shadow-xl dark:shadow-black/50 transition-all mb-6 flex flex-col md:block`}>
      <div key={gameData.id} className="absolute inset-0 animate-[fadeIn_1s_ease-out] z-0">
        <img src={`${API_BASE}/assets/pic?id=${gameData.id}`}
          onError={e => { (e.target as HTMLImageElement).style.display = 'none'; }}
          className="absolute inset-0 w-full h-full object-cover brightness-90" />
        <div className="absolute inset-0 bg-gradient-to-t from-black/60 via-transparent to-transparent"></div>
      </div>
      <div className="relative z-10 p-6 md:p-10 w-full h-full flex flex-col md:flex-row items-start md:items-end justify-between gap-4">
        <div className="w-full md:max-w-3xl order-2 md:order-1">
          <div className="flex items-center gap-2 mb-2">
            {isGameRunning
              ? <span className="px-2 py-1 bg-neon-green text-black text-[10px] font-black rounded uppercase shadow-lg shadow-neon-green/50 animate-pulse">Running</span>
              : <span className="px-2 py-1 bg-white/20 text-white text-[10px] font-bold rounded uppercase border border-white/20 backdrop-blur">Featured</span>}
            <span className="text-gray-300 text-xs font-mono bg-black/40 px-2 py-1 rounded">{gameData.id}</span>
          </div>
          <h1 className="text-3xl md:text-5xl font-black text-white leading-tight drop-shadow-md line-clamp-2 mb-4 md:mb-6" style={{ textShadow: '2px 2px 4px black' }}>{gameData.name}</h1>
          <div className="flex gap-3 w-full md:w-auto">
            <button onClick={() => { haptic(); onLaunch(gameData.id); }}
              className="flex-1 md:flex-none justify-center bg-neon-purple hover:bg-neon-purple/80 text-white px-8 py-3 md:py-2.5 rounded-xl font-bold text-sm transition-all duration-300 flex items-center gap-2 shadow-lg shadow-neon-purple/20">
              <Play size={16} className="fill-current" />
              {isGameRunning ? 'Game Active' : 'Launch Game'}
            </button>
          </div>
        </div>
        <img src={`${API_BASE}/assets/icon?id=${gameData.id}`}
          className="w-20 h-20 md:w-28 md:h-28 rounded-2xl shadow-2xl border border-white/20 order-1 md:order-2 md:rotate-3 md:group-hover:rotate-0 transition-transform duration-500 self-end md:self-auto bg-gray-200 dark:bg-void-800" />
      </div>
    </div>
  );
};

// ── Carousel (mobile) ─────────────────────────────────────────────────────
const Carousel = ({ games, onLaunch, isGameRunning }: { games: Game[] | null; onLaunch: (id: string) => void; isGameRunning: boolean }) => {
  const [index, setIndex] = useState(0);
  const [touchStart, setTouchStart] = useState<number | null>(null);

  const featuredGames = useMemo(() => {
    if (!games || !Array.isArray(games)) return [];
    const idHash = (s: string) => (s || '').split('').reduce((a, c) => (a * 31 + c.charCodeAt(0)) | 0, 0);
    return games.filter(g => g.exists !== false && g.exists !== 'false').sort((a, b) => idHash(a.id) - idHash(b.id)).slice(0, 10);
  }, [games]);

  useEffect(() => {
    if (featuredGames.length === 0) return;
    const interval = setInterval(() => setIndex(i => (i + 1) % featuredGames.length), 8000);
    return () => clearInterval(interval);
  }, [featuredGames]);

  if (isGameRunning || featuredGames.length === 0) return null;
  const game = featuredGames[index];
  if (!game) return null;

  return (
    <div className="md:hidden relative w-full aspect-video rounded-2xl overflow-hidden mb-6 shadow-2xl border border-black/10 dark:border-white/10"
      onTouchStart={e => setTouchStart(e.targetTouches[0].clientX)}
      onTouchEnd={e => {
        if (!touchStart) return;
        const diff = touchStart - e.changedTouches[0].clientX;
        if (Math.abs(diff) > 50) setIndex(i => diff > 0 ? (i + 1) % featuredGames.length : (i - 1 + featuredGames.length) % featuredGames.length);
        setTouchStart(null);
      }}>
      <img key={game.id} src={`${API_BASE}/assets/pic?id=${game.id}`}
        onError={e => { (e.target as HTMLImageElement).src = `${API_BASE}/assets/icon?id=${game.id}`; }}
        className="absolute inset-0 w-full h-full object-cover animate-[fadeIn_0.5s_ease-out] brightness-90" />
      <div className="absolute inset-0 bg-gradient-to-t from-black/80 via-black/20 to-transparent flex flex-col justify-end p-4">
        <div className="animate-[slideUp_0.3s_ease-out]">
          <h2 className="text-2xl font-black text-white leading-tight drop-shadow-md truncate">{game.name}</h2>
          <div className="flex gap-2 text-xs text-gray-300 mb-3 font-mono mt-1">
            <span className="bg-white/10 px-1.5 rounded">{game.id}</span>
          </div>
          <div className="flex gap-2">
            <button onClick={() => onLaunch(game.id)} className="flex-1 bg-neon-purple text-white py-2.5 rounded-xl font-bold text-sm shadow-lg">Launch</button>
          </div>
        </div>
      </div>
      <div className="absolute top-3 right-3 flex gap-1.5">
        {featuredGames.map((_, i) => <div key={i} className={`h-1.5 rounded-full shadow-sm transition-all duration-300 ${i === index ? 'bg-neon-purple w-6' : 'bg-white/40 w-1.5'}`}></div>)}
      </div>
    </div>
  );
};

// ── RecentlyAdded ─────────────────────────────────────────────────────────
const RecentlyAdded = ({ onLaunch, library }: { onLaunch: (id: string) => void; library: Game[] }) => {
  const [session, setSession] = useState({ count: 0, installed: [] as string[] });
  useEffect(() => { api.session().then(d => setSession(d)).catch(() => {}); }, []);
  if (!session.installed || session.installed.length === 0) return null;
  const getName = (id: string) => library.find(x => x.id === id)?.name ?? id;
  return (
    <div className="space-y-4">
      <h3 className="text-sm font-bold text-gray-500 dark:text-gray-400 uppercase tracking-wider flex items-center gap-2">
        <Sparkles size={16} className="text-neon-yellow" />
        Installed This Session ({session.count})
      </h3>
      <div className="grid grid-cols-2 md:grid-cols-4 lg:grid-cols-6 gap-3">
        {session.installed.map(id => (
          <button key={id} onClick={() => { haptic(); onLaunch(id); }}
            className="group glass p-3 rounded-xl border border-neon-yellow/20 hover:border-neon-yellow/50 transition-all hover:-translate-y-1 text-left">
            <div className="aspect-square rounded-lg overflow-hidden bg-gray-200 dark:bg-void-900 mb-2 relative">
              <img loading="lazy" src={`${API_BASE}/assets/icon?id=${id}`}
                className="w-full h-full object-cover group-hover:scale-110 transition-transform duration-500"
                onError={e => { (e.target as HTMLImageElement).src = PLACEHOLDER_IMG; }} />
              <div className="absolute top-1 right-1 bg-neon-yellow text-black text-[8px] font-black px-1 rounded">NEW</div>
            </div>
            <div className="text-xs font-bold text-gray-700 dark:text-gray-200 truncate group-hover:text-neon-yellow transition-colors">{getName(id)}</div>
            <div className="text-[10px] text-gray-500 font-mono truncate">{id}</div>
          </button>
        ))}
      </div>
    </div>
  );
};

// ── LibraryGrid ────────────────────────────────────────────────────────────
const LibraryGrid = ({ games, onLaunch, showToast }: { games: Game[] | null; onLaunch: (id: string) => void; showToast: (m: string) => void }) => {
  const [search, setSearch] = useState('');
  const [sort, setSort] = useState('name');
  const [limit, setLimit] = useState(24);
  const loadMoreRef = useRef<HTMLDivElement>(null);

  const filteredGames = useMemo(() => {
    if (!games) return [];
    let res = [...games];
    if (search) { const q = search.toLowerCase(); res = res.filter(g => g.name?.toLowerCase().includes(q) || g.id?.toLowerCase().includes(q)); }
    res.sort((a, b) => sort === 'name' ? (a.name || '').localeCompare(b.name || '') : (a.id || '').localeCompare(b.id || ''));
    return res;
  }, [games, search, sort]);

  const displayedGames = filteredGames.slice(0, limit);

  useEffect(() => {
    const observer = new IntersectionObserver(entries => { if (entries[0].isIntersecting) setLimit(l => l + 24); }, { threshold: 0.5 });
    if (loadMoreRef.current) observer.observe(loadMoreRef.current);
    return () => observer.disconnect();
  }, [filteredGames]);

  if (!games) return (
    <div className="grid grid-cols-2 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-4 md:gap-6">
      {[...Array(6)].map((_, i) => <div key={i} className="space-y-2"><Skeleton className="aspect-square rounded-xl" /><Skeleton className="h-4 w-3/4 rounded" /><Skeleton className="h-3 w-1/2 rounded" /></div>)}
    </div>
  );
  if (games.length === 0) return <div className="p-10 text-center text-gray-500">Library is empty.</div>;

  return (
    <div className="space-y-6 pb-24">
      <div className="flex flex-col md:flex-row gap-4 items-center justify-between bg-white/50 dark:bg-void-800/50 p-4 rounded-2xl backdrop-blur-md border border-black/5 dark:border-white/5">
        <div className="relative w-full md:w-96 group">
          <Search size={20} className="absolute left-3 top-2.5 text-gray-400 group-focus-within:text-neon-purple transition-colors" />
          <input type="text" placeholder="Search library..." value={search}
            onChange={e => setSearch((e.target as HTMLInputElement).value)}
            className="w-full bg-white dark:bg-void-900 border border-gray-200 dark:border-white/10 rounded-xl pl-10 pr-4 py-2 text-sm focus:border-neon-purple focus:ring-1 focus:ring-neon-purple outline-none transition-all text-gray-900 dark:text-white" />
        </div>
        <div className="flex gap-2 w-full md:w-auto">
          <select value={sort} onChange={e => setSort((e.target as HTMLSelectElement).value)}
            className="bg-white dark:bg-void-900 border border-gray-200 dark:border-white/10 rounded-xl px-4 py-2 text-sm text-gray-900 dark:text-white focus:outline-none focus:border-neon-purple cursor-pointer">
            <option value="name">Name (A-Z)</option>
            <option value="id">ID (A-Z)</option>
          </select>
        </div>
      </div>
      <div className="grid grid-cols-2 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-4 md:gap-6">
        {displayedGames.map(game => {
          const isGhost = game.exists === false || game.exists === 'false';
          return (
            <button key={game.id}
              onClick={() => { haptic(); if (isGhost) showToast('⚠️ Error: Game data not found on disk'); else onLaunch(game.id); }}
              className={`group relative text-left bg-white dark:bg-void-800 rounded-2xl p-3 border transition-all ${isGhost ? 'border-red-500/50 border-dashed opacity-60 cursor-not-allowed' : 'border-gray-100 dark:border-white/5 hover:border-neon-purple/50 cursor-pointer hover:-translate-y-1 hover:shadow-xl hover:shadow-neon-purple/10 dark:hover:shadow-black/50'}`}>
              <div className="aspect-square rounded-xl overflow-hidden bg-gray-200 dark:bg-void-900 relative mb-3">
                <img loading="lazy" src={`${API_BASE}/assets/icon?id=${game.id}`}
                  className={`w-full h-full object-cover transition-transform duration-500 ${isGhost ? 'grayscale' : 'group-hover:scale-110'}`}
                  onError={e => { (e.target as HTMLImageElement).src = PLACEHOLDER_IMG; }} />
                {isGhost && <div className="absolute inset-0 bg-black/60 flex items-center justify-center backdrop-blur-[2px]"><div className="bg-red-500/90 text-white px-3 py-1 rounded-lg text-xs font-bold border border-red-400 shadow-xl flex items-center gap-1"><AlertTriangle size={12} /> Missing</div></div>}
                {!isGhost && <div className="absolute inset-0 bg-black/40 opacity-0 group-hover:opacity-100 transition-opacity flex items-center justify-center backdrop-blur-[1px]"><PlayCircle size={40} className="text-white opacity-90 scale-50 group-hover:scale-100 transition-transform" /></div>}
              </div>
              <div className="space-y-1">
                <h3 className={`text-sm font-bold leading-tight line-clamp-2 h-10 transition-colors ${isGhost ? 'text-gray-500' : 'text-gray-700 dark:text-gray-200 group-hover:text-neon-purple'}`} title={game.name}>{game.name}</h3>
                <div className="flex items-center justify-between text-[10px] text-gray-500 font-mono border-t border-black/5 dark:border-white/5 pt-2 mt-1">
                  <span className="truncate pr-1">{game.id}</span>
                  <span className="bg-black/5 dark:bg-white/5 px-1.5 rounded shrink-0">v{game.version || '1.00'}</span>
                </div>
              </div>
            </button>
          );
        })}
      </div>
      {filteredGames.length > displayedGames.length && <div ref={loadMoreRef} className="h-20 flex items-center justify-center"><Loader size={24} className="animate-spin text-neon-purple" /></div>}
    </div>
  );
};

// ── Dashboard (exported) ──────────────────────────────────────────────────
interface DashboardStats {
  soc: number; cpu: number; fan_active: boolean;
  session_count: number; sys_uptime: string;
  total: number; ps5: number; ps4: number;
}

interface DashboardProps {
  activeGame: string;
  library: Game[] | null;
  stats: DashboardStats;
  onLaunch: (id: string) => void;
  showToast: (m: string) => void;
}

export const Dashboard = ({ activeGame, library, stats, onLaunch, showToast }: DashboardProps) => {
  const isGameRunning = !!(activeGame && activeGame !== 'MENU' && activeGame !== '');
  return (
    <div className="w-full space-y-6 md:space-y-8 animate-in fade-in slide-in-from-bottom-4 duration-500">
      <Hero activeGame={activeGame} library={library} onLaunch={onLaunch} />
      <Carousel games={library} onLaunch={onLaunch} isGameRunning={isGameRunning} />
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4 w-full">
        <TempWidget label="APU Temp" temp={stats.soc} icon="thermometer" isManual={stats.fan_active} />
        <TempWidget label="CPU Temp" temp={stats.cpu} icon="cpu" isManual={stats.fan_active} />
        <UptimeWidget uptime={stats.sys_uptime} />
        <LibraryWidget total={stats.total} ps5={stats.ps5} ps4={stats.ps4} />
      </div>
      {stats.session_count > 0 && (
        <div className="grid grid-cols-1 md:grid-cols-4 gap-4 w-full">
          <SessionWidget count={stats.session_count} />
        </div>
      )}
      <RecentlyAdded onLaunch={onLaunch} library={library || []} />
      <LibraryGrid games={library} onLaunch={onLaunch} showToast={showToast} />
    </div>
  );
};

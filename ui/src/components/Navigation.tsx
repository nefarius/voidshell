import { useState } from 'preact/hooks';
import {
  LayoutDashboard, Package, Cpu, HardDrive,
  Settings as SettingsIcon, Terminal as TerminalIcon,
  Ghost, Menu, Wrench, Moon, Sun, PauseCircle, RefreshCw
} from 'lucide-preact';
import { useTheme } from '../theme';
import { haptic } from '../utils';

interface NavProps {
  tab: string;
  setTab: (t: string) => void;
  onAction: (a: string) => void;
  version: string;
}

const tabs = [
  { id: 'dashboard', Icon: LayoutDashboard, label: 'Home' },
  { id: 'pkg',       Icon: Package,         label: 'PKG' },
  { id: 'payloads',  Icon: Cpu,             label: 'Payloads' },
  { id: 'images',    Icon: HardDrive,       label: 'Images' },
  { id: 'settings',  Icon: SettingsIcon,    label: 'Settings' },
];
const mobileTabs = tabs.slice(0, 3);

export const Navigation = ({ tab, setTab, onAction, version }: NavProps) => {
  const [showMore, setShowMore] = useState(false);
  const { isDark, toggle } = useTheme();

  const handleTab = (id: string) => { haptic(); setTab(id); setShowMore(false); };
  const handleMore = () => { haptic(); setShowMore(!showMore); };

  return (
    <>
      {/* Desktop sidebar */}
      <div className="hidden md:flex fixed left-0 top-0 h-full w-20 bg-white/80 dark:bg-void-800/80 backdrop-blur-xl border-r border-black/5 dark:border-white/5 flex-col items-center py-8 z-50">
        <div className="mb-8 text-neon-purple text-center">
          <Ghost size={32} className="mx-auto" />
        </div>
        <div className="flex flex-col gap-2 w-full px-2 items-center">
          {tabs.map(({ id, Icon }) => (
            <button key={id} onClick={() => handleTab(id)}
              className={`p-3 rounded-xl transition-all flex items-center justify-center ${tab === id ? 'bg-neon-purple text-white shadow-lg shadow-neon-purple/40' : 'text-gray-500 hover:text-black dark:hover:text-white hover:bg-black/5 dark:hover:bg-white/10'}`}
              aria-label={id}>
              <Icon size={24} />
            </button>
          ))}
          <button onClick={() => handleTab('logs')}
            className={`p-3 rounded-xl transition-all flex items-center justify-center ${tab === 'logs' ? 'bg-neon-purple text-white' : 'text-gray-500 hover:text-black dark:hover:text-white hover:bg-black/5 dark:hover:bg-white/10'}`}>
            <TerminalIcon size={24} />
          </button>
        </div>
        <div className="mt-auto flex flex-col gap-4 items-center">
          <button onClick={() => onAction('repair')} className="p-3 text-gray-500 hover:text-blue-500 dark:hover:text-blue-400 transition-colors" title="Repair Icons"><Wrench size={20} /></button>
          <button onClick={toggle} className="p-3 text-gray-500 hover:text-neon-yellow transition-colors">
            {isDark ? <Moon size={20} /> : <Sun size={20} />}
          </button>
          <button onClick={() => onAction('pause')} className="p-3 text-gray-500 hover:text-black dark:hover:text-white transition-colors"><PauseCircle size={20} /></button>
          <button onClick={() => onAction('rescan')} className="p-3 text-gray-500 hover:text-neon-red transition-colors"><RefreshCw size={20} /></button>
          <div className="text-[9px] text-gray-400 dark:text-gray-600 font-mono pb-2 opacity-70">{version}</div>
        </div>
      </div>

      {/* Mobile bottom bar */}
      <div className="md:hidden fixed bottom-0 left-0 w-full bg-white/90 dark:bg-void-800/90 backdrop-blur-xl border-t border-black/5 dark:border-white/10 flex items-center justify-around z-50 px-2"
        style={{ paddingBottom: 'max(8px, env(safe-area-inset-bottom))', paddingTop: '8px', minHeight: '60px' }}>
        {mobileTabs.map(({ id, Icon, label }) => (
          <button key={id} onClick={() => handleTab(id)}
            className={`p-2 rounded-xl transition-all flex flex-col items-center gap-1 ${tab === id ? 'text-neon-purple' : 'text-gray-400'}`}>
            <Icon size={24} />
            <span className="text-[10px] font-bold">{label}</span>
          </button>
        ))}
        <button onClick={handleMore}
          className={`p-2 rounded-xl transition-all flex flex-col items-center gap-1 ${showMore ? 'text-neon-purple' : 'text-gray-400'}`}>
          <Menu size={24} />
          <span className="text-[10px] font-bold">More</span>
        </button>
      </div>

      {/* More sheet */}
      {showMore && (
        <div className="md:hidden fixed inset-0 z-40" onClick={() => setShowMore(false)}>
          <div className="absolute inset-0 bg-black/50 backdrop-blur-sm animate-[fadeIn_0.2s]"></div>
          <div className="absolute bottom-16 left-0 w-full bg-white dark:bg-void-800 rounded-t-3xl p-6 animate-[slideUp_0.3s] shadow-2xl border-t border-black/5 dark:border-white/10"
            onClick={e => e.stopPropagation()}>
            <div className="w-12 h-1 bg-gray-300 dark:bg-gray-600 rounded-full mx-auto mb-6"></div>
            <div className="grid grid-cols-3 gap-4 mb-6">
              {[
                { id: 'images',   Icon: HardDrive,    label: 'Images' },
                { id: 'settings', Icon: SettingsIcon, label: 'Settings' },
                { id: 'logs',     Icon: TerminalIcon, label: 'Logs' },
              ].map(({ id, Icon, label }) => (
                <button key={id} onClick={() => handleTab(id)}
                  className={`flex flex-col items-center gap-2 ${tab === id ? 'text-neon-purple' : 'text-gray-600 dark:text-gray-300'}`}>
                  <div className={`w-12 h-12 rounded-2xl flex items-center justify-center ${tab === id ? 'bg-neon-purple/10' : 'bg-gray-100 dark:bg-void-700'}`}>
                    <Icon size={24} />
                  </div>
                  <span className="text-xs">{label}</span>
                </button>
              ))}
              {[
                { action: 'pause',  Icon: PauseCircle, label: 'Pause' },
                { action: 'repair', Icon: Wrench,      label: 'Repair' },
                { action: 'rescan', Icon: RefreshCw,   label: 'Rescan' },
              ].map(({ action, Icon, label }) => (
                <button key={action} onClick={() => { onAction(action); setShowMore(false); }}
                  className="flex flex-col items-center gap-2 text-gray-600 dark:text-gray-300">
                  <div className="w-12 h-12 bg-gray-100 dark:bg-void-700 rounded-2xl flex items-center justify-center">
                    <Icon size={24} />
                  </div>
                  <span className="text-xs">{label}</span>
                </button>
              ))}
            </div>
            <div className="text-center text-[11px] text-gray-400 dark:text-gray-500 font-mono mb-2 opacity-70">VoidShell {version}</div>
            <button onClick={toggle} className="w-full glass p-4 rounded-xl flex items-center justify-between">
              <span className="font-bold text-gray-900 dark:text-white">Dark Mode</span>
              <div className={`w-10 h-5 rounded-full relative transition-colors duration-200 ${isDark ? 'bg-neon-green' : 'bg-gray-400'}`}>
                <div className={`absolute top-1 w-3 h-3 bg-white rounded-full transition-all duration-200 ${isDark ? 'left-6' : 'left-1'}`}></div>
              </div>
            </button>
          </div>
        </div>
      )}
    </>
  );
};

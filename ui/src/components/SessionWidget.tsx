import { Download } from 'lucide-preact';

export const SessionWidget = ({ count }: { count?: number }) => (
  <div className="glass p-4 rounded-2xl relative overflow-hidden flex flex-col justify-center border-l-4 border-neon-blue/50">
    <div className="relative z-10">
      <div className="flex items-center gap-2 mb-2">
        <Download size={16} className="text-neon-blue" />
        <span className="text-[10px] font-bold uppercase tracking-wider text-gray-500 dark:text-gray-400">This Session</span>
      </div>
      <div className="text-3xl font-black text-gray-900 dark:text-white leading-none">
        {count || 0} <span className="text-sm font-normal text-gray-500">Installed</span>
      </div>
      <div className="text-[10px] text-neon-blue/70 mt-1">Since last boot</div>
    </div>
  </div>
);

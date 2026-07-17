import { Activity } from 'lucide-preact';

export const UptimeWidget = ({ uptime }: { uptime?: string }) => (
  <div className="glass p-4 rounded-2xl relative overflow-hidden flex flex-col justify-center border-l-4 border-neon-green/50">
    <div className="relative z-10">
      <div className="flex items-center gap-2 mb-2">
        <Activity size={16} className="text-neon-green animate-pulse" />
        <span className="text-[10px] font-bold uppercase tracking-wider text-gray-500 dark:text-gray-400">System Uptime</span>
      </div>
      <div className="text-2xl font-black font-mono tracking-widest text-gray-900 dark:text-white">{uptime || '00:00'}</div>
      <div className="text-[10px] text-neon-green/70 mt-1">Systems Nominal</div>
    </div>
  </div>
);

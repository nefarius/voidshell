export const ConnectionBadge = ({ online }: { online: boolean }) => (
  <div className={`flex items-center gap-2 px-3 py-1 rounded-full text-[10px] font-bold uppercase tracking-wider border transition-colors ${online ? 'bg-neon-green/10 text-neon-green border-neon-green/20' : 'bg-neon-red/10 text-neon-red border-neon-red/20'}`}>
    <div className={`w-1.5 h-1.5 rounded-full ${online ? 'bg-neon-green animate-pulse' : 'bg-neon-red'}`}></div>
    {online ? 'Online' : 'Offline'}
  </div>
);

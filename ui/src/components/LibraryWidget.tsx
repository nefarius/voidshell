import { Library as LibraryIcon } from 'lucide-preact';

interface LibraryWidgetProps { total?: number; ps5?: number; ps4?: number; }

export const LibraryWidget = ({ total, ps5, ps4 }: LibraryWidgetProps) => {
  const totalCount = total || 1;
  return (
    <div className="glass p-4 rounded-2xl relative overflow-hidden flex flex-col justify-between border-t border-black/5 dark:border-white/5">
      <div className="flex justify-between items-end mb-2">
        <div>
          <div className="text-[10px] font-bold uppercase tracking-wider text-gray-500 dark:text-gray-400 mb-1">Total Library</div>
          <div className="text-3xl font-black text-gray-900 dark:text-white leading-none">
            {total || 0} <span className="text-sm font-normal text-gray-500">Games</span>
          </div>
        </div>
        <LibraryIcon size={24} className="text-black/10 dark:text-white/20" />
      </div>
      <div className="space-y-2">
        <div className="h-2 w-full bg-gray-200 dark:bg-void-900 rounded-full overflow-hidden flex">
          <div style={{ width: `${((ps5 || 0) / totalCount) * 100}%` }} className="bg-neon-blue shadow-[0_0_10px_rgba(59,130,246,0.5)]"></div>
          <div style={{ width: `${((ps4 || 0) / totalCount) * 100}%` }} className="bg-purple-600 shadow-[0_0_10px_rgba(147,51,234,0.5)]"></div>
        </div>
        <div className="flex justify-between text-[10px] font-bold">
          <div className="flex items-center gap-1.5 text-neon-blue"><div className="w-2 h-2 rounded-full bg-neon-blue"></div>PS5: {ps5 || 0}</div>
          <div className="flex items-center gap-1.5 text-purple-600 dark:text-purple-400">PS4: {ps4 || 0}<div className="w-2 h-2 rounded-full bg-purple-500"></div></div>
        </div>
      </div>
    </div>
  );
};

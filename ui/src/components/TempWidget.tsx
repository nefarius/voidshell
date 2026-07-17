interface TempWidgetProps {
  label: string;
  temp: number;
  icon: string;
  isManual: boolean;
}

export const TempWidget = ({ label, temp, isManual }: TempWidgetProps) => {
  const safeTemp = temp || 0;
  const radius = 30;
  const circumference = 2 * Math.PI * radius;
  const offset = circumference - ((Math.min(safeTemp, 100) / 100) * circumference);
  let colorClass = 'text-neon-green';
  if (safeTemp > 70) colorClass = 'text-neon-red';
  else if (safeTemp > 55) colorClass = 'text-neon-orange';

  return (
    <div className="glass p-4 rounded-2xl relative overflow-hidden flex items-center justify-between border-l-4 border-transparent hover:border-black/5 dark:hover:border-white/10 transition-all group">
      <div className="relative z-10">
        <div className="flex items-center gap-2 mb-1">
          <span className="text-[10px] font-bold uppercase tracking-wider text-gray-500 dark:text-gray-400">{label}</span>
        </div>
        <div className="text-3xl font-black font-mono leading-none text-gray-900 dark:text-white">{safeTemp}°</div>
        {isManual && <div className="text-[8px] text-neon-orange font-bold mt-1 animate-pulse">MANUAL OVERRIDE</div>}
      </div>
      <div className="relative w-20 h-20 flex items-center justify-center">
        <div className={`absolute inset-0 blur-xl rounded-full opacity-20 ${safeTemp > 70 ? 'bg-red-500' : safeTemp > 55 ? 'bg-orange-500' : 'bg-green-500'}`}></div>
        <svg className="w-full h-full -rotate-90 transform" viewBox="0 0 80 80">
          <circle cx="40" cy="40" r={radius} stroke="currentColor" strokeWidth="6" fill="transparent" className="text-black/5 dark:text-white/5" />
          <circle cx="40" cy="40" r={radius} stroke="currentColor" strokeWidth="6" fill="transparent"
            strokeDasharray={circumference} strokeDashoffset={offset} strokeLinecap="round"
            className={`${colorClass} transition-all duration-1000 ease-out`} />
        </svg>
      </div>
    </div>
  );
};

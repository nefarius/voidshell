export const Skeleton = ({ className }: { className?: string }) => (
  <div className={`animate-pulse bg-gray-300 dark:bg-white/10 rounded ${className ?? ''}`}></div>
);

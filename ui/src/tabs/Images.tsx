import { useState, useEffect } from 'preact/hooks';
import { HardDrive, Disc, Unplug, RefreshCw } from 'lucide-preact';
import { api, type MountedImage } from '../api';
import { haptic } from '../utils';

export const Images = ({ showToast }: { showToast: (m: string) => void }) => {
  const [images, setImages] = useState<MountedImage[]>([]);

  const fetchImages = () => api.images().then(data => setImages(Array.isArray(data) ? data : [])).catch(() => {});

  useEffect(() => {
    fetchImages();
    const id = setInterval(fetchImages, 5000);
    return () => clearInterval(id);
  }, []);

  const unmount = (path: string) => {
    haptic();
    api.unmountImage(path)
      .then(d => { showToast(d.status === 'ok' ? 'Image unmounted' : 'Unmount failed'); fetchImages(); })
      .catch(() => showToast('Unmount error'));
  };

  return (
    <div className="space-y-6 pb-24 animate-in fade-in slide-in-from-bottom-4">
      <div className="flex justify-between items-center">
        <h2 className="text-2xl font-bold flex items-center gap-2 text-gray-900 dark:text-white">
          <HardDrive className="text-neon-blue" /> Mounted Images
        </h2>
        <button onClick={fetchImages}
          className="bg-neon-blue/10 text-neon-blue hover:bg-neon-blue/20 px-4 py-2 rounded-xl font-bold text-xs border border-neon-blue/20 transition-colors flex items-center gap-2">
          <RefreshCw size={14} /> Refresh
        </button>
      </div>

      {images.length === 0 ? (
        <div className="text-center text-gray-500 py-16">
          <HardDrive size={48} className="mx-auto mb-4 text-gray-300 dark:text-gray-600" />
          <p className="font-medium">No images mounted</p>
          <p className="text-xs mt-1 text-gray-400">Place .exfat / .ffpkg / .ffpfs files in a configured ScanPath</p>
        </div>
      ) : (
        <div className="glass rounded-2xl overflow-hidden">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-black/5 dark:border-white/10">
                <th className="text-left p-4 text-xs font-bold uppercase tracking-wider text-gray-500">Path</th>
                <th className="text-left p-4 text-xs font-bold uppercase tracking-wider text-gray-500 hidden md:table-cell">Mount Point</th>
                <th className="text-left p-4 text-xs font-bold uppercase tracking-wider text-gray-500">Backend</th>
                <th className="p-4"></th>
              </tr>
            </thead>
            <tbody>
              {images.map((img, i) => (
                <tr key={img.path} className={`border-b border-black/5 dark:border-white/5 last:border-0 ${i % 2 === 0 ? '' : 'bg-black/[0.02] dark:bg-white/[0.02]'}`}>
                  <td className="p-4">
                    <div className="flex items-center gap-3">
                      <div className="bg-neon-blue/10 p-2 rounded-lg flex-shrink-0"><Disc size={16} className="text-neon-blue" /></div>
                      <span className="font-mono text-xs text-gray-700 dark:text-gray-300 truncate max-w-[180px]" title={img.path}>{img.path.split('/').pop()}</span>
                    </div>
                  </td>
                  <td className="p-4 hidden md:table-cell">
                    <span className="font-mono text-xs text-gray-500 truncate max-w-[200px] block" title={img.mount_point}>{img.mount_point}</span>
                  </td>
                  <td className="p-4">
                    <span className={`text-xs font-bold px-2 py-1 rounded-full ${img.backend === 'lvd' ? 'bg-neon-purple/10 text-neon-purple' : 'bg-neon-green/10 text-neon-green'}`}>
                      {img.backend.toUpperCase()}
                    </span>
                  </td>
                  <td className="p-4 text-right">
                    <button onClick={() => unmount(img.path)}
                      className="bg-red-500/10 text-red-500 hover:bg-red-500/20 px-3 py-1.5 rounded-lg font-bold text-xs flex gap-1.5 items-center ml-auto transition-colors border border-red-500/20">
                      <Unplug size={12} /> Unmount
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
};

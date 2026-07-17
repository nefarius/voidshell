import { useState, useEffect } from 'preact/hooks';
import { Cpu, FileCode, Play } from 'lucide-preact';
import { api, type Payload } from '../api';
import { haptic, parseINI, stringifyINI } from '../utils';

interface PayloadEntry extends Payload { enabled: boolean; delay: string; }

export const Payloads = ({ showToast }: { showToast: (m: string) => void }) => {
  const [payloads, setPayloads] = useState<PayloadEntry[]>([]);
  const [config, setConfig] = useState<Record<string, Record<string, string>>>({ AutoPayloads: {} });

  useEffect(() => {
    const loadData = async () => {
      try {
        const [files, configText] = await Promise.all([api.payloads(), api.configRaw()]);
        const parsedConfig = parseINI(configText);
        if (!parsedConfig['AutoPayloads']) parsedConfig['AutoPayloads'] = {};
        setConfig(parsedConfig);
        const autoPayloads = parsedConfig['AutoPayloads'] || {};
        setPayloads(files.map(f => ({ ...f, enabled: f.name in autoPayloads, delay: autoPayloads[f.name] || '0' })));
      } catch (e) {}
    };
    loadData();
  }, []);

  const inject = (name: string) => { haptic(); api.sendPayload(name).then(() => showToast('Payload Injected')); };
  const toggleAuto = (index: number) => setPayloads(cur => { const n = [...cur]; n[index] = { ...n[index], enabled: !n[index].enabled }; return n; });
  const updateDelay = (index: number, val: string) => setPayloads(cur => { const n = [...cur]; n[index] = { ...n[index], delay: val }; return n; });
  const saveConfig = () => {
    haptic();
    const newAutoPayloads: Record<string, string> = {};
    payloads.forEach(p => { if (p.enabled) newAutoPayloads[p.name] = p.delay; });
    api.saveIni(stringifyINI({ ...config, AutoPayloads: newAutoPayloads })).then(() => showToast('Auto-Run Config Saved'));
  };

  return (
    <div className="space-y-6 pb-24 animate-in fade-in slide-in-from-bottom-4">
      <div className="flex justify-between items-center">
        <h2 className="text-2xl font-bold flex items-center gap-2 text-gray-900 dark:text-white">
          <Cpu className="text-neon-pink" /> Payload Manager
        </h2>
        <button onClick={saveConfig} className="bg-neon-pink text-white px-4 py-2 rounded-xl font-bold text-xs shadow-lg hover:bg-neon-pink/80 transition-colors">Save Config</button>
      </div>
      <div className="grid grid-cols-1 gap-3">
        {payloads.map((pl, i) => (
          <div key={pl.name} className="glass p-4 rounded-xl flex flex-col md:flex-row items-center justify-between gap-4">
            <div className="flex items-center gap-4 w-full md:w-auto">
              <div className="bg-gray-200 dark:bg-void-900 p-2 rounded-lg"><FileCode size={20} className="text-gray-500" /></div>
              <div className="font-mono text-sm font-bold truncate text-gray-900 dark:text-gray-200">{pl.name}</div>
            </div>
            <div className="flex items-center gap-4 w-full md:w-auto justify-end">
              <div className="flex items-center gap-2 bg-gray-100 dark:bg-void-900 p-1.5 rounded-lg border border-black/5 dark:border-white/5">
                <span className="text-[10px] font-bold text-gray-500 uppercase px-1">Auto</span>
                <button onClick={() => toggleAuto(i)}
                  className={`w-8 h-4 rounded-full relative transition-colors ${pl.enabled ? 'bg-neon-green' : 'bg-gray-400'}`}>
                  <div className={`absolute top-0.5 w-3 h-3 bg-white rounded-full transition-all ${pl.enabled ? 'left-4' : 'left-0.5'}`}></div>
                </button>
              </div>
              <div className="flex items-center gap-2">
                <span className="text-[10px] font-bold text-gray-500 uppercase">Delay(ms)</span>
                <input type="number" value={pl.delay} onChange={e => updateDelay(i, (e.target as HTMLInputElement).value)}
                  className="w-16 bg-gray-100 dark:bg-void-900 border border-gray-300 dark:border-white/10 rounded px-2 py-1 text-xs text-center focus:outline-none focus:border-neon-pink text-gray-900 dark:text-white" />
              </div>
              <button onClick={() => inject(pl.name)}
                className="bg-neon-blue/10 text-neon-blue hover:bg-neon-blue/20 px-4 py-2 rounded-lg font-bold text-xs flex gap-2 items-center transition-colors border border-neon-blue/20">
                <Play size={12} /> Inject
              </button>
            </div>
          </div>
        ))}
      </div>
      {payloads.length === 0 && <div className="text-center text-gray-500 py-10">No payloads found in /data/voidshell/payloads</div>}
    </div>
  );
};

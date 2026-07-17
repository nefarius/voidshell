const API_BASE = '';
export const PKG_PORT = 9200;
export const PKG_HOST = `http://${window.location.hostname}:${PKG_PORT}`;
export const APP_VERSION = 'V1.1';

export interface Stats {
  cpu: number;
  soc: number;
  active_game: string;
  fan_active: boolean;
  total: number;
  ps5: number;
  ps4: number;
  session_count: number;
  version: string;
  sys_uptime: string;
}

export interface Game {
  id: string;
  name: string;
  version?: string;
  exists?: boolean | string;
}

export interface MountedImage {
  path: string;
  mount_point: string;
  backend: 'lvd' | 'md';
  unit: number;
}

export interface PkgFile {
  name: string;
  path: string;
  size: string;
}

export interface Payload {
  name: string;
  size?: string;
}

export interface SessionData {
  count: number;
  installed: string[];
}

export const api = {
  stats: (signal?: AbortSignal) =>
    fetch(`${API_BASE}/api/stats`, { signal }).then(r => r.json() as Promise<Stats>),

  library: () =>
    fetch(`${API_BASE}/api/library`).then(r => r.json()).then((d: { games: Game[] }) => d.games || []),

  launch: (id: string) =>
    fetch(`${API_BASE}/api/launch`, { method: 'POST', body: id }),

  rescan: () =>
    fetch(`${API_BASE}/api/rescan`, { method: 'POST' }),

  repair: () =>
    fetch(`${API_BASE}/api/repair`, { method: 'POST' }),

  pause: () =>
    fetch(`${API_BASE}/api/pause`, { method: 'POST' }),

  configRaw: () =>
    fetch(`${API_BASE}/api/config_raw`).then(r => r.text()),

  saveIni: (body: string) =>
    fetch(`${API_BASE}/api/save_ini`, { method: 'POST', body }),

  logs: () =>
    fetch(`${API_BASE}/api/logs`).then(r => r.text()),

  clearLogs: () =>
    fetch(`${API_BASE}/api/logs/clear`, { method: 'POST' }),

  payloads: () =>
    fetch(`${API_BASE}/api/payloads`).then(r => r.json()).then((d: { files: Payload[] }) => d.files || []),

  sendPayload: (name: string) =>
    fetch(`${API_BASE}/api/send_payload`, { method: 'POST', body: name }),

  images: () =>
    fetch(`${API_BASE}/api/images`).then(r => r.json() as Promise<MountedImage[]>),

  unmountImage: (path: string) =>
    fetch(`${API_BASE}/api/images/unmount`, { method: 'POST', body: path })
      .then(r => r.json() as Promise<{ status: string }>),

  session: () =>
    fetch(`${API_BASE}/api/session`).then(r => r.json() as Promise<SessionData>),

  pkg: {
    list: () =>
      fetch(`${PKG_HOST}/list`, { mode: 'cors' }).then(r => r.json()).then((d: { files: PkgFile[] }) => d.files || []),

    status: () =>
      fetch(`${PKG_HOST}/status`, { mode: 'cors' }).then(r => r.text()),

    installUrl: (url: string) =>
      fetch(`${PKG_HOST}/api/install_url`, { method: 'POST', body: url, mode: 'cors' })
        .then(r => r.json() as Promise<{ status: string; msg?: string }>),

    installExisting: (path: string) =>
      fetch(`${PKG_HOST}/install_existing`, { method: 'POST', body: path, mode: 'cors' }),

    delete: (path: string) =>
      fetch(`${PKG_HOST}/delete`, { method: 'POST', body: path, mode: 'cors' }),
  },
};

export const haptic = (): void => {
  if (navigator.vibrate) navigator.vibrate(5);
};

export const parseINI = (text: string): Record<string, Record<string, string>> => {
  const result: Record<string, Record<string, string>> = {};
  let section = 'root';
  if (!text) return result;
  text.split('\n').forEach(line => {
    line = line.trim();
    if (!line || line.startsWith('#') || line.startsWith(';')) return;
    if (line.startsWith('[') && line.endsWith(']')) {
      section = line.slice(1, -1);
      result[section] = result[section] || {};
    } else if (line.includes('=')) {
      const [key, ...rest] = line.split('=');
      const val = rest.join('=');
      if (section === 'root') {
        if (!result['root']) result['root'] = {};
        result['root'][key.trim()] = val.trim();
      } else {
        result[section][key.trim()] = val.trim();
      }
    } else {
      if (section !== 'root' && line) result[section][line] = '';
    }
  });
  return result;
};

export const stringifyINI = (data: Record<string, Record<string, string>>): string => {
  let text = '# VoidShell Config\n\n';
  Object.keys(data).forEach(section => {
    if (section !== 'root') {
      text += `[${section}]\n`;
      Object.keys(data[section]).forEach(key => {
        const val = data[section][key];
        if (val === '' || val === null || val === undefined) text += `${key}\n`;
        else text += `${key}=${val}\n`;
      });
      text += '\n';
    }
  });
  return text;
};

export const formatBytes = (bytes: number, decimals = 2): string => {
  if (!+bytes) return '0 Bytes';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`;
};

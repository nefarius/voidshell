import { createContext } from 'preact';
import { useState, useEffect, useContext } from 'preact/hooks';
import type { ComponentChildren } from 'preact';

interface ThemeContextType {
  isDark: boolean;
  toggle: () => void;
}

export const ThemeContext = createContext<ThemeContextType>({ isDark: true, toggle: () => {} });

export const ThemeProvider = ({ children }: { children: ComponentChildren }) => {
  const [isDark, setIsDark] = useState(true);

  useEffect(() => {
    const saved = localStorage.getItem('voidshell_theme');
    if (saved) setIsDark(saved === 'dark');
    else if (window.matchMedia?.('(prefers-color-scheme: light)').matches) setIsDark(false);
  }, []);

  useEffect(() => {
    const root = window.document.documentElement;
    if (isDark) root.classList.add('dark');
    else root.classList.remove('dark');
    localStorage.setItem('voidshell_theme', isDark ? 'dark' : 'light');
  }, [isDark]);

  const toggle = () => {
    if (navigator.vibrate) navigator.vibrate(10);
    setIsDark(p => !p);
  };

  return (
    <ThemeContext.Provider value={{ isDark, toggle }}>
      {children}
    </ThemeContext.Provider>
  );
};

export const useTheme = () => useContext(ThemeContext);

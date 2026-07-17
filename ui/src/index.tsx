import { render } from 'preact';
import { ThemeProvider } from './theme';
import { App } from './App';

render(
  <ThemeProvider><App /></ThemeProvider>,
  document.getElementById('root')!
);

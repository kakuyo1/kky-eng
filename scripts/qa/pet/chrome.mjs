// Resolves the Chrome executable from config/paths.json (chromeExe), with config/paths.local.json winning
// key by key, as config/README.md requires. The CHROME environment variable overrides both.
import { readFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const read = file => (existsSync(file) ? JSON.parse(readFileSync(file, 'utf8')) : {});

export function chromePath() {
  if (process.env.CHROME) return process.env.CHROME;
  const paths = { ...read(path.join(root, 'config', 'paths.json')), ...read(path.join(root, 'config', 'paths.local.json')) };
  if (!paths.chromeExe) throw new Error('config/paths.json has no chromeExe; set it in config/paths.local.json or set CHROME');
  return paths.chromeExe;
}

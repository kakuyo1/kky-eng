// Captures the desktop pet prototype and the built settings page (ui-prototypes/pet.html) side by side:
// every action and every accessory, at the same offsets after the trigger, and a montage of each set.
// Usage (from anywhere): node scripts/qa/pet/pet-capture.mjs [outDir]
// Env: CHROME = path to chrome.exe (default: the Program Files install), PET_PROTO_PORT = dev server port (5179).
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromePath } from './chrome.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const proto = path.join(root, 'ui-prototypes', 'pet-prototype');
const { chromium } = await import(pathToFileURL(path.join(proto, 'node_modules', 'playwright', 'index.mjs')).href);

const outDir = path.resolve(process.argv[2] ?? path.join(process.env.TEMP ?? '.', 'pet-capture'));
const port = process.env.PET_PROTO_PORT ?? '5179';
const protoUrl = `http://localhost:${port}/`;
const pageUrl = pathToFileURL(path.join(root, 'ui-prototypes', 'pet.html')).href + '?shot';
const offsets = [0, 150, 350];

mkdirSync(outDir, { recursive: true });

// True only for a server that serves this prototype: the manifest must come back as JSON, not as the SPA fallback page.
async function servesPrototype() {
  try {
    const res = await fetch(`${protoUrl}pet/manifest.json`);
    return res.ok && (await res.json()).id === 'dog';
  } catch {
    return false;
  }
}

// Reuse a healthy server already on the port; otherwise start vite and stop it at the end.
// A server that answers but does not serve the prototype is refused, not reused: it would fail mid-run.
async function ensureDevServer() {
  if (await servesPrototype()) return null;
  const taken = await fetch(protoUrl).then(() => true, () => false);
  if (taken) throw new Error(`port ${port} is taken by a server that does not serve the pet prototype; stop it or set PET_PROTO_PORT`);
  const vite = spawn(process.execPath, [path.join(proto, 'node_modules', 'vite', 'bin', 'vite.js'), '--port', port, '--strictPort'], {
    cwd: proto,
    stdio: 'ignore',
  });
  for (let i = 0; i < 100; i += 1) {
    await new Promise(r => setTimeout(r, 200));
    if (await servesPrototype()) return vite;
  }
  vite.kill();
  throw new Error(`prototype dev server did not start on ${protoUrl}`);
}

const vite = await ensureDevServer();
const browser = await chromium.launch({ executablePath: chromePath() });
const errors = [];
try {
  const protoPage = await browser.newPage({ viewport: { width: 700, height: 700 }, deviceScaleFactor: 2 });
  const page = await browser.newPage({ viewport: { width: 420, height: 620 }, deviceScaleFactor: 2 });
  protoPage.on('pageerror', e => errors.push('proto: ' + e));
  page.on('pageerror', e => errors.push('page: ' + e));

  await protoPage.goto(protoUrl);
  await protoPage.waitForSelector('#pet');
  await page.goto(pageUrl);
  await page.click('#tog');
  await page.waitForTimeout(400);

  const protoCanvas = protoPage.locator('#pet');
  const pageCanvas = page.locator('#pet');
  const names = { acts: [], accs: [] };

  async function snap(name) {
    for (const [i, ms] of offsets.entries()) {
      if (i > 0) await protoPage.waitForTimeout(ms - offsets[i - 1]);
      await protoCanvas.screenshot({ path: path.join(outDir, `${name}-proto-${i}.png`) });
      await pageCanvas.screenshot({ path: path.join(outDir, `${name}-page-${i}.png`) });
    }
  }

  const actions = await protoPage.$$eval('button[data-action]', bs => bs.map(b => b.dataset.action));
  for (const a of actions) {
    await protoPage.click(`button[data-action="${a}"]`);
    await page.click(`button[data-act="${a}"]`);
    await snap(`act-${a}`);
    names.acts.push(`act-${a}`);
  }
  await protoPage.click('button[data-action="idle"]');
  await page.click('button[data-act="idle"]');

  const accs = await protoPage.$$eval('button[data-acc]', bs => bs.map(b => b.dataset.acc));
  for (const id of accs) {
    await protoPage.click(`button[data-acc="${id}"]`);
    await page.click(`button[data-acc="${id}"]`);
    await snap(`acc-${id}`);
    names.accs.push(`acc-${id}`);
    await protoPage.click(`button[data-acc="${id}"]`);
    await page.click(`button[data-acc="${id}"]`);
  }

  // Montage: one row per name, prototype frames then page frames, so the two can be compared by eye.
  for (const [file, rows] of [['montage-acts', names.acts], ['montage-accs', names.accs]]) {
    const html = montageHtml(rows);
    const htmlPath = path.join(outDir, `${file}.html`);
    writeFileSync(htmlPath, html);
    const montage = await browser.newPage({ viewport: { width: 1000, height: 900 } });
    await montage.goto(pathToFileURL(htmlPath).href);
    await montage.waitForTimeout(300);
    await montage.screenshot({ path: path.join(outDir, `${file}.png`), fullPage: true });
    await montage.close();
  }
  console.log(`wrote ${names.acts.length} action and ${names.accs.length} accessory sets to ${outDir}`);
  console.log('errors:', JSON.stringify(errors));
  if (errors.length > 0) process.exitCode = 1;
} finally {
  await browser.close();
  vite?.kill();
}

function montageHtml(rows) {
  const cell = (src, tag) => `<div class="cell"><img src="${src}" alt=""><span class="tag">${tag}</span></div>`;
  const body = rows.map(n => `<div class="row"><div class="name">${n}</div>${
    ['proto', 'page'].map(side => [0, 1, 2].map(i => cell(`${n}-${side}-${i}.png`, `${side}${i}`)).join('')).join('<div class="sep"></div>')
  }</div>`).join('');
  return `<!doctype html><html><head><meta charset="utf-8"><style>
body{margin:0;background:#fff;font:12px sans-serif;color:#222}
.row{display:flex;align-items:center;gap:6px;border-bottom:1px solid #ddd;padding:4px 8px}
.name{width:110px;font-weight:700}
.cell{width:150px;height:150px;background:#eef0f4;position:relative}
.cell img{width:150px;height:150px;image-rendering:pixelated;display:block}
.tag{position:absolute;left:4px;top:2px;font-size:10px;color:#555}
.sep{width:14px}
</style></head><body>${body}</body></html>`;
}

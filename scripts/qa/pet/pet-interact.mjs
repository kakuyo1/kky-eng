// Drives the built settings page (ui-prototypes/pet.html) with real mouse events and checks what the pet does:
// grab and carry, release, poke, a press on empty space, sleep hiding the accessories, and switching the pet off.
// Usage (from anywhere): node scripts/qa/pet/pet-interact.mjs
// Exit code 0 when every check passes, 1 otherwise. Env: CHROME = path to chrome.exe.
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromePath } from './chrome.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const { chromium } = await import(pathToFileURL(path.join(root, 'ui-prototypes', 'pet-prototype', 'node_modules', 'playwright', 'index.mjs')).href);
const pageUrl = pathToFileURL(path.join(root, 'ui-prototypes', 'pet.html')).href + '?shot';

const browser = await chromium.launch({ executablePath: chromePath() });
const page = await browser.newPage({ viewport: { width: 420, height: 620 }, deviceScaleFactor: 2 });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));

const results = [];
function check(name, ok, detail = '') {
  results.push({ name, ok });
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
}
const action = () => page.evaluate(() => pet.engine.action);

try {
  await page.goto(pageUrl);
  await page.waitForTimeout(400);
  await page.click('#tog');
  await page.waitForTimeout(300);
  // The stage sits inside the scrolling settings body; bring it into view before reading coordinates.
  await page.locator('#pet').scrollIntoViewIfNeeded();
  await page.waitForTimeout(100);

  const box = await page.locator('#pet').boundingBox();
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height * 0.6;

  // Grab at the dog's centre (opaque), carry up and right, read mid-drag.
  await page.mouse.move(cx, cy);
  await page.mouse.down();
  await page.mouse.move(cx + 40, cy - 30, { steps: 5 });
  await page.waitForTimeout(150);
  check('grab switches to pickup', (await action()) === 'pickup', await action());
  check('carried sprite is moved', (await page.evaluate(() => document.querySelector('#pet').style.transform)).includes('translate'));
  await page.mouse.up();
  await page.waitForTimeout(100);
  check('release returns to idle', (await action()) === 'idle', await action());

  // Poke: press and release on the dog where it now stands (it stayed where it was released).
  const after = await page.locator('#pet').boundingBox();
  await page.mouse.move(after.x + after.width / 2, after.y + after.height * 0.6);
  await page.mouse.down();
  await page.mouse.up();
  await page.waitForTimeout(50);
  check('poke plays click_react', (await action()) === 'click_react', await action());

  // A press on the transparent corner of the canvas must not grab.
  await page.waitForTimeout(600);
  await page.mouse.move(after.x + 2, after.y + 2);
  await page.mouse.down();
  await page.mouse.move(after.x + 30, after.y + 30, { steps: 3 });
  await page.mouse.up();
  check('press on empty canvas does not grab', (await action()) !== 'pickup', await action());

  // Sleep hides the accessories: wear a hat, sleep, the hat chip stays pressed but the hat is not drawn.
  await page.click('[data-acc="hat"]');
  await page.click('[data-act="sleep"]');
  await page.waitForTimeout(100);
  check('sleep action is selected', (await page.getAttribute('[data-act="sleep"]', 'aria-pressed')) === 'true');
  check('hat stays worn while asleep', (await page.getAttribute('[data-acc="hat"]', 'aria-pressed')) === 'true');
  await page.click('[data-act="idle"]');

  // Switching the pet off stops the frame clock and hides the section.
  await page.click('#tog');
  check('switching off stops the pet', (await action()) === 'idle' && (await page.evaluate(() => document.getElementById('pet-section').hidden)));
} finally {
  await browser.close();
}

check('no page errors', errors.length === 0, errors.join(' | '));
const failed = results.filter(r => !r.ok).length;
console.log(`${results.length - failed}/${results.length} passed`);
if (failed > 0) process.exitCode = 1;

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { PetEngine } from '../src/pet-engine.js';

// Fixture mirrors assets/pet/manifest.json in shape; the rules are what is under test.
const manifest = {
  actions: {
    idle: { frames: 5, fps: 5, loop: true, priority: 0, blink: true, anchors: Array(5).fill([0, 0]) },
    study: { frames: 6, fps: 6, loop: true, priority: 50, blink: true, expressions: [3, 3, 0, 4, 4, 0], anchors: Array(6).fill([0, 0]) },
    celebrate: { frames: 6, fps: 10, loop: false, priority: 50, returnTo: 'idle', anchors: Array(6).fill([0, 0]) },
    click_react: { frames: 4, fps: 12, loop: false, priority: 80, returnTo: 'idle', anchors: Array(4).fill([0, 0]) },
    pickup: { frames: 4, fps: 6, loop: true, priority: 100, anchors: Array(4).fill([0, 0]) },
    look_around: { frames: 6, fps: 6, loop: false, priority: 30, returnTo: 'idle', anchors: Array(6).fill([0, 0]) },
    stretch: { frames: 6, fps: 6, loop: false, priority: 30, returnTo: 'idle', anchors: Array(6).fill([0, 0]) },
    yawn: { frames: 5, fps: 6, loop: false, priority: 30, returnTo: 'sleep', anchors: Array(5).fill([0, 0]) },
    sleep: { frames: 4, fps: 3, loop: true, priority: 40, expressions: [8, 8, 8, 8], anchors: Array(4).fill([0, 0]) }
  }
};

const make = (opts) => new PetEngine(manifest, opts);

test('loops a looping action and wraps the frame index', () => {
  const e = make();
  e.request('study');
  e.update(1000); // six frames at 6 fps (1000 ms), so the sixth frame wraps to 0
  assert.equal(e.state.frame, 0);
  assert.equal(e.state.action, 'study');
});

test('a single-play action returns to returnTo after its last frame', () => {
  const e = make();
  e.request('celebrate');
  e.update(1000 * 6 / 10 + 1); // 6 frames at 10 fps, then one more ms
  assert.equal(e.state.action, 'idle');
  assert.equal(e.state.frame, 0);
});

test('a higher-priority request interrupts, an equal or lower one is ignored', () => {
  const e = make();
  assert.equal(e.request('study'), true);
  assert.equal(e.request('celebrate'), false); // same priority, queued and dropped
  assert.equal(e.state.action, 'study');
  assert.equal(e.request('idle'), false);
  assert.equal(e.request('click_react'), true); // a poke interrupts a study event
  assert.equal(e.state.action, 'click_react');
});

test('pickup beats everything, and release returns to idle, not to the old action', () => {
  const e = make();
  e.request('study');
  assert.equal(e.request('pickup'), true);
  e.request('click_react'); // ignored while carried
  assert.equal(e.state.action, 'pickup');
  e.release();
  assert.equal(e.state.action, 'idle');
});

test('stop() returns to idle from any action', () => {
  const e = make();
  e.request('sleep');
  e.stop();
  assert.equal(e.state.action, 'idle');
});

test('frame index and expression come from the same clock', () => {
  const e = make();
  e.request('study');
  e.update(340); // two frames at 6 fps (333 ms)
  assert.equal(e.state.frame, 2);
  assert.equal(e.state.anchor[0], 0);
  assert.equal(e.state.expression, 0);
  e.update(200); // frame 3 uses the look-left eyes
  assert.equal(e.state.expression, 4);
});

test('blink runs on blink actions only and closes the eyes for three steps', () => {
  const e = make({ blinkEvery: 1000 });
  e.update(1000); // timer fires, blink starts
  assert.equal(e.state.expression, 1);
  e.update(80);
  assert.equal(e.state.expression, 2);
  e.update(80);
  assert.equal(e.state.expression, 1);
  e.update(80);
  assert.equal(e.state.expression, 0);
  e.request('sleep');
  e.update(5000);
  assert.equal(e.state.expression, 8); // sleep draws its own eyes and never blinks
});

test('pause freezes time and step() advances one frame', () => {
  const e = make();
  e.request('study');
  e.pause();
  e.update(10000);
  assert.equal(e.state.frame, 0);
  e.step();
  assert.equal(e.state.frame, 1);
  e.resume();
  assert.equal(e.state.paused, false);
});

test('setFps overrides the action fps and null restores it', () => {
  const e = make();
  e.request('study');
  e.setFps(12);
  assert.equal(e.state.fps, 12);
  e.setFps(null);
  assert.equal(e.state.fps, 6);
});

test('a random idle action starts after the cooldown and never repeats back to back', () => {
  const e = make({ random: () => 0 }); // cooldown 20 s, always the first candidate
  e.update(19999);
  assert.equal(e.state.action, 'idle');
  e.update(1); // the pick happens before any frame advances, so the new action is visible
  assert.equal(e.state.action, 'look_around');
  e.update(1100); // look_around (1 s) ends and returns to idle
  assert.equal(e.state.action, 'idle');
  e.update(19999);
  e.update(1);
  assert.equal(e.state.action, 'stretch'); // look_around is excluded this time
});

test('three quiet minutes make the dog yawn, and the yawn leads to sleep', () => {
  const e = make({ random: () => 0.999 });
  let yawned = false;
  for (let t = 0; t < 1900 && !yawned; t += 1) {
    e.update(100);
    yawned = e.state.action === 'yawn';
  }
  assert.ok(yawned, 'no yawn within three minutes of quiet');
  e.update(1000);
  assert.equal(e.state.action, 'sleep');
});

test('a request resets the quiet timer', () => {
  const e = make({ random: () => 0.999 });
  e.update(170000);
  e.request('study');
  e.stop();
  e.update(15000); // 185 s in total, but only 15 s since the request
  assert.notEqual(e.state.action, 'yawn');
});

test('engine source imports no DOM, CSS or window and no graphics API', () => {
  const src = readFileSync(fileURLToPath(new URL('../src/pet-engine.js', import.meta.url)), 'utf8');
  const code = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
  assert.doesNotMatch(code, /\bdocument\b|\bwindow\b|\bHTML\w*|\bCSS\b|\bcanvas\b|\bimport\b/i);
});

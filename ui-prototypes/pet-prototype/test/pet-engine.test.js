import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { PetEngine } from '../src/pet-engine.js';

// Fixture mirrors assets/pet/manifest.json in shape; the rules are what is under test.
const manifest = {
  actions: {
    idle: { frames: 5, fps: 5, loop: true, priority: 0, blink: true, anchors: [[0, 0], [0, 0], [0, 0], [0, 0], [0, 0]] },
    study: { frames: 6, fps: 6, loop: true, priority: 50, blink: true, anchors: Array(6).fill([0, 0]) },
    celebrate: { frames: 6, fps: 10, loop: false, priority: 50, returnTo: 'idle', anchors: Array(6).fill([0, 0]) },
    sleep: { frames: 4, fps: 4, loop: true, priority: 50, expression: 'closed', anchors: Array(4).fill([0, 0]) }
  }
};

const make = (opts) => new PetEngine(manifest, opts);

test('loops a looping action and wraps the frame index', () => {
  const e = make();
  e.request('study');
  e.update(835); // five frames at 6 fps (833 ms), a little left over
  assert.equal(e.state.frame, 5);
  e.update(1000 / 6); // sixth frame wraps to 0
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
  e.play('sleep'); // debug panel: forced switch ignores priority
  assert.equal(e.state.action, 'sleep');
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
  assert.equal(e.state.expression, 2); // sleep keeps the eyes closed, never blinks
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

test('engine source imports no DOM, CSS or window and no graphics API', () => {
  const src = readFileSync(fileURLToPath(new URL('../src/pet-engine.js', import.meta.url)), 'utf8');
  const code = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
  assert.doesNotMatch(code, /\bdocument\b|\bwindow\b|\bHTML\w*|\bCSS\b|\bcanvas\b|\bimport\b/i);
});

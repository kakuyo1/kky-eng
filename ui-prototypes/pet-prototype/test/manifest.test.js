import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../assets/pet/', import.meta.url);
const manifest = JSON.parse(readFileSync(new URL('manifest.json', root), 'utf8'));
const pngWidth = path => readFileSync(new URL(path, root)).readUInt32BE(16); // IHDR width

test('every action has one anchor per frame and a sheet of frames x canvas width', () => {
  for (const [name, a] of Object.entries(manifest.actions)) {
    assert.equal(a.anchors.length, a.frames, `${name}: anchors`);
    assert.equal(pngWidth(a.sheet), a.frames * manifest.canvas, `${name}: sheet width`);
    if (a.effect) assert.equal(pngWidth(a.effect), a.frames * manifest.canvas, `${name}: effect width`);
  }
});

test('expression sheet has the three eye states', () => {
  assert.equal(pngWidth(manifest.expression), 3 * manifest.canvas);
});

test('accessories point at real files and only at actions that exist', () => {
  for (const acc of manifest.accessories) {
    assert.equal(pngWidth(acc.asset), manifest.canvas, `${acc.id}: asset`);
    for (const action of acc.supportedActions) {
      assert.ok(manifest.actions[action], `${acc.id} supports unknown action ${action}`);
    }
  }
});

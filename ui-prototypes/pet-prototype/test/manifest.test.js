import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const root = new URL('../assets/pet/', import.meta.url);
const manifest = JSON.parse(readFileSync(new URL('manifest.json', root), 'utf8'));
const pngSize = path => {
  const buf = readFileSync(new URL(path, root));
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) }; // IHDR width and height
};

test('every action has one anchor per frame and a sheet of frames x canvas size', () => {
  for (const [name, a] of Object.entries(manifest.actions)) {
    assert.equal(a.anchors.length, a.frames, `${name}: anchors`);
    assert.deepEqual(pngSize(a.sheet), { width: a.frames * manifest.canvas, height: manifest.height }, `${name}: sheet`);
    if (a.effect) {
      assert.deepEqual(pngSize(a.effect), { width: a.frames * manifest.canvas, height: manifest.height }, `${name}: effect`);
    }
    if (a.expressions) assert.equal(a.expressions.length, a.frames, `${name}: expressions`);
  }
});

test('expression sheet has one frame per name, and every expression index is named', () => {
  const { width } = pngSize(manifest.expression);
  assert.equal(width, manifest.expressionNames.length * manifest.canvas);
  for (const a of Object.values(manifest.actions)) {
    for (const e of a.expressions ?? []) assert.ok(e < manifest.expressionNames.length, `expression ${e} out of range`);
  }
});

test('accessories point at real files, only at actions that exist, and sleep hides them all', () => {
  for (const acc of manifest.accessories) {
    assert.deepEqual(pngSize(acc.asset), { width: manifest.canvas, height: manifest.height }, `${acc.id}: asset`);
    assert.ok(acc.label, `${acc.id}: label`);
    for (const action of acc.supportedActions) {
      assert.ok(manifest.actions[action], `${acc.id} supports unknown action ${action}`);
      assert.notEqual(action, 'sleep', `${acc.id} must not show while the dog sleeps`);
    }
  }
});

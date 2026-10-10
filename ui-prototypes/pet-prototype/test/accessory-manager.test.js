import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AccessoryManager } from '../src/accessory-manager.js';

const accessories = [
  { id: 'hat', slot: 'head', zIndex: 4, supportedActions: ['idle', 'study', 'celebrate'] },
  { id: 'glasses', slot: 'face', zIndex: 3, supportedActions: ['idle', 'study', 'celebrate'] },
  { id: 'scarf', slot: 'body', zIndex: 1, supportedActions: ['idle', 'study', 'celebrate'] }
];

test('one accessory per slot: toggling a second one in the slot swaps it', () => {
  const beret = { id: 'beret', slot: 'head', zIndex: 4, supportedActions: ['idle'] };
  const m = new AccessoryManager([...accessories, beret]);
  m.toggle('hat');
  m.toggle('scarf');
  assert.deepEqual(m.wornList().map(a => a.id), ['hat', 'scarf']);
  m.toggle('beret');
  assert.deepEqual(m.wornList().map(a => a.id), ['beret', 'scarf']);
});

test('toggling the worn accessory again takes it off', () => {
  const m = new AccessoryManager(accessories);
  m.toggle('glasses');
  m.toggle('glasses');
  assert.deepEqual(m.wornList(), []);
});

test('an accessory is hidden for actions it does not support, sorted by zIndex', () => {
  const m = new AccessoryManager(accessories);
  m.toggle('hat');
  m.toggle('glasses');
  m.toggle('scarf');
  assert.deepEqual(m.visibleFor('idle').map(a => a.id), ['scarf', 'glasses', 'hat']);
  assert.deepEqual(m.visibleFor('sleep').map(a => a.id), []); // curled up: nothing is shown
});

import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { AccessoryManager } from './accessory-manager.js';
import { attachPointer } from './pet-pointer.js';
import { mountDebugPanel } from './debug-panel.js';

// publicDir is assets/, so assets/pet/* is served at /pet/*.
const BASE = '/pet/';

const manifest = await (await fetch(`${BASE}manifest.json`)).json();
const canvas = document.querySelector('#pet');
const renderer = new PetRenderer(canvas, manifest);
await renderer.load(path => BASE + path);

const engine = new PetEngine(manifest);
const accessories = new AccessoryManager(manifest.accessories);
attachPointer(canvas, { engine, renderer });
const panel = mountDebugPanel(document.querySelector('#panel'), { manifest, engine, accessories });

// One clock for the whole pet: the engine advances the frame, every layer reads it.
let last = performance.now();
function tick(now) {
  engine.update(Math.min(now - last, 100));
  last = now;
  renderer.draw(engine.state, accessories.visibleFor(engine.action));
  panel.refresh();
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);

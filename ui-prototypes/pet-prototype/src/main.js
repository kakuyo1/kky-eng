import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { AccessoryManager } from './accessory-manager.js';
import { mountDebugPanel } from './debug-panel.js';

// publicDir is assets/, so assets/pet/* is served at /pet/*.
const BASE = '/pet/';

const manifest = await (await fetch(`${BASE}manifest.json`)).json();
const renderer = new PetRenderer(document.querySelector('#pet'), manifest);
await renderer.load(BASE);

const engine = new PetEngine(manifest);
const accessories = new AccessoryManager(manifest.accessories);
const panel = mountDebugPanel(document.querySelector('#panel'), { engine, accessories });

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

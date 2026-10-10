/**
 * Settings-page entry: the prototype's own engine, renderer, pointer and accessory manager, bundled as one
 * script for ui-prototypes/pet.html (tools/build-pet-html.mjs). The page supplies the manifest and the
 * images as data URLs, because a file:// page can neither fetch nor load modules.
 */
import { PetEngine } from './pet-engine.js';
import { PetRenderer } from './pet-renderer.js';
import { AccessoryManager } from './accessory-manager.js';
import { attachPointer } from './pet-pointer.js';

/**
 * @param {HTMLCanvasElement} canvas
 * @param {object} manifest Parsed assets/pet/manifest.json.
 * @param {{scale: number, resolve: (path: string) => string, live?: boolean, bounds?: HTMLElement}} options
 *        `live: false` draws the idle frame once with no pointer and no clock, for thumbnails.
 * @returns {Promise<{engine: PetEngine, accessories: AccessoryManager, setRunning: (on: boolean) => void}>}
 *          `setRunning` stops the frame clock while the stage is hidden.
 */
export async function mountStage(canvas, manifest, { scale, resolve, live = true, bounds }) {
  const renderer = new PetRenderer(canvas, { ...manifest, scale });
  await renderer.load(resolve);
  const engine = new PetEngine(manifest);
  const accessories = new AccessoryManager(manifest.accessories);

  if (!live) {
    renderer.draw({ action: 'idle', frame: 0, expression: 0, anchor: [0, 0] }, []);
    return { engine, accessories, setRunning() {} };
  }

  attachPointer(canvas, { engine, renderer, bounds });
  let running = false;
  let last = 0;
  let frame = 0;
  const tick = now => {
    engine.update(Math.min(now - last, 100));
    last = now;
    renderer.draw(engine.state, accessories.visibleFor(engine.action));
    frame = requestAnimationFrame(tick);
  };
  const setRunning = on => {
    if (on === running) return;
    running = on;
    if (on) {
      last = performance.now();
      frame = requestAnimationFrame(tick);
    } else {
      cancelAnimationFrame(frame);
    }
  };
  return { engine, accessories, setRunning };
}

/**
 * Pointer input for the pet. A press only counts on an opaque pixel; a short press is a poke, and a
 * press that moves past the threshold lifts the pet. The engine picks the animation; this file moves
 * the canvas and reports what happened.
 */

/** Pixels the pointer must travel before a press becomes a drag. */
const DRAG_THRESHOLD = 4;
/** Largest tilt while carried, in degrees, so a fast flick does not flip the sprite. */
const MAX_TILT = 15;

/**
 * @param {HTMLCanvasElement} canvas
 * @param {{engine: import('./pet-engine.js').PetEngine, renderer: import('./pet-renderer.js').PetRenderer,
 *          bounds?: HTMLElement}} deps `bounds` keeps the carried sprite inside that element; default is the window.
 */
export function attachPointer(canvas, { engine, renderer, bounds }) {
  const place = { x: 0, y: 0, tilt: 0 };
  let press = null;

  /** Canvas pixel under the pointer is opaque, so transparent margins do not grab. */
  const opaqueAt = event => {
    const r = canvas.getBoundingClientRect();
    const x = Math.floor((event.clientX - r.left) * canvas.width / r.width);
    const y = Math.floor((event.clientY - r.top) * canvas.height / r.height);
    if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return false;
    return renderer.ctx.getImageData(x, y, 1, 1).data[3] > 0;
  };

  const paint = () => {
    canvas.style.transform = `translate(${place.x}px, ${place.y}px) rotate(${place.tilt}deg)`;
  };

  canvas.addEventListener('pointermove', event => {
    if (press) return;
    canvas.style.cursor = opaqueAt(event) ? 'grab' : 'default';
  });

  canvas.addEventListener('pointerdown', event => {
    if (event.button !== 0 || !opaqueAt(event)) return;
    canvas.setPointerCapture(event.pointerId);
    press = {
      x: event.clientX,
      y: event.clientY,
      lastX: event.clientX,
      startX: place.x,
      startY: place.y,
      bounds: canvas.getBoundingClientRect(),
      dragging: false
    };
    event.preventDefault();
  });

  canvas.addEventListener('pointermove', event => {
    if (!press) return;
    const dx = event.clientX - press.x;
    const dy = event.clientY - press.y;
    if (!press.dragging) {
      if (Math.hypot(dx, dy) < DRAG_THRESHOLD) return;
      press.dragging = true;
      engine.handle('DragStart');
      canvas.style.cursor = 'grabbing';
    }
    const b = press.bounds;
    // Keep the whole sprite inside the area while it is carried.
    const area = bounds ? bounds.getBoundingClientRect() : { left: 0, top: 0, right: window.innerWidth, bottom: window.innerHeight };
    const clampedX = Math.min(Math.max(dx, area.left - b.left), area.right - b.right);
    const clampedY = Math.min(Math.max(dy, area.top - b.top), area.bottom - b.bottom);
    place.x = press.startX + clampedX;
    place.y = press.startY + clampedY;
    const swing = (event.clientX - press.lastX) * 1.2;
    place.tilt = Math.min(Math.max(place.tilt * 0.6 + swing * 0.4, -MAX_TILT), MAX_TILT);
    press.lastX = event.clientX;
    paint();
  });

  const finish = (event, clicked) => {
    if (!press) return;
    if (press.dragging) engine.handle('DragEnd');
    else if (clicked) engine.handle('Click');
    press = null;
    place.tilt = 0;
    paint();
    canvas.style.cursor = opaqueAt(event) ? 'grab' : 'default';
  };

  canvas.addEventListener('pointerup', event => finish(event, true));
  canvas.addEventListener('pointercancel', event => finish(event, false));
}

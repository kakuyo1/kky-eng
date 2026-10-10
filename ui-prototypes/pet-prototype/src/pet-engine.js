/**
 * Action state machine and frame clock. Pure logic: no DOM, no CSS, no window, no graphics API.
 * The renderer reads `state` each frame and draws every layer at the same frame index.
 */

/** Duration of one blink step in ms. */
const BLINK_STEP_MS = 80;
/** Expression sheet indices: 0 open, 1 half, 2 closed. */
const BLINK_SEQUENCE = [1, 2, 1];

export class PetEngine {
  /**
   * @param {object} manifest Parsed assets/pet/manifest.json (only `actions` is read here).
   * @param {{blinkEvery?: number}} [options] Ms between blinks.
   */
  constructor(manifest, { blinkEvery = 4000 } = {}) {
    this.actions = manifest.actions;
    this.blinkEvery = blinkEvery;
    this.action = 'idle';
    this.frame = 0;
    this.acc = 0;
    this.blinkClock = 0;
    this.blinkT = null;
    this.paused = false;
    this.fpsOverride = null;
  }

  get def() {
    return this.actions[this.action];
  }

  get fps() {
    return this.fpsOverride ?? this.def.fps;
  }

  get expression() {
    if (this.blinkT !== null) return BLINK_SEQUENCE[Math.floor(this.blinkT / BLINK_STEP_MS)];
    return this.def.expression === 'closed' ? 2 : 0;
  }

  get state() {
    return {
      action: this.action,
      frame: this.frame,
      expression: this.expression,
      fps: this.fps,
      paused: this.paused,
      anchor: this.def.anchors[this.frame]
    };
  }

  /** Event from the business side. Obeys priority: equal or lower requests are dropped while busy. */
  request(name) {
    const next = this.actions[name];
    if (this.action !== 'idle' && this.def.priority >= next.priority) return false;
    this.enter(name);
    return true;
  }

  /** Debug-panel switch. Ignores priority. */
  play(name) {
    this.enter(name);
  }

  /** Ends any action and returns to idle. */
  stop() {
    this.enter('idle');
  }

  pause() {
    this.paused = true;
  }

  resume() {
    this.paused = false;
  }

  /** Advances one frame, used for frame-by-frame review while paused. */
  step() {
    this.advance();
  }

  /** @param {number|null} fps Overrides every action's fps; null restores each action's own. */
  setFps(fps) {
    this.fpsOverride = fps;
  }

  update(deltaMs) {
    if (this.paused) return;
    this.tickBlink(deltaMs);
    this.acc += deltaMs;
    const frameMs = 1000 / this.fps;
    while (this.acc >= frameMs) {
      this.acc -= frameMs;
      this.advance();
    }
  }

  enter(name) {
    this.action = name;
    this.frame = 0;
    this.acc = 0;
    this.blinkClock = 0;
    this.blinkT = null;
  }

  advance() {
    const def = this.def;
    if (this.frame + 1 < def.frames) {
      this.frame += 1;
    } else if (def.loop) {
      this.frame = 0;
    } else {
      this.enter(def.returnTo ?? 'idle');
    }
  }

  tickBlink(deltaMs) {
    if (!this.def.blink) {
      this.blinkT = null;
      return;
    }
    if (this.blinkT === null) {
      this.blinkClock += deltaMs;
      if (this.blinkClock >= this.blinkEvery) {
        this.blinkClock = 0;
        this.blinkT = 0;
      }
      return;
    }
    this.blinkT += deltaMs;
    if (this.blinkT >= BLINK_SEQUENCE.length * BLINK_STEP_MS) this.blinkT = null;
  }
}

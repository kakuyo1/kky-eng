/**
 * Action state machine and frame clock. Pure logic: no DOM, no CSS, no window, no graphics API.
 * The renderer reads `state` each frame and draws every layer at the same frame index.
 */

/** Duration of one blink step in ms. */
const BLINK_STEP_MS = 80;
/** Expression sheet indices for one blink: half, closed, half. */
const BLINK_SEQUENCE = [1, 2, 1];
/** Quiet time with no request before the dog yawns and falls asleep. */
const QUIET_MS = 180000;
/** Idle wait between random actions: this base, plus up to the same again at random. */
const IDLE_BASE_MS = 20000;
/** Actions the idle timer picks from. */
const RANDOM_IDLE = ['look_around', 'stretch'];

export class PetEngine {
  /**
   * @param {object} manifest Parsed assets/pet/manifest.json (only `actions` is read here).
   * @param {{blinkEvery?: number, random?: () => number}} [options] Ms between blinks; random source in [0, 1).
   */
  constructor(manifest, { blinkEvery = 4000, random = Math.random } = {}) {
    this.actions = manifest.actions;
    this.blinkEvery = blinkEvery;
    this.random = random;
    this.action = 'idle';
    this.frame = 0;
    this.acc = 0;
    this.blinkClock = 0;
    this.blinkT = null;
    this.paused = false;
    this.fpsOverride = null;
    this.quietMs = 0;
    this.idleMs = 0;
    this.idleWait = this.nextIdleWait();
    this.lastIdle = null;
  }

  get def() {
    return this.actions[this.action];
  }

  get fps() {
    return this.fpsOverride ?? this.def.fps;
  }

  get expression() {
    if (this.blinkT !== null) return BLINK_SEQUENCE[Math.floor(this.blinkT / BLINK_STEP_MS)];
    return this.def.expressions?.[this.frame] ?? 0;
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
    this.quietMs = 0;
    const next = this.actions[name];
    if (this.action !== 'idle' && this.def.priority >= next.priority) return false;
    this.enter(name);
    return true;
  }

  /** Debug-panel switch and pointer input. Ignores priority. */
  play(name) {
    this.quietMs = 0;
    this.enter(name);
  }

  /** Ends any action and returns to idle. */
  stop() {
    this.enter('idle');
  }

  /** Pointer released: a drag ends in idle, not in the action the dog had before it was lifted. */
  release() {
    if (this.action === 'pickup') this.stop();
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
    this.tickIdle(deltaMs);
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
    if (name === 'idle') {
      this.idleMs = 0;
      this.idleWait = this.nextIdleWait();
    }
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

  /** Idle only: a random small action after a cooldown, or a yawn after a long quiet spell. */
  tickIdle(deltaMs) {
    this.quietMs += deltaMs;
    if (this.action !== 'idle') return;
    if (this.quietMs >= QUIET_MS) {
      this.quietMs = 0;
      this.enter('yawn');
      return;
    }
    this.idleMs += deltaMs;
    if (this.idleMs >= this.idleWait) this.enter(this.pickRandomIdle());
  }

  pickRandomIdle() {
    const choices = RANDOM_IDLE.filter(name => name !== this.lastIdle);
    this.lastIdle = choices[Math.floor(this.random() * choices.length)];
    return this.lastIdle;
  }

  nextIdleWait() {
    return IDLE_BASE_MS + this.random() * IDLE_BASE_MS;
  }
}

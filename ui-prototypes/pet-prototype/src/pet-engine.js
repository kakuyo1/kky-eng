/**
 * Action state machine and frame clock (PHASE3 3.2 and 3.3). Pure logic: no DOM, no CSS, no window, no graphics API.
 * The renderer reads `state` each frame and draws every layer at the same frame index.
 *
 * The rules mirror src/core/pet/pet_state.cpp, which is the specification's implementation.
 */

/** Duration of one blink step in ms. */
const BLINK_STEP_MS = 80;
/** Expression sheet indices for one blink: half, closed, half. */
const BLINK_SEQUENCE = [1, 2, 1];
/** Quiet time with no event before the dog yawns and falls asleep. */
const QUIET_MS = 180000;
/** Idle wait before a random action: this base, plus up to the same again at random. */
const IDLE_BASE_MS = 20000;
/** Actions the idle timer picks from. */
const RANDOM_IDLE = ['look_around', 'stretch'];
/** Priority tiers, lowest first. A request at or below the tier of the action now playing is dropped. */
const TIER = { idle: 0, situational: 1, event: 2, click: 3, drag: 4 };

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
    this.priority = TIER.idle;
    this.frame = 0;
    this.acc = 0;
    this.blinkClock = 0;
    this.blinkT = null;
    this.paused = false;
    this.fpsOverride = null;
    this.dragging = false;
    /** The looping state an event keeps: reading while an explanation is up, sleep while the budget is paused. */
    this.held = null;
    /** Where the current action goes when it ends, from its returnTo. Cleared by any event. */
    this.pendingReturn = null;
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

  /**
   * Applies one event by its type. Events carry no text (PHASE3 3.2).
   * @param {'DragStart'|'DragEnd'|'Click'|'SelectionShown'|'ExplanationRequested'|'ExplanationShown'|'ExplanationHidden'|
   *         'KnownMarked'|'NewWordMarked'|'BudgetPaused'|'BudgetResumed'|'UpdateAvailable'} event
   */
  handle(event) {
    this.pendingReturn = null;
    this.quietMs = 0;
    switch (event) {
      case 'DragStart':
        if (this.dragging) return;
        this.dragging = true;
        this.enter('pickup', TIER.drag);
        return;
      case 'DragEnd':
        if (!this.dragging) return;
        this.dragging = false;
        this.enter('idle', TIER.idle); // releasing lands on idle, not on what the dog did before the grab
        return;
      case 'Click':
        if (!this.dragging) this.request('click_react', TIER.click);
        return;
      case 'SelectionShown':
        this.request('look_around', TIER.event);
        return;
      case 'ExplanationRequested':
        this.request('thinking', TIER.event);
        return;
      case 'ExplanationShown':
        this.held = 'study';
        this.request('study', TIER.event);
        return;
      case 'ExplanationHidden':
        if (this.held === 'study') this.held = null;
        if (this.action === 'study') this.settle();
        return;
      case 'KnownMarked':
      case 'UpdateAvailable':
        this.request('celebrate', TIER.event);
        return;
      case 'NewWordMarked':
        this.request('encourage', TIER.event);
        return;
      case 'BudgetPaused':
        this.held = 'sleep';
        this.request('sleep', TIER.event);
        return;
      case 'BudgetResumed':
        if (this.held === 'sleep') this.held = null;
        if (this.action === 'sleep') this.settle();
        return;
      default:
        throw new Error(`unknown pet event: ${event}`);
    }
  }

  /** Debug panel and the settings preview: plays any action now, ignoring priority. */
  play(name) {
    this.quietMs = 0;
    this.enter(name, TIER.event);
  }

  /** Ends any action and returns to idle, dropping whatever was held. */
  stop() {
    this.held = null;
    this.dragging = false;
    this.enter('idle', TIER.idle);
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
    this.tick(deltaMs);
    this.acc += deltaMs;
    const frameMs = 1000 / this.fps;
    while (this.acc >= frameMs) {
      this.acc -= frameMs;
      this.advance();
    }
  }

  /**
   * Starts an action if the priority allows it. The same tier as the action now playing is dropped, so an
   * event waits for the one-shot before it. Thinking is the wait for an explanation, so any event replaces it.
   * @returns {boolean} Whether the action started.
   */
  request(name, tier) {
    const givesWayToThinking = this.action === 'thinking' && tier === TIER.event;
    if (tier <= this.priority && !givesWayToThinking) return false;
    this.enter(name, tier);
    return true;
  }

  enter(name, tier) {
    this.action = name;
    this.priority = tier;
    this.pendingReturn = this.actions[name].returnTo ?? null;
    this.frame = 0;
    this.acc = 0;
    this.blinkClock = 0;
    this.blinkT = null;
    if (name === 'idle') {
      this.idleMs = 0;
      this.idleWait = this.nextIdleWait();
    }
  }

  /** A one-shot ended, or a held state was released: go to the pending return, else the held state, else idle. */
  settle() {
    const next = this.pendingReturn ?? this.held ?? 'idle';
    this.enter(next, next === 'idle' ? TIER.idle : TIER.event);
  }

  advance() {
    const def = this.def;
    if (this.frame + 1 < def.frames) {
      this.frame += 1;
    } else if (def.loop) {
      this.frame = 0;
    } else {
      this.settle();
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

  /** Quiet time yawns the dog into sleep; idle time picks a random situational action. */
  tick(deltaMs) {
    this.quietMs += deltaMs;
    if (this.quietMs >= QUIET_MS) {
      this.quietMs = 0;
      this.request('yawn', TIER.situational); // the yawn's returnTo is sleep
      return;
    }
    if (this.action !== 'idle') return;
    this.idleMs += deltaMs;
    if (this.idleMs >= this.idleWait) this.request(this.pickRandomIdle(), TIER.situational);
  }

  pickRandomIdle() {
    const choices = RANDOM_IDLE.filter(name => name !== this.lastIdle);
    this.lastIdle = choices[Math.min(Math.floor(this.random() * choices.length), choices.length - 1)];
    return this.lastIdle;
  }

  nextIdleWait() {
    return IDLE_BASE_MS + this.random() * IDLE_BASE_MS;
  }
}

/**
 * Debug panel: plays actions, simulates learning events, pauses and steps, sets the fps override and
 * toggles accessories. Labels come from the manifest; it only calls the engine and accessory manager
 * through their public methods.
 */

/** Each button sends one event type, the same names the page sends in the app (PHASE3 3.2). */
const EVENTS = [
  { label: '标记已会 → 庆祝', type: 'KnownMarked' },
  { label: '发现新版本 → 庆祝', type: 'UpdateAvailable' },
  { label: '解释请求 → 思考', type: 'ExplanationRequested' },
  { label: '解释气泡出现 → 阅读', type: 'ExplanationShown' },
  { label: '解释气泡消失 → 待机', type: 'ExplanationHidden' },
  { label: '标记新词 → 鼓励', type: 'NewWordMarked' },
  { label: '选区动作条出现 → 张望', type: 'SelectionShown' },
  { label: '预算暂停 → 睡觉', type: 'BudgetPaused' },
  { label: '预算恢复 → 待机', type: 'BudgetResumed' }
];

const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  node.append(...children);
  return node;
};

const section = (title, ...children) => el('section', {}, el('h2', { textContent: title }), el('div', { className: 'row' }, ...children));

/**
 * @param {HTMLElement} root
 * @param {{manifest: object, engine: import('./pet-engine.js').PetEngine, accessories: import('./accessory-manager.js').AccessoryManager}} deps
 * @returns {{refresh: () => void}} Call once per frame to update the status and pressed states.
 */
export function mountDebugPanel(root, { manifest, engine, accessories }) {
  const status = el('pre', { id: 'status' });
  const actionButtons = Object.entries(manifest.actions).map(([name, def]) => {
    const b = el('button', { type: 'button', textContent: def.label, onclick: () => engine.play(name) });
    b.dataset.action = name;
    return b;
  });
  const eventButtons = EVENTS.map(({ label, type }) =>
    el('button', { type: 'button', textContent: label, onclick: () => engine.handle(type) }));
  const pauseButton = el('button', { type: 'button', onclick: () => (engine.paused ? engine.resume() : engine.pause()) });
  const stepButton = el('button', { type: 'button', textContent: '逐帧', onclick: () => engine.step() });
  const fpsInput = el('input', {
    type: 'number', min: 0, max: 24, value: 0, title: '0 表示沿用每个动作自己的帧率',
    onchange: () => engine.setFps(Number(fpsInput.value) > 0 ? Number(fpsInput.value) : null)
  });
  const accessoryButtons = manifest.accessories.map(({ id, label }) => {
    const b = el('button', { type: 'button', textContent: label, onclick: () => accessories.toggle(id) });
    b.dataset.acc = id;
    return b;
  });

  root.replaceChildren(
    el('h2', { textContent: '调试' }),
    status,
    section('动作', ...actionButtons),
    section('模拟事件', ...eventButtons),
    section('播放', pauseButton, stepButton, el('label', { className: 'fps' }, 'FPS ', fpsInput)),
    section('配饰', ...accessoryButtons)
  );

  return {
    refresh() {
      const s = engine.state;
      status.textContent = [
        `动作  ${s.action}`,
        `帧    ${s.frame}`,
        `表情  ${manifest.expressionNames[s.expression]}`,
        `帧率  ${s.fps}`,
        `状态  ${s.paused ? '已暂停' : '播放中'}`
      ].join('\n');
      pauseButton.textContent = s.paused ? '继续' : '暂停';
      stepButton.disabled = !s.paused;
      for (const b of actionButtons) b.setAttribute('aria-pressed', String(b.dataset.action === s.action));
      for (const b of accessoryButtons) b.setAttribute('aria-pressed', String(accessories.isWorn(b.dataset.acc)));
    }
  };
}

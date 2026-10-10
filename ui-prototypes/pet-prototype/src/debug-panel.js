/**
 * Debug panel: plays actions, simulates learning events, pauses and steps, sets the fps override and
 * toggles accessories. It only calls the engine and accessory manager through their public methods.
 */

const ACTION_LABELS = { idle: '待机', study: '阅读', celebrate: '庆祝', sleep: '睡觉' };

const EVENTS = [
  { label: '标记已会 → 庆祝', run: e => e.request('celebrate') },
  { label: '解释气泡出现 → 阅读', run: e => e.request('study') },
  { label: '解释气泡消失 → 待机', run: e => { if (e.action === 'study') e.stop(); } },
  { label: '预算暂停 → 睡觉', run: e => e.request('sleep') },
  { label: '预算恢复 → 待机', run: e => { if (e.action === 'sleep') e.stop(); } }
];

const ACCESSORY_LABELS = { hat: '帽子', glasses: '眼镜', scarf: '围巾' };

const el = (tag, props = {}, ...children) => {
  const node = Object.assign(document.createElement(tag), props);
  node.append(...children);
  return node;
};

const section = (title, ...children) => el('section', {}, el('h2', { textContent: title }), el('div', { className: 'row' }, ...children));

/**
 * @param {HTMLElement} root
 * @param {{engine: import('./pet-engine.js').PetEngine, accessories: import('./accessory-manager.js').AccessoryManager}} deps
 * @returns {{refresh: () => void}} Call once per frame to update the status and pressed states.
 */
export function mountDebugPanel(root, { engine, accessories }) {
  const status = el('pre', { id: 'status' });
  const actionButtons = Object.entries(ACTION_LABELS).map(([name, label]) => {
    const b = el('button', { type: 'button', textContent: label, onclick: () => engine.play(name) });
    b.dataset.action = name;
    return b;
  });
  const eventButtons = EVENTS.map(({ label, run }) =>
    el('button', { type: 'button', textContent: label, onclick: () => run(engine) }));
  const pauseButton = el('button', { type: 'button', onclick: () => (engine.paused ? engine.resume() : engine.pause()) });
  const stepButton = el('button', { type: 'button', textContent: '逐帧', onclick: () => engine.step() });
  const fpsInput = el('input', {
    type: 'number', min: 0, max: 24, value: 0, title: '0 表示沿用每个动作自己的帧率',
    onchange: () => engine.setFps(Number(fpsInput.value) > 0 ? Number(fpsInput.value) : null)
  });
  const accessoryButtons = Object.entries(ACCESSORY_LABELS).map(([id, label]) => {
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
        `表情  ${['睁眼', '半闭', '闭眼'][s.expression]}`,
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

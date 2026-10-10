/**
 * Canvas drawing for the pet. Reads the engine state and the visible accessories; it never decides
 * what plays. Body, expression and accessories follow the action's anchor; effects stay where they were drawn.
 */
export class PetRenderer {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {object} manifest Parsed assets/pet/manifest.json.
   */
  constructor(canvas, manifest) {
    this.manifest = manifest;
    this.width = manifest.canvas;
    this.height = manifest.height;
    this.scale = manifest.scale;
    canvas.width = this.width * this.scale;
    canvas.height = this.height * this.scale;
    this.ctx = canvas.getContext('2d');
    this.ctx.imageSmoothingEnabled = false;
    this.canvas = canvas;
    this.images = new Map();
  }

  /**
   * Loads every sheet and accessory PNG the manifest names.
   * @param {(path: string) => string} urlOf Maps a manifest path to the URL the image loads from.
   */
  async load(urlOf) {
    const { actions, expression, accessories } = this.manifest;
    const paths = [
      ...Object.values(actions).flatMap(a => [a.sheet, a.effect].filter(Boolean)),
      expression,
      ...accessories.map(a => a.asset)
    ];
    await Promise.all(paths.map(path => new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => {
        this.images.set(path, img);
        resolve();
      };
      img.onerror = () => reject(new Error(`pet asset missing: ${path}`));
      img.src = urlOf(path);
    })));
  }

  /**
   * @param {{action: string, frame: number, expression: number, anchor: number[]}} state From PetEngine.state.
   * @param {Array<{asset: string, zIndex: number}>} accessories Worn accessories visible for this action.
   */
  draw(state, accessories) {
    const { ctx, width, height, scale, manifest } = this;
    const def = manifest.actions[state.action];
    const [dx, dy] = state.anchor;
    const layers = [
      { z: manifest.layers.body, path: def.sheet, index: state.frame, at: [dx, dy] },
      { z: manifest.layers.expression, path: manifest.expression, index: state.expression, at: [dx, dy] },
      ...(def.effect ? [{ z: manifest.layers.effect, path: def.effect, index: state.frame, at: [0, 0] }] : []),
      ...accessories.map(a => ({ z: a.zIndex, path: a.asset, index: 0, at: [dx, dy] }))
    ].sort((p, q) => p.z - q.z);

    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.setTransform(scale, 0, 0, scale, 0, 0); // draw in canvas units, 1 unit = `scale` device pixels
    for (const { path, index, at } of layers) {
      ctx.drawImage(this.images.get(path), index * width, 0, width, height, at[0], at[1], width, height);
    }
  }
}

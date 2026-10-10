/**
 * Worn accessories, one per slot. Knows nothing about the action state machine:
 * the caller asks which accessories are visible for the current action.
 */
export class AccessoryManager {
  /** @param {Array<{id: string, slot: string, zIndex: number, supportedActions: string[]}>} accessories */
  constructor(accessories) {
    this.all = accessories;
    this.worn = new Map();
  }

  find(id) {
    return this.all.find(a => a.id === id);
  }

  /** Puts the accessory on its slot, or takes it off if it is already worn there. */
  toggle(id) {
    const { slot } = this.find(id);
    if (this.worn.get(slot) === id) this.worn.delete(slot);
    else this.worn.set(slot, id);
  }

  isWorn(id) {
    return this.worn.get(this.find(id).slot) === id;
  }

  wornList() {
    return [...this.worn.values()].map(id => this.find(id));
  }

  /** Worn accessories that support `action`, bottom layer first. */
  visibleFor(action) {
    return this.wornList()
      .filter(a => a.supportedActions.includes(action))
      .sort((p, q) => p.zIndex - q.zIndex);
  }
}

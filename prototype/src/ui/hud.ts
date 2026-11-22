import { biomeName, VEHICLE } from '../sim/tuning';
import type { World } from '../sim/world';

function el(id: string): HTMLElement {
  const node = document.getElementById(id);
  if (!node) throw new Error(`missing element #${id}`);
  return node;
}

export class Hud {
  private speed = el('speed');
  private gear = el('gear');
  private rpmBar = el('rpm-bar');
  private biome = el('biome');
  private distance = el('distance');
  private pressureBar = el('pressure-bar');
  private tensionBar = el('tension-bar');
  private tensionValue = el('tension-value');
  private tensionPanel = el('top-right');
  private latches = el('latches');
  private cutter = el('cutter');
  private cutterBar = el('cutter-bar');
  private boost = el('boost');
  private boostBar = el('boost-bar');
  private surface = el('surface');
  private toast = el('toast');
  private toastTimer = 0;

  update(world: World, dt: number): void {
    const v = world.vehicle;
    const kmh = v.speed * 3.6;
    this.speed.textContent = Math.round(kmh).toString();
    this.gear.textContent = v.reverseMode ? 'R' : v.gear.toString();
    this.rpmBar.style.width = `${Math.min(100, (v.rpm / VEHICLE.redline) * 100).toFixed(1)}%`;

    this.biome.textContent = biomeName(world.distance);
    this.distance.textContent = `${Math.round(world.distance)} m`;
    this.pressureBar.style.width = `${(world.director.pressure * 100).toFixed(1)}%`;

    const worst = world.vines.vines.reduce(
      (acc, vine) => (vine.tension > acc.tension ? vine : acc),
      { tension: 0, spec: { maxTension: 1 } } as any,
    );
    const ratio = worst.tension > 0 ? Math.min(1, worst.tension / worst.spec.maxTension) : 0;
    this.tensionBar.style.width = `${(ratio * 100).toFixed(1)}%`;
    this.tensionValue.textContent = `${Math.round(ratio * 100)}%`;
    this.tensionPanel.classList.toggle('critical', ratio > 0.72);
    this.latches.textContent = world.vines.latchCount.toString();

    const cutterReady = world.vines.cutterReady;
    this.cutter.textContent = cutterReady ? 'READY' : 'CYCLING';
    this.cutterBar.style.width = `${(cutterReady ? 100 : (1 - world.vines.cutter.cooldown / VEHICLE.cutterCooldown) * 100).toFixed(1)}%`;

    const boostReady = v.boostCooldown <= 0;
    this.boost.textContent = boostReady ? 'READY' : v.boostTimer > 0 ? 'ACTIVE' : 'CHARGING';
    this.boostBar.style.width = `${(boostReady ? 100 : (1 - v.boostCooldown / (VEHICLE.boostDuration + VEHICLE.boostCooldown)) * 100).toFixed(1)}%`;

    const q = world.track.nearest(v.pos.x, v.pos.z);
    const surfName = v.wheels[2]?.surfaceMu ?? 1;
    this.surface.textContent =
      surfName < 0.62 ? 'Standing water' : surfName < 0.78 ? 'Wet mud' : surfName < 0.95 ? 'Leaf litter' : 'Packed dirt';
    void q;

    if (this.toastTimer > 0) {
      this.toastTimer -= dt;
      if (this.toastTimer <= 0) this.toast.style.opacity = '0';
    }
  }

  showToast(text: string, duration = 1.6): void {
    this.toast.textContent = text;
    this.toast.style.opacity = '1';
    this.toastTimer = duration;
  }
}

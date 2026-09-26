// ---------------------------------------------------------------------------
// Tiny DOM HUD helpers (import-safe outside the browser).
// ---------------------------------------------------------------------------

const $ = (id) => (typeof document !== 'undefined' ? document.getElementById(id) : null);

export const hud = {
  els: {
    hud: $('hud'),
    prompt: $('prompt'),
    speedo: $('speedo'),
    hpFill: $('health-fill'),
    tideFill: $('tide-fill'),
    tideText: $('tide-text'),
    objective: $('objective-text'),
    flash: $('damage-flash'),
    menu: $('menu'),
    end: $('end'),
    endTitle: $('end-title'),
    endText: $('end-text'),
  },

  show() { this.els.hud?.classList.remove('hidden'); },
  hide() { this.els.hud?.classList.add('hidden'); },

  prompt(text) {
    if (!this.els.prompt) return;
    if (text) {
      this.els.prompt.textContent = text;
      this.els.prompt.classList.add('show');
    } else {
      this.els.prompt.classList.remove('show');
    }
  },

  health(v) {
    if (!this.els.hpFill) return;
    this.els.hpFill.style.width = `${Math.max(0, v)}%`;
    this.els.hpFill.style.background = v > 55 ? '#67c17a' : v > 25 ? '#e8b64c' : '#d9534f';
  },

  tide(t, level) {
    if (!this.els.tideFill) return;
    this.els.tideFill.style.width = `${Math.round(t * 100)}%`;
    this.els.tideText.textContent = `+${level.toFixed(1)}m`;
  },

  speed(kmh) {
    if (!this.els.speedo) return;
    if (kmh === null) { this.els.speedo.innerHTML = ''; return; }
    this.els.speedo.innerHTML = `${Math.round(kmh)}<small>KM/H</small>`;
  },

  damage() {
    if (!this.els.flash) return;
    this.els.flash.style.opacity = '1';
    setTimeout(() => { this.els.flash.style.opacity = '0'; }, 140);
  },

  objective(text) { if (this.els.objective) this.els.objective.textContent = text; },
};

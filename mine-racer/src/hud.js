import { GW, GH, tidx, CFG } from './config.js';
import { formatTime } from './util.js';

// ---------------------------------------------------------------------------
// DOM HUD: timer, checkpoints, hearts, minimap, warnings, overlays.
// ---------------------------------------------------------------------------
export class HUD {
  constructor() {
    const $ = (id) => document.getElementById(id);
    this.el = {
      time: $('time'), best: $('best'), cp: $('cp-line'), hearts: $('hearts'),
      speed: $('speed'), warn: $('warn'), msg: $('msg'), vignette: $('vignette'),
      overlay: $('overlay'), result: $('result'), wreck: $('wreck'), err: $('err'),
      resultTime: $('result-time'), resultDetail: $('result-detail'), resultSub: $('result-sub'),
      seedLine: $('seed-line'),
      btnStart: $('btn-start'), btnAgain: $('btn-again'), btnNew: $('btn-newmap'),
      minimap: $('minimap'),
    };
    this.mm = this.el.minimap.getContext('2d');
    this._staticMap = null;
    this._vigTimer = null;
  }

  onButtons({ start, again, newMap }) {
    this.el.btnStart.addEventListener('click', start);
    this.el.btnAgain.addEventListener('click', again);
    this.el.btnNew.addEventListener('click', newMap);
  }

  showError(err) {
    document.getElementById('err-text').textContent = String(err && err.stack || err);
    this.el.err.classList.remove('hidden');
  }

  setSeedLine(seed) { this.el.seedLine.textContent = `mine seed #${seed} · rails shared with ghost carts · v0.1`; }
  hideOverlay() { this.el.overlay.classList.add('hidden'); }
  showResult(t, best, hits) {
    this.el.resultTime.textContent = formatTime(t);
    const isBest = best != null && Math.abs(t - best) < 0.005;
    this.el.resultSub.textContent = isBest ? '★ NEW PERSONAL BEST ★' : 'SHAFT 07 CLEARED';
    this.el.resultDetail.innerHTML =
      `time <b>${formatTime(t)}</b> · best <b>${formatTime(best)}</b> · cart hits taken <b>${hits}</b>`;
    this.el.result.classList.remove('hidden');
  }
  hideResult() { this.el.result.classList.add('hidden'); }
  showWreck() { this.el.wreck.classList.remove('hidden'); }
  hideWreck() { this.el.wreck.classList.add('hidden'); }

  setTime(t, best) {
    this.el.time.textContent = formatTime(t);
    this.el.best.textContent = 'BEST ' + formatTime(best);
  }
  setCP(i, n) { this.el.cp.textContent = `◈ CHECKPOINT ${i}/${n}`; }
  setHearts(h) { this.el.hearts.textContent = '♥'.repeat(h) + '♡'.repeat(Math.max(0, CFG.HEALTH - h)); }
  setSpeed(kmh) { this.el.speed.textContent = Math.round(kmh); }
  warn(on, text = '⚠ CART INCOMING ⚠') {
    this.el.warn.textContent = text;
    this.el.warn.style.display = on ? 'block' : 'none';
  }
  message(text, color = '#f5a83b') {
    if (text == null) { this.el.msg.style.display = 'none'; return; }
    this.el.msg.textContent = text;
    this.el.msg.style.color = color;
    this.el.msg.style.display = 'block';
  }
  hit() {
    this.el.vignette.classList.add('hit');
    clearTimeout(this._vigTimer);
    this._vigTimer = setTimeout(() => this.el.vignette.classList.remove('hit'), 350);
  }

  // ---- minimap -------------------------------------------------------------
  buildMinimap(maze, railLines, finishPos) {
    const S = 188, T = CFG.TILE;
    const c = document.createElement('canvas');
    c.width = c.height = S;
    const g = c.getContext('2d');
    this.mmScale = S / (GW * T);
    const w2m = (wx) => (wx + (GW * T) / 2) * this.mmScale;
    this.w2m = w2m;
    g.fillStyle = '#0c0a09'; g.fillRect(0, 0, S, S);
    // tunnel floors
    g.fillStyle = '#4a3f30';
    const px = T * this.mmScale;
    for (let x = 0; x < GW; x++) for (let z = 0; z < GH; z++) {
      if (!maze.open[tidx(x, z)]) continue;
      g.fillRect(x * px, z * px, px + 0.5, px + 0.5);
    }
    // rail lines
    g.strokeStyle = '#d9a241'; g.lineWidth = 1.4;
    for (const line of railLines) {
      g.beginPath();
      for (let i = 0; i <= 40; i++) {
        const p = line.curve.getPointAt(i / 40);
        if (i === 0) g.moveTo(w2m(p.x), w2m(p.z));
        else g.lineTo(w2m(p.x), w2m(p.z));
      }
      g.stroke();
    }
    // finish
    g.fillStyle = '#ffd166';
    g.fillRect(w2m(finishPos.x) - 3, w2m(finishPos.z) - 3, 6, 6);
    this._staticMap = c;
  }

  drawMinimap(player, yaw, carts, checkpoints, nextCP, time) {
    if (!this._staticMap) return;
    const g = this.mm, S = 188;
    g.drawImage(this._staticMap, 0, 0);
    // checkpoints
    checkpoints.forEach((cp, i) => {
      const x = this.w2m(cp.pos.x), y = this.w2m(cp.pos.z);
      const pulse = 2.4 + Math.sin(time * 5) * 0.9;
      g.beginPath();
      g.arc(x, y, i === nextCP ? pulse + 2 : 2.4, 0, 7);
      g.fillStyle = cp.done ? '#3f6d5a' : i === nextCP ? '#59e6ff' : '#1b5f70';
      g.fill();
    });
    // carts
    for (const c of carts) {
      g.beginPath();
      g.arc(this.w2m(c.g.position.x), this.w2m(c.g.position.z), 2.6, 0, 7);
      g.fillStyle = c.state === 'run' ? '#ff6a2a' : '#7a6a55';
      g.fill();
    }
    // player arrow: rotate canvas so "up" = car forward (sin yaw, cos yaw)
    const x = this.w2m(player.x), y = this.w2m(player.z);
    g.save();
    g.translate(x, y);
    g.rotate(Math.PI - yaw);
    g.beginPath();
    g.moveTo(0, -5); g.lineTo(3.4, 4); g.lineTo(0, 2); g.lineTo(-3.4, 4);
    g.closePath();
    g.fillStyle = '#ffffff';
    g.fill();
    g.restore();
  }
}

/* ============================================================
   Frontier — Epic Games sign-in prototype
   No backend. Everything below is front-end theatre.
   ============================================================ */
(() => {
  'use strict';

  const $  = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------- wallpaper data ---------- */
  const SLIDES = [
    {
      title: 'Neon Reaches',
      copy: 'A new frontier opens on the rim of a dying star. Squad up, drop in, and claim it.',
      tags: ['Co-op', 'Open world', 'Ray tracing']
    },
    {
      title: 'Hollow Cathedral',
      copy: 'The old gods left the lights on. Descend the vault, steal the relic, outrun the choir.',
      tags: ['Dungeon crawl', 'Loot', 'Permadeath']
    },
    {
      title: 'Twin-Sun Flats',
      copy: 'Three hundred square miles of mirror-glass salt. One city floating above it. Zero cover.',
      tags: ['Extraction', 'Vehicles', 'PvPvE']
    }
  ];

  const DURATION = 9000;                 // ms per wallpaper
  const slidesEl = $('#slides');
  const slides   = $$('.slide', slidesEl);
  const switcher = $('#switcher');
  const showcase = $('#showcase');
  const titleEl  = $('#slideTitle');
  const copyEl   = $('#slideCopy');
  const metaEl   = $('.showcase__meta');

  let index = 0;
  let timer = null;
  let start = performance.now();
  let rafId = null;

  /* ---------- build the switcher buttons ---------- */
  slides.forEach((_, i) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.setAttribute('role', 'tab');
    b.setAttribute('aria-label', `Wallpaper ${i + 1}: ${SLIDES[i].title}`);
    b.innerHTML = `<span>${i + 1}</span>`;
    b.addEventListener('click', () => go(i, true));
    switcher.appendChild(b);
  });
  const dots = $$('button', switcher);

  /* ---------- crossfade + caption swap ---------- */
  function go(next, manual = false) {
    if (next === index && manual) return;
    const from = slides[index];
    const to   = slides[next];

    from.classList.remove('is-active');
    from.classList.add('is-leaving');
    setTimeout(() => from.classList.remove('is-leaving'), 1700);

    to.classList.add('is-active');
    index = next;

    paintCaptions();
    dots.forEach((d, i) => d.setAttribute('aria-selected', i === index ? 'true' : 'false'));

    start = performance.now();
    if (manual) restart();
  }

  function paintCaptions() {
    const s = SLIDES[index];
    titleEl.textContent = s.title;
    copyEl.textContent  = s.copy;
    metaEl.innerHTML = s.tags.map(t => `<span class="tag">${t}</span>`).join('');

    showcase.classList.remove('is-swapping');
    void showcase.offsetWidth;            // reflow → replay the animation
    showcase.classList.add('is-swapping');
  }

  /* ---------- auto-advance + progress ring ---------- */
  function tick(now) {
    const p = Math.min((now - start) / DURATION, 1);
    dots[index].style.setProperty('--p', p.toFixed(3));
    if (p >= 1) {
      go((index + 1) % slides.length);
    }
    rafId = requestAnimationFrame(tick);
  }
  function restart() {
    cancelAnimationFrame(rafId);
    dots.forEach(d => d.style.setProperty('--p', 0));
    if (!reduced) rafId = requestAnimationFrame(tick);
  }

  dots[0].setAttribute('aria-selected', 'true');
  restart();

  document.addEventListener('visibilitychange', () => {
    if (document.hidden) cancelAnimationFrame(rafId);
    else restart();
  });

  /* ---------- pointer parallax + cursor aura ---------- */
  const glow = $('#cursorGlow');
  let queued = false;
  let mx = 0, my = 0;                    // cached for the canvas loop

  window.addEventListener('pointermove', (e) => {
    mx = (e.clientX / window.innerWidth) * 2 - 1;
    my = (e.clientY / window.innerHeight) * 2 - 1;

    if (!queued) {
      queued = true;
      requestAnimationFrame(() => {
        const root = document.documentElement.style;
        root.setProperty('--mx', mx.toFixed(3));
        root.setProperty('--my', my.toFixed(3));
        root.setProperty('--gx', e.clientX + 'px');
        root.setProperty('--gy', e.clientY + 'px');
        queued = false;
      });
    }
    document.body.classList.add('pointer');
  }, { passive: true });

  window.addEventListener('pointerleave', () => document.body.classList.remove('pointer'));

  /* hover: hold the wallpaper still */
  showcase.addEventListener('mouseenter', () => cancelAnimationFrame(rafId));
  showcase.addEventListener('mouseleave', () => restart());

  /* arrow keys flip wallpapers */
  window.addEventListener('keydown', (e) => {
    if (e.target.matches('input, button, a')) return;
    if (e.key === 'ArrowRight') go((index + 1) % slides.length, true);
    if (e.key === 'ArrowLeft')  go((index - 1 + slides.length) % slides.length, true);
  });

  /* ============================================================
     EMBERS — canvas dust motes drifting up through the light
     ============================================================ */
  const canvas = $('#embers');
  const ctx = canvas.getContext('2d');
  let motes = [];
  let dpr = 1;

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width  = window.innerWidth  * dpr;
    canvas.height = window.innerHeight * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const count = Math.round((window.innerWidth * window.innerHeight) / 14000);
    motes = Array.from({ length: Math.min(count, 160) }, () => spawn(true));
  }

  function spawn(anywhere = false) {
    const depth = Math.random();                       // 0 far … 1 near
    return {
      x: Math.random() * window.innerWidth,
      y: anywhere ? Math.random() * window.innerHeight : window.innerHeight + 20,
      r: 0.5 + depth * 2.1,
      vy: -(0.12 + depth * 0.55),
      vx: (Math.random() - 0.5) * 0.22,
      sway: Math.random() * Math.PI * 2,
      swaySpeed: 0.004 + Math.random() * 0.012,
      hue: Math.random() < 0.55 ? 186 : (Math.random() < 0.5 ? 258 : 315),
      a: 0.18 + depth * 0.5,
      depth
    };
  }

  function draw() {
    ctx.clearRect(0, 0, window.innerWidth, window.innerHeight);

    for (const m of motes) {
      m.sway += m.swaySpeed;
      m.x += m.vx + Math.sin(m.sway) * 0.28;
      m.y += m.vy;

      if (m.y < -20) Object.assign(m, spawn());

      const px = m.x + mx * 26 * m.depth;
      const py = m.y + my * 18 * m.depth;

      const g = ctx.createRadialGradient(px, py, 0, px, py, m.r * 5);
      g.addColorStop(0,   `hsla(${m.hue}, 100%, 78%, ${m.a})`);
      g.addColorStop(0.4, `hsla(${m.hue}, 100%, 66%, ${m.a * 0.35})`);
      g.addColorStop(1,   `hsla(${m.hue}, 100%, 60%, 0)`);

      ctx.beginPath();
      ctx.arc(px, py, m.r * 5, 0, Math.PI * 2);
      ctx.fillStyle = g;
      ctx.fill();
    }
    requestAnimationFrame(draw);
  }

  resize();
  window.addEventListener('resize', resize);
  if (!reduced) requestAnimationFrame(draw);

  /* ============================================================
     FORM
     ============================================================ */
  const form    = $('#form');
  const card    = $('#card');
  const email   = $('#email');
  const pass    = $('#password');
  const note    = $('#formNote');
  const submit  = $('#submitBtn');
  const reveal  = $('#revealBtn');

  /* show / hide password */
  reveal.addEventListener('click', () => {
    const show = pass.type === 'password';
    pass.type = show ? 'text' : 'password';
    reveal.setAttribute('aria-pressed', String(show));
    reveal.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
    pass.focus();
  });

  /* live re-validation */
  [email, pass].forEach(input => {
    input.addEventListener('input', () => {
      input.closest('.field').classList.remove('is-invalid');
      if (note.textContent) { note.textContent = ''; note.className = 'form__note'; }
    });
  });

  function setError(input, msg) {
    const field = input.closest('.field');
    field.classList.add('is-invalid');
    $('[data-err]', field).textContent = msg;
  }

  function validate() {
    let ok = true;
    $$('.field', form).forEach(f => f.classList.remove('is-invalid'));

    const v = email.value.trim();
    if (!v) { setError(email, 'Enter your email or display name.'); ok = false; }
    else if (v.includes('@') && !/^[^\s@]+@[^\s@]+\.[a-z]{2,}$/i.test(v)) {
      setError(email, 'That email address looks incomplete.'); ok = false;
    }

    if (!pass.value) { setError(pass, 'Enter your password.'); ok = false; }
    else if (pass.value.length < 8) { setError(pass, 'Passwords are at least 8 characters.'); ok = false; }

    return ok;
  }

  form.addEventListener('submit', (e) => {
    e.preventDefault();
    if (submit.classList.contains('is-loading')) return;

    if (!validate()) {
      card.classList.remove('is-shaking');
      void card.offsetWidth;
      card.classList.add('is-shaking');
      note.textContent = 'Check the highlighted fields.';
      note.className = 'form__note is-error';
      const bad = $('.field.is-invalid input', form);
      if (bad) bad.focus();
      return;
    }

    submit.classList.add('is-loading');
    note.textContent = 'Verifying credentials…';
    note.className = 'form__note';

    setTimeout(() => {
      submit.classList.remove('is-loading');
      card.classList.add('is-done');
      card.querySelector('.card__head').innerHTML =
        `<h2>Welcome back</h2><p>Signed in as ${escapeHtml(email.value.trim())}</p>`;
      if (!card.querySelector('.card__done')) {
        const done = document.createElement('div');
        done.className = 'card__done';
        done.innerHTML = `
          <div class="ring">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 12.5l5 5L20 6.5"/></svg>
          </div>
          <h3>You're in.</h3>
          <p>Prototype only — nothing was sent anywhere.</p>
          <button type="button" id="resetBtn">Sign in as someone else</button>`;
        card.appendChild(done);
        $('#resetBtn').addEventListener('click', () => location.reload());
      }
      flash();
    }, 1500);
  });

  /* background pulse on success */
  function flash() {
    const f = document.createElement('div');
    Object.assign(f.style, {
      position: 'fixed', inset: '0', zIndex: '5', pointerEvents: 'none',
      background: 'radial-gradient(circle at 50% 55%, rgba(124,247,255,.35), transparent 60%)',
      opacity: '0', transition: 'opacity .5s ease'
    });
    document.body.appendChild(f);
    requestAnimationFrame(() => (f.style.opacity = '1'));
    setTimeout(() => (f.style.opacity = '0'), 420);
    setTimeout(() => f.remove(), 1100);
  }

  /* social buttons — demo only */
  $$('.social').forEach(btn => {
    btn.addEventListener('click', () => {
      toast(`Prototype: ${btn.dataset.provider} sign-in isn't wired up.`);
      btn.animate(
        [{ transform: 'translateY(-3px) scale(1.04)' }, { transform: 'none' }],
        { duration: 320, easing: 'cubic-bezier(.16,.84,.44,1)' }
      );
    });
  });

  /* ============================================================
     TOAST + CLOCK
     ============================================================ */
  const toastEl = $('#toast');
  let toastTimer;
  function toast(msg) {
    toastEl.textContent = msg;
    toastEl.classList.add('is-visible');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove('is-visible'), 2800);
  }

  const clock = $('#clock');
  function tickClock() {
    clock.textContent = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }
  tickClock();
  setInterval(tickClock, 20000);

  /* autofocus, but don't fight mobile keyboards */
  if (window.matchMedia('(min-width: 721px)').matches) email.focus({ preventScroll: true });

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, c => ({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;' }[c]));
  }
})();

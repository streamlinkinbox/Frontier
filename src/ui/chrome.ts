// ---------------------------------------------------------------------------
// Frontier UI / shared chrome helpers (toast). The viewport is intentionally
// bare: no toolbars, no rails — all document actions live in the editor pill.
// ---------------------------------------------------------------------------

let toastTimer: number | undefined;
export function toast(msg: string, warn = false, ms = 4200): void {
  let el = document.querySelector('#toast') as HTMLElement | null;
  if (!el) {
    el = document.createElement('div');
    el.id = 'toast';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.classList.toggle('warn', warn);
  el.style.display = 'block';
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => { el!.style.display = 'none'; }, ms);
}

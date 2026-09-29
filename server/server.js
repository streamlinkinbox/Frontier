// Frontier manual order server — no dependencies, Node 18+.
// Buyers place an order, pick a payment method, pay, and get a private order link.
// You confirm payments by hand in /admin, and the buyer's order page unlocks the download.
'use strict';
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const ROOT = __dirname;
const SITE_DIR = path.join(ROOT, '..', 'site');
const DATA_DIR = process.env.DATA_DIR || path.join(ROOT, 'data');
const FILES_DIR = path.join(ROOT, 'files');
const ORDERS_FILE = path.join(DATA_DIR, 'orders.json');
const PORT = Number(process.env.PORT || 3000);
const HOST = process.env.HOST || '0.0.0.0';
const ADMIN_PASSWORD = process.env.ADMIN_PASSWORD || '';
const DOWNLOAD_FILE = process.env.DOWNLOAD_FILE || ''; // file name inside server/files
const DOWNLOAD_URL = process.env.DOWNLOAD_URL || '';   // or an external link (Drive, GitHub Releases...)
const MAX_DOWNLOADS = Number(process.env.MAX_DOWNLOADS || 5);
const CONTACT_EMAIL = process.env.CONTACT_EMAIL || 'contact@YOURDOMAIN';

if (!ADMIN_PASSWORD) console.warn('WARNING: ADMIN_PASSWORD is not set — /admin is disabled.');

// ---------- storage (simple JSON file, atomic writes) ----------
fs.mkdirSync(DATA_DIR, { recursive: true });
let orders = [];
try { orders = JSON.parse(fs.readFileSync(ORDERS_FILE, 'utf8')); } catch { orders = []; }
function save() {
  const tmp = ORDERS_FILE + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(orders, null, 2));
  fs.renameSync(tmp, ORDERS_FILE);
}
function loadConfig() {
  return JSON.parse(fs.readFileSync(path.join(ROOT, 'payment-methods.json'), 'utf8'));
}

// ---------- helpers ----------
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const rand = (n) => crypto.randomBytes(n).toString('hex');
const safeEq = (a, b) => { const x = Buffer.from(String(a)), y = Buffer.from(String(b)); return x.length === y.length && crypto.timingSafeEqual(x, y); };
const fill = (t, o, p) => String(t || '').replaceAll('{ref}', o.ref).replaceAll('{amount}', p.price).replaceAll('{currency}', p.currency);
const newRef = () => 'FRN-' + crypto.randomBytes(3).toString('hex').toUpperCase();
const STATUS = {
  awaiting_payment: ['Waiting for your payment', '#d29922'],
  checking: ['Payment being checked', '#58a6ff'],
  paid: ['Paid — download ready', '#3fb950'],
  cancelled: ['Cancelled', '#f85149'],
};

function page(title, body) {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(title)}</title><style>
body{font-family:system-ui,sans-serif;max-width:780px;margin:32px auto;padding:0 18px;background:#0f1115;color:#e6e6e6;line-height:1.55}
a{color:#6cb6ff}.card{background:#181b22;border:1px solid #2a2f3a;border-radius:12px;padding:20px;margin:18px 0}
label{display:block;margin:12px 0 4px;font-weight:600}input,textarea,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #333a46;background:#0f1115;color:#e6e6e6;font:inherit}
.btn{display:inline-block;background:#2f81f7;color:#fff;border:0;padding:12px 22px;border-radius:8px;font-weight:600;cursor:pointer;text-decoration:none;font:inherit}
.btn.alt{background:#30363d}.method{display:flex;gap:12px;align-items:flex-start;border:1px solid #2a2f3a;border-radius:10px;padding:12px;margin:8px 0;cursor:pointer}
.method input{width:auto;margin-top:5px}.method small,small{color:#9aa4b2}.badge{display:inline-block;padding:3px 10px;border-radius:99px;color:#000;font-weight:700;font-size:.85em}
table{width:100%;border-collapse:collapse;font-size:.92em}td,th{border-bottom:1px solid #2a2f3a;padding:8px;text-align:left;vertical-align:top}
.hp{position:absolute;left:-9999px}code{background:#0b0d11;padding:2px 6px;border-radius:5px}
</style></head><body>${body}<p><small><a href="/">Home</a> · <a href="/refund.html">Refunds</a> · <a href="/privacy.html">Privacy</a></small></p></body></html>`;
}

function send(res, code, body, headers = {}) {
  res.writeHead(code, { 'Content-Type': 'text/html; charset=utf-8', 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer', ...headers });
  res.end(body);
}
function readBody(req, limit = 16 * 1024) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (c) => { data += c; if (data.length > limit) { reject(new Error('too large')); req.destroy(); } });
    req.on('end', () => resolve(Object.fromEntries(new URLSearchParams(data))));
    req.on('error', reject);
  });
}

// basic per-IP rate limit for creating orders
const hits = new Map();
function rateLimited(ip, max = 10, windowMs = 60 * 60 * 1000) {
  const now = Date.now();
  const arr = (hits.get(ip) || []).filter((t) => now - t < windowMs);
  arr.push(now); hits.set(ip, arr);
  return arr.length > max;
}
function isAdmin(req) {
  if (!ADMIN_PASSWORD) return false;
  const h = req.headers.authorization || '';
  if (!h.startsWith('Basic ')) return false;
  const [, pass] = Buffer.from(h.slice(6), 'base64').toString().split(/:(.*)/s);
  return safeEq(pass || '', ADMIN_PASSWORD);
}

// ---------- pages ----------
function orderForm(error = '', v = {}) {
  const cfg = loadConfig(); const p = cfg.product;
  const methods = cfg.methods.filter((m) => m.enabled).map((m, i) => `
    <label class="method"><input type="radio" name="method" value="${esc(m.id)}" ${v.method === m.id || (!v.method && i === 0) ? 'checked' : ''} required>
    <span><b>${esc(m.name)}</b><br><small>${esc(m.regions || '')}</small></span></label>`).join('');
  return page(`Order ${p.name}`, `
  <h1>Order ${esc(p.name)}</h1>
  <div class="card"><b style="font-size:1.6em">${esc(p.price)} ${esc(p.currency)}</b><br><small>Digital download. Orders are checked by hand, usually within 24 hours.</small></div>
  ${error ? `<div class="card" style="border-color:#f85149">${esc(error)}</div>` : ''}
  <form method="post" action="/order" class="card">
    <label>Your name</label><input name="name" maxlength="80" required value="${esc(v.name)}">
    <label>Email (where we send your download)</label><input name="email" type="email" maxlength="120" required value="${esc(v.email)}">
    <label>Country</label><input name="country" maxlength="60" required value="${esc(v.country)}">
    <label>How do you want to pay?</label>${methods}
    <input class="hp" name="website" tabindex="-1" autocomplete="off">
    <p><button class="btn">Place order</button></p>
    <small>You'll get a private order page. Bookmark it — your download appears there once your payment is confirmed.</small>
  </form>`);
}

function orderPage(o, notice = '') {
  const cfg = loadConfig(); const p = cfg.product;
  const m = cfg.methods.find((x) => x.id === o.method) || { name: o.method, instructions: '' };
  const [label, color] = STATUS[o.status] || [o.status, '#999'];
  const link = `/o/${o.id}?k=${o.key}`;
  let main = '';
  if (o.status === 'paid') {
    main = `<div class="card"><h2>🎉 Thanks! Your download is ready</h2>
      <p><a class="btn" href="/download/${o.downloadToken}">Download ${esc(p.name)}</a></p>
      <small>Downloads used: ${o.downloads || 0} of ${MAX_DOWNLOADS}.</small></div>`;
  } else if (o.status === 'cancelled') {
    main = `<div class="card">This order was cancelled. Questions? Email <a href="mailto:${esc(CONTACT_EMAIL)}">${esc(CONTACT_EMAIL)}</a> with reference <code>${esc(o.ref)}</code>.</div>`;
  } else {
    const payLink = m.link ? fill(m.link, o, p) : '';
    main = `<div class="card"><h2>1. Pay with ${esc(m.name)}</h2>
      <p>${esc(fill(m.instructions, o, p))}</p>
      ${payLink ? `<p><a class="btn" href="${esc(payLink)}" target="_blank" rel="noopener">Pay ${esc(p.price)} ${esc(p.currency)}</a></p>` : ''}
      <p>Always include your reference: <code>${esc(o.ref)}</code></p></div>
      <form method="post" action="${link.replace('?', '/paid?')}" class="card"><h2>2. Tell us you've paid</h2>
      <label>Transaction ID, sender name, or anything that helps us find your payment</label>
      <textarea name="proof" rows="3" maxlength="500" required>${esc(o.proof)}</textarea>
      <p><button class="btn">${o.status === 'checking' ? 'Update payment details' : "I've paid"}</button></p></form>`;
  }
  return page(`Order ${o.ref}`, `
    <h1>Order <code>${esc(o.ref)}</code></h1>
    ${notice ? `<div class="card" style="border-color:#3fb950">${esc(notice)}</div>` : ''}
    <p><span class="badge" style="background:${color}">${esc(label)}</span></p>
    <p><small>⭐ Bookmark this page — it's your private link. Anyone with it can see your order.</small></p>
    ${main}`);
}

function adminPage(filter) {
  const cfg = loadConfig();
  const list = orders.filter((o) => !filter || o.status === filter).slice().reverse();
  const counts = Object.keys(STATUS).map((s) => `<a href="/admin?status=${s}">${STATUS[s][0]} (${orders.filter((o) => o.status === s).length})</a>`).join(' · ');
  const rows = list.map((o) => {
    const m = cfg.methods.find((x) => x.id === o.method);
    const [label, color] = STATUS[o.status] || [o.status, '#999'];
    const btn = (s, t) => `<form method="post" action="/admin/orders/${o.id}" style="display:inline"><input type="hidden" name="status" value="${s}"><button class="btn ${s === 'paid' ? '' : 'alt'}" style="padding:6px 10px">${t}</button></form>`;
    return `<tr><td><code>${esc(o.ref)}</code><br><small>${esc(new Date(o.createdAt).toLocaleString('en-ZA'))}</small></td>
      <td>${esc(o.name)}<br><small>${esc(o.email)}<br>${esc(o.country)}</small></td>
      <td>${esc(m ? m.name : o.method)}<br><small>${esc(o.amount)} ${esc(o.currency)}</small></td>
      <td>${esc(o.proof || '—')}</td>
      <td><span class="badge" style="background:${color}">${esc(label)}</span><br>
      <a href="/o/${o.id}?k=${o.key}" target="_blank">buyer page</a></td>
      <td>${o.status !== 'paid' ? btn('paid', '✔ Mark paid') : ''} ${o.status !== 'cancelled' ? btn('cancelled', 'Cancel') : ''} ${o.status !== 'awaiting_payment' ? btn('awaiting_payment', 'Reset') : ''}</td></tr>`;
  }).join('');
  return page('Orders admin', `<h1>Orders</h1><p><a href="/admin">All (${orders.length})</a> · ${counts}</p>
    <div class="card" style="overflow-x:auto"><table><tr><th>Ref</th><th>Buyer</th><th>Method</th><th>Payment details</th><th>Status</th><th>Actions</th></tr>${rows || '<tr><td colspan=6>No orders yet.</td></tr>'}</table></div>
    <small>Only mark an order paid after you see the money in your own PayPal / Payoneer / bank app — never trust screenshots. Then email the buyer their order page link.</small>`);
}

// ---------- static files ----------
const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.txt': 'text/plain' };
function serveStatic(req, res, pathname) {
  const rel = pathname === '/' ? 'index.html' : decodeURIComponent(pathname).replace(/^\/+/, '');
  const file = path.resolve(SITE_DIR, rel);
  if (!file.startsWith(SITE_DIR + path.sep)) return send(res, 403, 'Forbidden');
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) return send(res, 404, page('Not found', '<h1>Not found</h1>'));
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
}

// ---------- router ----------
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  const p = url.pathname;
  const ip = (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '').split(',')[0].trim();
  try {
    if (p === '/order' && req.method === 'GET') return send(res, 200, orderForm());

    if (p === '/order' && req.method === 'POST') {
      const b = await readBody(req);
      if (b.website) return send(res, 200, orderForm()); // bot honeypot
      const cfg = loadConfig();
      const method = cfg.methods.find((m) => m.enabled && m.id === b.method);
      if (!b.name || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(b.email || '') || !b.country || !method)
        return send(res, 400, orderForm('Please fill in every field and choose a payment method.', b));
      if (rateLimited(ip)) return send(res, 429, orderForm('Too many orders from your connection. Please try again later.', b));
      const o = {
        id: rand(8), key: rand(16), ref: newRef(), status: 'awaiting_payment',
        name: b.name.slice(0, 80), email: b.email.slice(0, 120), country: b.country.slice(0, 60),
        method: method.id, amount: cfg.product.price, currency: cfg.product.currency,
        proof: '', createdAt: new Date().toISOString(), downloads: 0,
      };
      orders.push(o); save();
      console.log(`New order ${o.ref} via ${o.method} from ${o.country}`);
      res.writeHead(303, { Location: `/o/${o.id}?k=${o.key}&new=1` }); return res.end();
    }

    let m;
    if ((m = p.match(/^\/o\/([a-f0-9]{16})(\/paid)?$/))) {
      const o = orders.find((x) => x.id === m[1]);
      if (!o || !safeEq(url.searchParams.get('k') || '', o.key)) return send(res, 404, page('Not found', '<h1>Order not found</h1>'));
      if (m[2] && req.method === 'POST') {
        const b = await readBody(req);
        if (o.status === 'awaiting_payment' || o.status === 'checking') {
          o.proof = String(b.proof || '').slice(0, 500); o.status = 'checking'; o.paidClaimAt = new Date().toISOString(); save();
          console.log(`Order ${o.ref} marked as paid by buyer — check it in /admin`);
        }
        return send(res, 200, orderPage(o, "Thanks! We'll check your payment and unlock your download here, usually within 24 hours."));
      }
      return send(res, 200, orderPage(o, url.searchParams.get('new') ? `Order placed! Your reference is ${o.ref}.` : ''));
    }

    if ((m = p.match(/^\/download\/([a-f0-9]{32})$/))) {
      const o = orders.find((x) => x.status === 'paid' && x.downloadToken && safeEq(x.downloadToken, m[1]));
      if (!o) return send(res, 404, page('Not found', '<h1>Download link not valid</h1>'));
      if ((o.downloads || 0) >= MAX_DOWNLOADS) return send(res, 403, page('Limit reached', `<h1>Download limit reached</h1><p>Email <a href="mailto:${esc(CONTACT_EMAIL)}">${esc(CONTACT_EMAIL)}</a> with reference <code>${esc(o.ref)}</code> for a reset.</p>`));
      o.downloads = (o.downloads || 0) + 1; save();
      if (DOWNLOAD_FILE) {
        const file = path.resolve(FILES_DIR, DOWNLOAD_FILE);
        if (!file.startsWith(FILES_DIR + path.sep) || !fs.existsSync(file)) return send(res, 500, page('Error', '<h1>File missing — please contact us.</h1>'));
        res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Disposition': `attachment; filename="${path.basename(file)}"`, 'Content-Length': fs.statSync(file).size });
        return fs.createReadStream(file).pipe(res);
      }
      if (DOWNLOAD_URL) { res.writeHead(302, { Location: DOWNLOAD_URL }); return res.end(); }
      return send(res, 500, page('Error', '<h1>Download not configured yet — please contact us.</h1>'));
    }

    if (p === '/admin' || p.startsWith('/admin/')) {
      if (!isAdmin(req)) return send(res, 401, page('Login', '<h1>Login required</h1>'), { 'WWW-Authenticate': 'Basic realm="orders"' });
      if ((m = p.match(/^\/admin\/orders\/([a-f0-9]{16})$/)) && req.method === 'POST') {
        const o = orders.find((x) => x.id === m[1]);
        const b = await readBody(req);
        if (o && STATUS[b.status]) {
          o.status = b.status;
          if (b.status === 'paid') { o.downloadToken ||= rand(16); o.paidAt = new Date().toISOString(); }
          save(); console.log(`Admin set ${o.ref} -> ${o.status}`);
        }
        res.writeHead(303, { Location: '/admin' }); return res.end();
      }
      return send(res, 200, adminPage(url.searchParams.get('status')));
    }

    if (req.method === 'GET') return serveStatic(req, res, p);
    send(res, 405, 'Method not allowed');
  } catch (e) {
    console.error(e);
    send(res, 500, page('Error', '<h1>Something went wrong</h1>'));
  }
});

server.listen(PORT, HOST, () => console.log(`Frontier shop running on http://${HOST}:${PORT}  (admin: /admin)`));

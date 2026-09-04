/* ============ FRONTIER HMI — cockpit 3D + HMI wiring ============ */
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { CSS3DRenderer, CSS3DObject } from "three/addons/renderers/CSS3DRenderer.js";
import { MockEOS, VEHICLES } from "./mock-eos.js";

const $ = (s) => document.querySelector(s);
const eos = new MockEOS();

/* ================= HMI STATE / RENDER ================= */
let lbScope = "season";
let lbQuery = "";

eos.onLog = (html) => {
  const feed = $("#lobbyLog");
  const t = new Date().toLocaleTimeString([], { hour12: false });
  feed.innerHTML = `<b>[${t}]</b> ${html} &nbsp;///&nbsp; ` + feed.innerHTML;
};

function logSys(text) {
  const li = document.createElement("li");
  li.className = "sys";
  li.textContent = text;
  const chat = $("#chatLog");
  chat.appendChild(li);
  chat.scrollTop = chat.scrollHeight;
}
function logChat(name, text) {
  const li = document.createElement("li");
  const b = document.createElement("b");
  b.textContent = name + " ";
  li.appendChild(b);
  li.appendChild(document.createTextNode(text));
  const chat = $("#chatLog");
  chat.appendChild(li);
  chat.scrollTop = chat.scrollHeight;
}

function renderRoster() {
  const ul = $("#rosterList");
  ul.innerHTML = "";
  eos.players.forEach((p) => {
    const li = document.createElement("li");
    if (p.me) li.className = "me";
    const hostCrown = eos.isHost && p.me ? ' <span class="crown">♛</span>' : "";
    li.innerHTML =
      `<span class="pav" style="background:${p.color}">${p.name.slice(0, 2).toUpperCase()}</span>` +
      `<span class="pinfo"><b>${p.name}${hostCrown}</b><i>${p.platform} • ${p.vehicle}</i></span>` +
      `<span class="ping ${p.ping > 60 ? "lag" : ""}">${p.ping}ms</span>` +
      `<span class="rdy ${p.ready ? "yes" : "no"}">${p.ready ? "READY" : "WAIT"}</span>`;
    ul.appendChild(li);
  });
  for (let i = eos.players.length; i < eos.lobby.maxSlots; i++) {
    const li = document.createElement("li");
    li.className = "open";
    li.textContent = "+ OPEN SLOT — INVITE";
    ul.appendChild(li);
  }
  $("#slotCount").textContent = `${eos.players.length}/${eos.lobby.maxSlots}`;
  $("#readyCount").textContent = `${eos.readyCount}/${eos.players.length} ready`;
  const btn = $("#startBtn");
  const canStart = eos.isHost && eos.me.ready && eos.players.length >= 2;
  btn.disabled = !canStart;
  const note = $("#hostNote");
  if (!eos.isHost) { note.textContent = "Only the host can start. (Use SIM ⇄ to take host)"; note.className = "host-note warn"; }
  else if (!eos.me.ready) { note.textContent = "Ready up to enable launch."; note.className = "host-note warn"; }
  else { note.textContent = "All systems go. Awaiting launch command."; note.className = "host-note"; }
}

function renderVehicles() {
  const wrap = $("#vehicleCards");
  wrap.innerHTML = "";
  VEHICLES.forEach((v) => {
    const d = document.createElement("div");
    d.className = "veh" + (eos.myVehicle.id === v.id ? " sel" : "");
    d.innerHTML =
      `<b>${v.name}</b><span class="vtag">${v.tag}</span><small>${v.desc}</small>` +
      `<div class="stat">SPD<span class="bar"><i style="width:${v.spd}%"></i></span></div>` +
      `<div class="stat">ACC<span class="bar"><i style="width:${v.acc}%"></i></span></div>` +
      `<div class="stat">GRP<span class="bar"><i style="width:${v.grp}%"></i></span></div>`;
    d.onclick = () => { eos.setVehicle(v); renderVehicles(); renderRoster(); };
    wrap.appendChild(d);
  });
}

function renderBoard(changed = new Set()) {
  const rows = eos.boardView(lbScope, lbQuery);
  const medals = ["🥇", "🥈", "🥉"];
  $("#podium").innerHTML = rows.slice(0, 3).map((e, i) =>
    `<div class="pod p${i + 1}"><span class="medal">${medals[i]}</span>` +
    `<div><b>${e.name}${e.me ? " (YOU)" : ""}</b><i>${e.vehicle} • ${e.wins}W</i></div>` +
    `<span class="pr">${e.rating}</span></div>`).join("");
  const tb = $("#lbBody");
  tb.innerHTML = "";
  rows.forEach((e, i) => {
    const tr = document.createElement("tr");
    if (e.me) tr.className = "me";
    if (changed.has(e.name)) tr.classList.add("bump");
    const t = e.trend > 0 ? ["▲", "up"] : e.trend < 0 ? ["▼", "dn"] : ["–", "eq"];
    tr.innerHTML = `<td>${String(i + 1).padStart(2, "0")}</td><td>${e.name}${e.me ? " ★" : ""}</td>` +
      `<td>${e.vehicle}</td><td>${e.wins}</td><td>${e.podiums}</td><td><b>${e.rating}</b></td>` +
      `<td class="trend ${t[1]}">${t[0]}</td>`;
    tb.appendChild(tr);
  });
}

/* ---- HMI events ---- */
document.querySelectorAll(".tabs button").forEach((b) => {
  b.onclick = () => {
    document.querySelectorAll(".tabs button").forEach((x) => x.classList.remove("active"));
    b.classList.add("active");
    $("#tab-lobby").hidden = b.dataset.tab !== "lobby";
    $("#tab-leaderboard").hidden = b.dataset.tab !== "leaderboard";
    if (b.dataset.tab === "leaderboard") renderBoard();
  };
});
document.querySelectorAll("[data-lb]").forEach((b) => {
  b.onclick = () => {
    document.querySelectorAll("[data-lb]").forEach((x) => x.classList.remove("active"));
    b.classList.add("active");
    lbScope = b.dataset.lb;
    eos.onLog(`<b>EOS_Leaderboards_Query</b> scope=${lbScope} → ${eos.boardView(lbScope, lbQuery).length} rows`);
    renderBoard();
  };
});
$("#lbSearch").oninput = (e) => { lbQuery = e.target.value; renderBoard(); };
$("#lbRefresh").onclick = () => {
  const c = eos.tickBoard(lbScope);
  eos.onLog(`<b>EOS_Leaderboards_Sync</b> scope=${lbScope} OK (${c.size} deltas)`);
  renderBoard(c);
};
$("#lbMe").onclick = () => {
  lbScope = "season"; lbQuery = "";
  $("#lbSearch").value = "";
  document.querySelectorAll("[data-lb]").forEach((x) => x.classList.toggle("active", x.dataset.lb === "season"));
  renderBoard();
  const me = $("#lbBody tr.me");
  if (me) me.scrollIntoView({ block: "center", behavior: "smooth" });
};

$("#readyBtn").onclick = () => {
  const r = eos.toggleReady();
  $("#readyBtn").textContent = r ? "✓ READY — TAP TO UNREADY" : "✓ READY UP";
  $("#readyBtn").classList.toggle("on", r);
  renderRoster();
};
$("#startBtn").onclick = () => {
  const cd = $("#countdown"), num = $("#cdNum");
  cd.hidden = false;
  eos.onLog(`<b>EOS_Session_Start</b> map=${eos.lobby.map} players=${eos.players.length} — launching…`);
  const seq = ["3", "2", "1", "GO"];
  let i = 0;
  const step = () => {
    num.textContent = seq[i];
    num.style.animation = "none"; void num.offsetWidth; num.style.animation = "";
    num.style.color = seq[i] === "GO" ? "#34d399" : "";
    i++;
    if (i < seq.length) setTimeout(step, 900);
    else setTimeout(() => {
      cd.hidden = true;
      eos.onLog(`<b>EOS_Session_Lock</b> race started on ${eos.lobby.map} (mock — staying in lobby)`);
      logSys(`🏁 Race launched on ${eos.lobby.map}! (mock — lobby persists)`);
    }, 1000);
  };
  step();
};
$("#copyBtn").onclick = () => {
  try { navigator.clipboard.writeText($("#roomCode").textContent); } catch (e) { /* sandbox */ }
  eos.onLog(`<b>EOS_Lobby_CopyCode</b> ${$("#roomCode").textContent} → clipboard`);
};
$("#mapSelect").onchange = (e) => {
  eos.lobby.map = e.target.value;
  eos.onLog(`<b>EOS_Lobby_Update</b> map → ${e.target.value}`);
};
$("#regionSelect").onchange = (e) => {
  eos.lobby.region = e.target.value;
  $("#regionLabel").textContent = e.target.value;
  eos.onLog(`<b>EOS_Lobby_Migrate</b> region → ${e.target.value} (re-ping)`);
};
$("#simJoin").onclick = () => { const p = eos.addBot(); if (p) { renderRoster(); logSys(`📥 ${p.name} joined the lobby`); } };
$("#simLeave").onclick = () => { const p = eos.removeRandom(); if (p) { renderRoster(); logSys(`📤 ${p.name} left the lobby`); } };
$("#simHost").onclick = () => { const h = eos.toggleHost(); renderRoster(); logSys(h ? "♛ You are now host" : "♛ Host migrated to Nova_K"); };

function sendChat() {
  const inp = $("#chatInput");
  const text = inp.value.trim();
  if (!text) return;
  inp.value = "";
  logChat("YOU_77", text);
  eos.onLog(`<b>EOS_Lobby_SendMsg</b> YOU_77: "${text.slice(0, 32)}"`);
  setTimeout(() => {
    const others = eos.players.filter((p) => !p.me);
    if (others.length) logChat(others[Math.floor(Math.random() * others.length)].name, eos.botReply());
  }, 900 + Math.random() * 1200);
}
$("#chatSend").onclick = sendChat;
$("#chatInput").onkeydown = (e) => { if (e.key === "Enter") sendChat(); e.stopPropagation(); };

setInterval(() => {
  $("#clock").textContent = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: false });
}, 1000);

/* ---- boot sequence (mock EOS connect) ---- */
renderRoster(); renderVehicles(); renderBoard();
logChat("Nova_K", "yo who put harbor loop in the vote 💀");
logChat("Kaito", "neon dunes or we riot");
logSys("Connected to mock EOS backend (no network)");
setTimeout(() => { const p = $("#eosPill"); p.textContent = "● LOBBY SYNCED • MOCK"; p.classList.add("sync"); }, 600);
eos.onLog(`<b>EOS_Auth_Login</b> account=YOU_77 grant=client_credentials OK`);
eos.onLog(`<b>EOS_Lobby_Create</b> id=${eos.lobby.code} map=${eos.lobby.map} max=${eos.lobby.maxSlots}`);
eos.onLog(`<b>EOS_Presence_Set</b> YOU_77 status=IN_LOBBY crossplay=ON`);

setInterval(() => { eos.tickPings(); renderRoster(); }, 3000);
setInterval(() => { if (!$("#tab-leaderboard").hidden) renderBoard(eos.tickBoard(lbScope)); }, 5000);
setInterval(() => {
  if (Math.random() > 0.45) return;
  if (eos.players.length < eos.lobby.maxSlots && Math.random() > 0.5) {
    const p = eos.addBot(); if (p) { renderRoster(); logSys(`📥 ${p.name} joined the lobby`); }
  } else {
    const p = eos.ambientEvent(); if (p) renderRoster();
  }
}, 14000);

/* ================= 3D COCKPIT ================= */
const CAMS = {
  cockpit: { p: [0.05, 1.34, 1.62], t: [0.05, 0.98, -0.9] },
  tablet:  { p: [0.35, 1.14, 0.42], t: [0.35, 1.0, -0.62] },
  driver:  { p: [-0.62, 1.28, 0.62], t: [-0.2, 0.92, -0.95] },
  road:    { p: [0.1, 1.32, 0.95],  t: [0.1, 0.95, -8.0] },
};
let goPreset = () => {};

try {
  const stage = $("#stage");
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setSize(innerWidth, innerHeight);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.1;
  stage.appendChild(renderer.domElement);

  const cssRenderer = new CSS3DRenderer();
  cssRenderer.setSize(innerWidth, innerHeight);
  Object.assign(cssRenderer.domElement.style, { position: "absolute", top: "0", left: "0", pointerEvents: "none" });
  stage.appendChild(cssRenderer.domElement);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x02040a);
  scene.fog = new THREE.Fog(0x02040a, 12, 55);

  const camera = new THREE.PerspectiveCamera(58, innerWidth / innerHeight, 0.05, 200);
  camera.position.set(...CAMS.cockpit.p);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(...CAMS.cockpit.t);
  controls.enableDamping = true;
  controls.dampingFactor = 0.06;
  controls.enablePan = false;
  controls.minDistance = 0.35;
  controls.maxDistance = 4.2;
  controls.maxPolarAngle = 1.72;

  /* ----- camera tween ----- */
  let tween = null;
  goPreset = (name) => {
    const c = CAMS[name];
    tween = { t: 0, dur: 1.3,
      p0: camera.position.clone(), p1: new THREE.Vector3(...c.p),
      t0: controls.target.clone(), t1: new THREE.Vector3(...c.t) };
    document.querySelectorAll(".hud-cams button").forEach((x) =>
      x.classList.toggle("active", x.dataset.cam === name));
  };
  document.querySelectorAll(".hud-cams button").forEach((b) => (b.onclick = () => goPreset(b.dataset.cam)));
  renderer.domElement.addEventListener("pointerdown", () => (tween = null));
  const ease = (x) => x * x * (3 - 2 * x);

  /* ----- materials ----- */
  const M = {
    dark: new THREE.MeshStandardMaterial({ color: 0x0b0f18, roughness: 0.85 }),
    trim: new THREE.MeshStandardMaterial({ color: 0x141b2c, roughness: 0.5, metalness: 0.4 }),
    seat: new THREE.MeshStandardMaterial({ color: 0x11182b, roughness: 0.95 }),
    cyan: new THREE.MeshStandardMaterial({ color: 0x062026, emissive: 0x22d3ee, emissiveIntensity: 2.2 }),
    mag: new THREE.MeshStandardMaterial({ color: 0x260626, emissive: 0xff00ff, emissiveIntensity: 1.6 }),
    warm: new THREE.MeshStandardMaterial({ color: 0x2a1e08, emissive: 0xffb35c, emissiveIntensity: 1.2 }),
    glass: new THREE.MeshStandardMaterial({ color: 0x0a1626, roughness: 0.1, metalness: 0.9 }),
  };
  const box = (w, h, d, mat, x, y, z, rx = 0, ry = 0, rz = 0) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
    m.position.set(x, y, z); m.rotation.set(rx, ry, rz);
    scene.add(m); return m;
  };

  /* ----- cabin shell ----- */
  box(3.4, 0.12, 4.2, M.dark, 0, -0.1, -0.6);          // floor
  box(3.4, 1.4, 0.12, M.dark, 0, 0.7, 1.45);          // rear wall
  box(0.12, 1.5, 4.2, M.dark, -1.7, 0.7, -0.6);       // sides
  box(0.12, 1.5, 4.2, M.dark, 1.7, 0.7, -0.6);
  box(3.4, 0.1, 4.2, M.dark, 0, 1.78, -0.6);          // roof
  // A-pillars + mirror
  box(0.09, 1.1, 0.09, M.trim, -1.28, 1.25, -1.5, 0, 0, 0.28);
  box(0.09, 1.1, 0.09, M.trim, 1.28, 1.25, -1.5, 0, 0, -0.28);
  box(0.5, 0.14, 0.04, M.dark, 0, 1.62, -1.35);

  /* ----- dashboard ----- */
  box(3.0, 0.32, 0.7, M.trim, 0, 0.98, -0.95);        // dash body
  box(3.0, 0.1, 0.78, M.dark, 0, 1.16, -0.95);        // dash top pad
  box(2.9, 0.035, 0.035, M.cyan, 0, 1.0, -0.585);     // LED strip
  // driver cluster glow
  const cluster = new THREE.Mesh(new THREE.PlaneGeometry(0.5, 0.16),
    new THREE.MeshBasicMaterial({ color: 0x0e7490 }));
  cluster.position.set(-0.55, 1.06, -0.592); scene.add(cluster);

  /* ----- steering wheel ----- */
  const wheel = new THREE.Group();
  const rim = new THREE.Mesh(new THREE.TorusGeometry(0.19, 0.024, 12, 40), M.dark);
  wheel.add(rim);
  const spokeG = new THREE.BoxGeometry(0.34, 0.035, 0.02);
  const sp1 = new THREE.Mesh(spokeG, M.trim); wheel.add(sp1);
  const sp2 = new THREE.Mesh(new THREE.BoxGeometry(0.035, 0.32, 0.02), M.trim); wheel.add(sp2);
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.05, 20), M.trim);
  hub.rotation.x = Math.PI / 2; wheel.add(hub);
  const hubGlow = new THREE.Mesh(new THREE.CircleGeometry(0.028, 20),
    new THREE.MeshBasicMaterial({ color: 0x22d3ee }));
  hubGlow.position.z = 0.028; wheel.add(hubGlow);
  wheel.position.set(-0.55, 0.92, -0.52);
  wheel.rotation.x = -0.35;
  scene.add(wheel);
  box(0.1, 0.3, 0.1, M.dark, -0.55, 0.68, -0.6, -0.4); // column

  /* ----- seats ----- */
  const seatAt = (x) => {
    box(0.62, 0.16, 0.6, M.seat, x, 0.28, 0.4);
    box(0.62, 0.75, 0.16, M.seat, x, 0.68, 0.68, 0.12);
    box(0.5, 0.14, 0.12, M.seat, x, 1.12, 0.72, 0.12);
  };
  seatAt(-0.55); seatAt(0.6);

  /* ----- center console + tablet ----- */
  box(0.5, 0.5, 1.3, M.trim, 0.35, 0.28, -0.25);
  box(0.46, 0.04, 1.1, M.dark, 0.35, 0.55, -0.25);
  box(0.44, 0.02, 0.02, M.mag, 0.35, 0.53, -0.7);     // console glow

  const tablet = new THREE.Group();
  tablet.position.set(0.35, 1.02, -0.62);
  tablet.rotation.x = -0.24;
  scene.add(tablet);
  const bezel = new THREE.Mesh(new THREE.BoxGeometry(2.06, 1.32, 0.07), M.trim);
  tablet.add(bezel);
  const backing = new THREE.Mesh(new THREE.PlaneGeometry(1.94, 1.22),
    new THREE.MeshBasicMaterial({ color: 0x000000 }));
  backing.position.z = 0.037;
  tablet.add(backing);

  const screenEl = $("#tablet-screen");
  const screen = new CSS3DObject(screenEl);
  screen.scale.set(0.0016, 0.0016, 0.0016);
  screen.position.z = 0.039;
  tablet.add(screen);

  const glow = new THREE.PointLight(0x66d9ff, 2.2, 3.2);
  glow.position.set(0.35, 1.15, 0.1);
  scene.add(glow);

  /* ----- lights ----- */
  scene.add(new THREE.AmbientLight(0x334466, 0.9));
  const dashLight = new THREE.PointLight(0x22d3ee, 3.5, 4);
  dashLight.position.set(0, 1.2, -0.2); scene.add(dashLight);
  const footLight = new THREE.PointLight(0xff00ff, 2.0, 3);
  footLight.position.set(0, 0.15, 0.2); scene.add(footLight);
  const dome = new THREE.PointLight(0xffc98a, 1.2, 4);
  dome.position.set(0, 1.7, 0.4); scene.add(dome);
  const head = new THREE.SpotLight(0xbfe3ff, 60, 60, 0.5, 0.5);
  head.position.set(0, 1.0, -1.0);
  head.target.position.set(0, -0.4, -20);
  scene.add(head, head.target);

  /* ----- outside world: road, lamps, city, stars ----- */
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(120, 120),
    new THREE.MeshStandardMaterial({ color: 0x05070c, roughness: 1 }));
  ground.rotation.x = -Math.PI / 2; ground.position.y = -0.42; scene.add(ground);

  const rc = document.createElement("canvas"); rc.width = 128; rc.height = 256;
  const g = rc.getContext("2d");
  g.fillStyle = "#0a0d14"; g.fillRect(0, 0, 128, 256);
  g.fillStyle = "#22d3ee"; g.fillRect(60, 20, 8, 90);
  g.fillStyle = "rgba(255,255,255,.75)"; g.fillRect(4, 0, 4, 256); g.fillRect(120, 0, 4, 256);
  const roadTex = new THREE.CanvasTexture(rc);
  roadTex.wrapS = roadTex.wrapT = THREE.RepeatWrapping;
  roadTex.repeat.set(1, 10);
  const road = new THREE.Mesh(new THREE.PlaneGeometry(3.4, 70),
    new THREE.MeshStandardMaterial({ map: roadTex, roughness: 0.9 }));
  road.rotation.x = -Math.PI / 2; road.position.set(0, -0.4, -32); scene.add(road);

  for (let i = 0; i < 12; i++) {
    const z = -4 - i * 5;
    const mat = i % 2 ? M.cyan : M.mag;
    box(0.1, 3.2, 0.1, M.dark, -3.4, 1.1, z);
    box(0.1, 3.2, 0.1, M.dark, 3.4, 1.1, z);
    const l1 = new THREE.Mesh(new THREE.SphereGeometry(0.09, 10, 10), mat);
    l1.position.set(-3.4, 2.7, z); scene.add(l1);
    const l2 = l1.clone(); l2.position.x = 3.4; scene.add(l2);
  }
  // city silhouette
  const cc = document.createElement("canvas"); cc.width = 256; cc.height = 128;
  const cg = cc.getContext("2d");
  cg.fillStyle = "#04060c"; cg.fillRect(0, 0, 256, 128);
  for (let i = 0; i < 90; i++) {
    cg.fillStyle = Math.random() > 0.5 ? "rgba(34,211,238,.9)" : "rgba(255,0,255,.8)";
    cg.fillRect(Math.random() * 256, Math.random() * 128, 2, 3);
  }
  const cityTex = new THREE.CanvasTexture(cc);
  for (let i = 0; i < 14; i++) {
    const h = 3 + Math.random() * 6;
    const b = new THREE.Mesh(new THREE.BoxGeometry(2 + Math.random() * 3, h, 2),
      new THREE.MeshStandardMaterial({ color: 0x070b14, emissive: 0xffffff, emissiveMap: cityTex, emissiveIntensity: 0.9 }));
    b.position.set(-22 + Math.random() * 44, h / 2 - 0.4, -48 - Math.random() * 8);
    scene.add(b);
  }
  // stars
  const starGeo = new THREE.BufferGeometry();
  const sp = [];
  for (let i = 0; i < 400; i++) sp.push((Math.random() - 0.5) * 120, 4 + Math.random() * 30, -30 - Math.random() * 60);
  starGeo.setAttribute("position", new THREE.Float32BufferAttribute(sp, 3));
  scene.add(new THREE.Points(starGeo, new THREE.PointsMaterial({ color: 0x9fd8ff, size: 0.08 })));
  // speed particles
  const pGeo = new THREE.BufferGeometry();
  const N = 220, pp = new Float32Array(N * 3);
  for (let i = 0; i < N; i++) { pp[i * 3] = (Math.random() - 0.5) * 8; pp[i * 3 + 1] = Math.random() * 3; pp[i * 3 + 2] = -Math.random() * 40; }
  pGeo.setAttribute("position", new THREE.BufferAttribute(pp, 3));
  const streaks = new THREE.Points(pGeo, new THREE.PointsMaterial({ color: 0x67e8f9, size: 0.05, transparent: true, opacity: 0.8 }));
  scene.add(streaks);

  /* ----- click tablet to focus ----- */
  const ray = new THREE.Raycaster(), ptr = new THREE.Vector2();
  let downAt = 0;
  renderer.domElement.addEventListener("pointerdown", (e) => (downAt = e.timeStamp));
  renderer.domElement.addEventListener("pointerup", (e) => {
    if (e.timeStamp - downAt > 250) return;
    ptr.set((e.clientX / innerWidth) * 2 - 1, -(e.clientY / innerHeight) * 2 + 1);
    ray.setFromCamera(ptr, camera);
    if (ray.intersectObjects([bezel, backing]).length) goPreset("tablet");
  });
  addEventListener("keydown", (e) => { if (e.key === "Escape") goPreset("cockpit"); });

  addEventListener("resize", () => {
    camera.aspect = innerWidth / innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(innerWidth, innerHeight);
    cssRenderer.setSize(innerWidth, innerHeight);
  });

  /* ----- loop ----- */
  const clockT = new THREE.Clock();
  renderer.setAnimationLoop(() => {
    const dt = Math.min(clockT.getDelta(), 0.05);
    const t = clockT.elapsedTime;
    roadTex.offset.y += dt * 2.6;                       // driving motion
    const arr = pGeo.attributes.position.array;
    for (let i = 0; i < N; i++) {
      arr[i * 3 + 2] += dt * 14;
      if (arr[i * 3 + 2] > 2) arr[i * 3 + 2] = -40;
    }
    pGeo.attributes.position.needsUpdate = true;
    wheel.rotation.z = Math.sin(t * 0.7) * 0.05;        // idle sway
    glow.intensity = 2.1 + Math.sin(t * 7.3) * 0.15;    // screen flicker
    dashLight.intensity = 3.4 + Math.sin(t * 2.1) * 0.25;
    if (tween) {
      tween.t += dt / tween.dur;
      const k = ease(Math.min(tween.t, 1));
      camera.position.lerpVectors(tween.p0, tween.p1, k);
      controls.target.lerpVectors(tween.t0, tween.t1, k);
      if (tween.t >= 1) tween = null;
    }
    controls.update();
    renderer.render(scene, camera);
    cssRenderer.render(scene, camera);
  });

  setTimeout(() => $("#boot").classList.add("gone"), 900);
} catch (err) {
  console.error("3D init failed, falling back to 2D:", err);
  document.body.classList.add("no3d");
  $("#boot").classList.add("gone");
}

/* ============ FRONTIER — Mock EOS + game data ============
   Simulates Epic Online Services lobby / presence / leaderboard
   flows entirely client-side so the HMI can be previewed with
   zero backend. Swap these calls for the real EOS SDK later. */

export const VEHICLES = [
  { id: "vanta",  name: "VANTA GT",     desc: "Balanced interceptor",  spd: 82, acc: 78, grp: 85, tag: "BALANCED" },
  { id: "crawler", name: "DUNE CRAWLER", desc: "Off-road convoy rig",  spd: 74, acc: 88, grp: 92, tag: "GRIP" },
  { id: "volt",   name: "VOLT STRIKER", desc: "Top-speed sprinter",    spd: 95, acc: 81, grp: 70, tag: "SPEED" },
];

export const PAINTS = [
  { name: "NEON CYAN",     c: "#22d3ee" },
  { name: "MAGMA MAGENTA", c: "#ff2fd6" },
  { name: "VOLT LIME",     c: "#a3e635" },
  { name: "SOLAR AMBER",   c: "#fbbf24" },
  { name: "VOID VIOLET",   c: "#818cf8" },
];

const AVATAR_COLORS = ["#22d3ee", "#f0abfc", "#fbbf24", "#34d399", "#fb7185", "#818cf8", "#f97316", "#a3e635"];

const BOT_POOL = [
  { name: "Nova_K",     platform: "PS", vehicle: "VOLT STRIKER" },
  { name: "DriftLord",  platform: "XB", vehicle: "VANTA GT" },
  { name: "Kaito",      platform: "PC", vehicle: "DUNE CRAWLER" },
  { name: "Mira",       platform: "PS", vehicle: "VANTA GT" },
  { name: "GhostPeppa", platform: "XB", vehicle: "VOLT STRIKER" },
  { name: "TurboTess",  platform: "PC", vehicle: "DUNE CRAWLER" },
  { name: "Overdrive",  platform: "PS", vehicle: "VANTA GT" },
  { name: "SlipStream", platform: "PC", vehicle: "VOLT STRIKER" },
];

const CHAT_REPLIES = [
  "lets run it 🏁", "ready when u are", "neon dunes my map fr",
  "whos host? start it up", "just tuned my striker, watch out",
  "gg last race btw", "convoy formation on green", "brb grabbing fuel lol",
];

const BOARD_SEED = [
  { name: "Nova_K",     vehicle: "VOLT STRIKER", wins: 214, podiums: 388, rating: 2841, friend: true },
  { name: "DriftLord",  vehicle: "VANTA GT",     wins: 201, podiums: 350, rating: 2798, friend: true },
  { name: "Kaito",      vehicle: "DUNE CRAWLER", wins: 196, podiums: 341, rating: 2744, friend: false },
  { name: "YOU_77",     vehicle: "VANTA GT",     wins: 187, podiums: 322, rating: 2690, friend: true, me: true },
  { name: "Mira",       vehicle: "VANTA GT",     wins: 181, podiums: 310, rating: 2655, friend: true },
  { name: "GhostPeppa", vehicle: "VOLT STRIKER", wins: 170, podiums: 290, rating: 2588, friend: false },
  { name: "TurboTess",  vehicle: "DUNE CRAWLER", wins: 164, podiums: 281, rating: 2541, friend: true },
  { name: "Overdrive",  vehicle: "VANTA GT",     wins: 150, podiums: 260, rating: 2477, friend: false },
  { name: "SlipStream", vehicle: "VOLT STRIKER", wins: 141, podiums: 244, rating: 2410, friend: false },
  { name: "NightOwl",   vehicle: "DUNE CRAWLER", wins: 130, podiums: 220, rating: 2352, friend: false },
  { name: "PixelPete",  vehicle: "VANTA GT",     wins: 118, podiums: 199, rating: 2288, friend: false },
  { name: "ApexAna",    vehicle: "VOLT STRIKER", wins: 104, podiums: 180, rating: 2215, friend: true },
];

const rnd = (a, b) => Math.floor(Math.random() * (b - a + 1)) + a;
const pick = (arr) => arr[Math.floor(Math.random() * arr.length)];

export class MockEOS {
  constructor() {
    this.onLog = () => {};
    this.lobby = { name: "NIGHT CONVOY", code: "FR-7X2Q", map: "Neon Dunes", region: "EU-WEST", maxSlots: 8 };
    this.isHost = true;
    this.myVehicle = VEHICLES[0];
    this.paint = PAINTS[0].c;
    this.maps = ["Neon Dunes", "Harbor Loop", "Solar Flats"];
    this.mapVotes = { "Neon Dunes": 2, "Harbor Loop": 2, "Solar Flats": 1 };
    this.myVote = "Neon Dunes";
    this._botIdx = 0;
    this.players = [
      this._mk("YOU_77", "PC", "VANTA GT", true, true),
      this._mk("Nova_K", "PS", "VOLT STRIKER", true, false),
      this._mk("DriftLord", "XB", "VANTA GT", false, false),
      this._mk("Kaito", "PC", "DUNE CRAWLER", true, false),
      this._mk("Mira", "PS", "VANTA GT", false, false),
    ];
    this.board = BOARD_SEED.map((e) => ({ ...e, trend: 0 }));
  }

  _mk(name, platform, vehicle, ready, me) {
    return { name, platform, vehicle, ready, me: !!me, ping: rnd(12, 68),
             color: AVATAR_COLORS[name.length % AVATAR_COLORS.length] };
  }

  get me() { return this.players.find((p) => p.me); }
  get readyCount() { return this.players.filter((p) => p.ready).length; }

  /* ---- lobby ops (mock EOS_Lobby_*) ---- */
  toggleReady() {
    const me = this.me;
    me.ready = !me.ready;
    this.onLog(`<b>EOS_Lobby_UpdateMember</b> ${me.name} → ready=${me.ready ? 1 : 0}`);
    return me.ready;
  }
  addBot() {
    if (this.players.length >= this.lobby.maxSlots) return null;
    const b = BOT_POOL[this._botIdx++ % BOT_POOL.length];
    if (this.players.some((p) => p.name === b.name)) return this.addBot();
    const p = this._mk(b.name, b.platform, b.vehicle, Math.random() > 0.5, false);
    this.players.push(p);
    this.onLog(`<b>EOS_LobbyJoin</b> ${p.name} connected via crossplay (${p.platform}) ping=${p.ping}ms`);
    return p;
  }
  removeRandom() {
    const target = this.players.find((p) => !p.me);
    if (!target) return null;
    this.players.splice(this.players.indexOf(target), 1);
    this.onLog(`<b>EOS_LobbyLeave</b> ${target.name} disconnected (timeout 5004)`);
    return target;
  }
  toggleHost() {
    this.isHost = !this.isHost;
    this.onLog(`<b>EOS_Lobby_PromoteMember</b> host → ${this.isHost ? "YOU_77 (local)" : "Nova_K (remote)"}`);
    return this.isHost;
  }
  setVehicle(v) {
    this.myVehicle = v;
    this.me.vehicle = v.name;
    this.onLog(`<b>EOS_Presence_Modify</b> vehicle=${v.id} status=IN_LOBBY`);
  }

  /* ---- map voting (mock EOS_Lobby attributes) ---- */
  vote(map) {
    if (this.myVote === map || !(map in this.mapVotes)) return;
    this.mapVotes[this.myVote] = Math.max(0, this.mapVotes[this.myVote] - 1);
    this.mapVotes[map]++;
    this.myVote = map;
    this.onLog(`<b>EOS_Lobby_Vote</b> YOU_77 → ${map}`);
  }
  botVoteTick() {
    const a = pick(this.maps);
    const b = pick(this.maps);
    if (a === b || this.mapVotes[a] <= 0) return null;
    this.mapVotes[a]--;
    this.mapVotes[b]++;
    return [a, b];
  }
  winningMap() {
    return this.maps.reduce((x, y) => (this.mapVotes[x] >= this.mapVotes[y] ? x : y));
  }

  /* ---- race results (mock EOS_Session_End) ---- */
  genResults() {
    const order = [...this.players].sort(() => Math.random() - 0.5);
    const base = 88 + Math.random() * 4;
    const deltas = [25, 18, 12, 6, 0, -6, -12, -18];
    return order.map((p, i) => ({
      name: p.name, me: p.me, vehicle: p.vehicle,
      time: base + i * (0.5 + Math.random() * 1.5) + Math.random() * 0.8,
      xp: Math.max(60, 420 - i * 38 + rnd(-20, 60)),
      delta: deltas[Math.min(i, deltas.length - 1)],
    }));
  }
  applyResults(results) {
    results.forEach((r) => {
      const e = this.board.find((x) => x.name === r.name);
      if (!e) return;
      e.rating = Math.max(1000, e.rating + r.delta);
      e.trend = r.delta > 0 ? 1 : r.delta < 0 ? -1 : 0;
      if (r.delta >= 18) e.wins += 1;
      if (r.delta >= 6) e.podiums += 1;
    });
    this.board.sort((a, b) => b.rating - a.rating);
  }

  /* ---- chat (mock EOS_Lobby_SendMessage) ---- */
  botReply() { return pick(CHAT_REPLIES); }

  /* ---- ambient simulation ---- */
  tickPings() { this.players.forEach((p) => { if (!p.me) p.ping = Math.max(8, p.ping + rnd(-9, 9)); }); }
  ambientEvent() {
    const others = this.players.filter((p) => !p.me);
    if (!others.length) return null;
    const p = pick(others);
    p.ready = !p.ready;
    this.onLog(`<b>EOS_Presence_Query</b> ${p.name} ready=${p.ready ? 1 : 0}`);
    return p;
  }

  /* ---- leaderboard (mock EOS_Leaderboards_Query) ---- */
  tickBoard(scope) {
    const pool = scope === "friends" ? this.board.filter((e) => e.friend) : this.board;
    const changed = new Set();
    for (let i = 0; i < 3; i++) {
      const e = pick(pool);
      if (!e) continue;
      const d = rnd(-9, 14);
      e.rating = Math.max(1000, e.rating + d);
      e.trend = d > 0 ? 1 : d < 0 ? -1 : 0;
      if (d !== 0 && Math.random() > 0.6) { e.wins += 1; e.podiums += 1; }
      changed.add(e.name);
    }
    this.board.sort((a, b) => b.rating - a.rating);
    return changed;
  }
  boardView(scope, query) {
    let rows = scope === "weekly"
      ? this.board.map((e) => ({ ...e, wins: Math.floor(e.wins / 9), podiums: Math.floor(e.podiums / 9), rating: 1200 + ((e.rating * 7) % 900) }))
      : this.board;
    if (scope === "friends") rows = rows.filter((e) => e.friend);
    if (query) rows = rows.filter((e) => e.name.toLowerCase().includes(query.toLowerCase()));
    return [...rows].sort((a, b) => b.rating - a.rating);
  }
}

import { corridorPoints } from "./transport-geometry.js";
import { streetLayout } from "./landscape-layout.js";
export const PRESETS = [
  {
    id: "parking",
    name: "Parking-protected street",
    type: "Urban",
    lanes: 2,
    laneWidth: 3.1,
    sidewalk: 3.2,
    bike: true,
    landscape: "parking",
    color: "#908f7f",
    speed: 30,
    curbStyle: "concrete",
    drainage: "none",
    desc: "Parallel parking lanes, a clear door buffer and separated cycle tracks.",
  },
  {
    id: "garden",
    name: "Garden boulevard",
    type: "Urban",
    lanes: 2,
    laneWidth: 3.1,
    sidewalk: 3.2,
    bike: true,
    landscape: "garden",
    color: "#7d9867",
    speed: 30,
    curbStyle: "concrete",
    drainage: "none",
    desc: "Planted central median, crossing refuges and buffered cycle tracks.",
  },
  {
    id: "cycle",
    name: "Protected cycle street",
    type: "Urban",
    lanes: 2,
    laneWidth: 3,
    sidewalk: 3,
    bike: true,
    landscape: "cycle",
    color: "#ad6c5d",
    speed: 30,
    curbStyle: "concrete",
    drainage: "none",
    desc: "Red 2 m cycle tracks separated from traffic by planted 1.2 m buffers.",
  },
  {
    id: "tram",
    name: "Green tram boulevard",
    type: "Urban",
    lanes: 2,
    laneWidth: 3.1,
    sidewalk: 3.2,
    bike: true,
    landscape: "tram",
    color: "#73947e",
    speed: 40,
    curbStyle: "concrete",
    drainage: "none",
    desc: "Reserved 7 m twin-track grass tramway with cycle tracks and planted buffers.",
  },
  {
    id: "roundabout",
    name: "Roundabout",
    type: "Urban",
    lanes: 1,
    laneWidth: 5.2,
    sidewalk: 1.8,
    bike: false,
    color: "#7d9688",
    speed: 20,
    desc: "Connected circulating ring with three or four entry roads.",
  },
  {
    id: "access",
    name: "Access street",
    type: "Residential",
    lanes: 2,
    laneWidth: 2.8,
    sidewalk: 2.4,
    bike: false,
    color: "#91a18f",
    speed: 20,
    curbStyle: "concrete",
    drainage: "none",
    desc: "Low-speed two-way access road.",
  },
  {
    id: "service",
    name: "Service lane",
    type: "Urban",
    lanes: 1,
    laneWidth: 4.2,
    sidewalk: 1.8,
    bike: false,
    color: "#b09a88",
    speed: 15,
    curbStyle: "spoon",
    drainage: "weep-channel",
    desc: "Narrow service access with formed concrete water channels.",
  },
  {
    id: "local",
    name: "Neighborhood street",
    type: "Residential",
    lanes: 2,
    laneWidth: 3.2,
    sidewalk: 2.4,
    bike: false,
    color: "#7d8e80",
    speed: 30,
    desc: "A quieter way through the city.",
  },
  {
    id: "avenue",
    name: "Complete avenue",
    type: "Urban",
    lanes: 4,
    laneWidth: 3.3,
    sidewalk: 3.2,
    bike: true,
    color: "#6c968e",
    speed: 50,
    desc: "Four lanes, protected cycling, generous sidewalks.",
  },
  {
    id: "country",
    name: "Country road",
    type: "Rural",
    lanes: 2,
    laneWidth: 3.1,
    sidewalk: 0,
    bike: false,
    color: "#b3a77b",
    speed: 70,
    desc: "Two lanes with a soft gravel shoulder.",
  },
  {
    id: "alley",
    name: "Shared lane",
    type: "Residential",
    lanes: 1,
    laneWidth: 4,
    sidewalk: 1.5,
    bike: false,
    color: "#9b9c90",
    speed: 15,
    desc: "A compact, shared neighborhood connection.",
  },
  {
    id: "trail",
    name: "Unpaved trail",
    type: "Off-road",
    lanes: 1,
    laneWidth: 4,
    sidewalk: 0,
    bike: false,
    color: "#b39b74",
    speed: 20,
    desc: "Terrain-following dirt and gravel.",
  },
  {
    id: "highway",
    name: "Regional highway",
    type: "Highway",
    lanes: 4,
    laneWidth: 3.6,
    sidewalk: 0,
    bike: false,
    color: "#7d8796",
    speed: 100,
    desc: "A wide regional route with a center reservation.",
  },
];
export const presetById = (id) =>
  PRESETS.find((p) => p.id === id) || PRESETS.find((p) => p.id === "local");
export const roadWidth = (e) => streetLayout(e).width;
export const dist = (a, b) => Math.hypot(a.x - b.x, a.z - b.z);
export const riverX = (z) =>
  -119 + 15 * Math.sin(z / 62) + 9 * Math.sin(z / 120);
export function terrainHeight(x, z, brushes = []) {
  let h =
    1.4 +
    0.55 * Math.sin(x / 55) * Math.cos(z / 63) +
    Math.max(0, (-x - 155) / 80) * 3;
  for (const b of brushes)
    h +=
      b.amount *
      Math.exp(-((x - b.x) ** 2 + (z - b.z) ** 2) / (2 * b.radius * b.radius));
  return h;
}
export function terrainSurface(x, z, brushes = []) {
  const h = terrainHeight(x, z, brushes);
  const d = Math.abs(x - riverX(z));
  const lake = Math.hypot((x + 151) / 24, (z - 72) / 32);
  if (lake < 1) return -2.1;
  if (lake < 1.16) return -2.1 + ((h + 2.1) * (lake - 1)) / 0.16;
  return d < 10 ? -2.2 : d < 15 ? -2.2 + ((h + 2.2) * (d - 10)) / 5 : h;
}
export function newProject() {
  return {
    version: 1,
    pavementLayoutVersion: 2,
    name: "Westhaven County",
    nodes: [],
    edges: [],
    brushes: [],
    corridors: [],
    nextId: 1,
  };
}
export function segmentIntersection(a, b, c, d) {
  const rx = b.x - a.x,
    rz = b.z - a.z,
    sx = d.x - c.x,
    sz = d.z - c.z,
    den = rx * sz - rz * sx;
  if (Math.abs(den) < 1e-8) return null;
  const t = ((c.x - a.x) * sz - (c.z - a.z) * sx) / den,
    u = ((c.x - a.x) * rz - (c.z - a.z) * rx) / den;
  if (t < -1e-7 || t > 1 + 1e-7 || u < -1e-7 || u > 1 + 1e-7) return null;
  return { x: a.x + t * rx, z: a.z + t * rz, t, u };
}
function nodeAt(p, pos, tolerance = 0.15) {
  const height = (v) =>
    v.elevation ?? terrainHeight(v.x, v.z, p.brushes) + 0.12;
  let n = p.nodes.find(
    (n) => dist(n, pos) < tolerance && Math.abs(height(n) - height(pos)) < 0.05,
  );
  if (!n) {
    n = {
      id: `n${p.nextId++}`,
      x: pos.x,
      z: pos.z,
      ...(pos.elevation !== undefined ? { elevation: pos.elevation } : {}),
    };
    p.nodes.push(n);
  }
  return n;
}
export function nearestSnap(p, point, enabled = true) {
  if (!enabled) return { ...point };
  let best = null,
    d = 8;
  for (const n of p.nodes) {
    const dd = dist(n, point);
    if (dd < d) {
      best = n;
      d = dd;
    }
  }
  if (best)
    return {
      x: best.x,
      z: best.z,
      ...(best.elevation !== undefined ? { elevation: best.elevation } : {}),
    };
  for (const e of p.edges) {
    if (e.structure !== "surface") continue;
    const a = p.nodes.find((n) => n.id === e.a),
      b = p.nodes.find((n) => n.id === e.b);
    const l = (b.x - a.x) ** 2 + (b.z - a.z) ** 2;
    const t = Math.max(
      0,
      Math.min(
        1,
        ((point.x - a.x) * (b.x - a.x) + (point.z - a.z) * (b.z - a.z)) / l,
      ),
    );
    const q = { x: a.x + t * (b.x - a.x), z: a.z + t * (b.z - a.z) };
    const dd = dist(q, point);
    if (dd < d) {
      best = q;
      d = dd;
    }
  }
  return best
    ? { x: best.x, z: best.z }
    : { x: Math.round(point.x / 2) * 2, z: Math.round(point.z / 2) * 2 };
}
export function addRoad(p, start, end, preset = "local", options = {}) {
  if (dist(start, end) < 4) return [];
  const props = {
    ...presetById(preset),
    preset,
    structure: "surface",
    markings: true,
    ...options,
  };
  delete props.id;
  // Refuse coincident segments rather than creating overlapping faces.
  for (const e of p.edges) {
    const a = p.nodes.find((n) => n.id === e.a),
      b = p.nodes.find((n) => n.id === e.b);
    const cross =
      (end.x - start.x) * (b.z - a.z) - (end.z - start.z) * (b.x - a.x);
    const side =
      (a.x - start.x) * (end.z - start.z) - (a.z - start.z) * (end.x - start.x);
    if (Math.abs(cross) < 1e-6 && Math.abs(side) < 1e-5) {
      const dx = end.x - start.x,
        dz = end.z - start.z,
        l = dx * dx + dz * dz,
        t1 = ((a.x - start.x) * dx + (a.z - start.z) * dz) / l,
        t2 = ((b.x - start.x) * dx + (b.z - start.z) * dz) / l;
      if (Math.min(1, Math.max(t1, t2)) - Math.max(0, Math.min(t1, t2)) > 0.001)
        return [];
    }
  }
  const cuts = [
    { ...start, t: 0 },
    { ...end, t: 1 },
  ];
  for (const e of [...p.edges]) {
    if (e.structure !== "surface" || props.structure !== "surface") continue;
    const a = p.nodes.find((n) => n.id === e.a),
      b = p.nodes.find((n) => n.id === e.b),
      hit = segmentIntersection(start, end, a, b);
    if (!hit) continue;
    const n = nodeAt(p, hit);
    cuts.push({ ...n, t: hit.t });
    if (hit.u > 1e-5 && hit.u < 1 - 1e-5) {
      p.edges.splice(p.edges.indexOf(e), 1);
      const children = [
        { ...e, id: `r${p.nextId++}`, b: n.id },
        { ...e, id: `r${p.nextId++}`, a: n.id },
      ];
      p.edges.push(...children);
      for (const c of p.corridors || [])
        if (c.sourceEdge === e.id)
          c.sourceEdge = children.find((child) =>
            [child.a, child.b].includes(c.sourceNode),
          )?.id;
    }
  }
  cuts.sort((a, b) => a.t - b.t);
  const unique = cuts.filter(
    (c, i) => i === 0 || Math.abs(c.t - cuts[i - 1].t) > 1e-5,
  );
  let ids = [];
  for (let i = 0; i < unique.length - 1; i++) {
    if (dist(unique[i], unique[i + 1]) < 0.1) continue;
    const a = nodeAt(p, unique[i]),
      b = nodeAt(p, unique[i + 1]);
    if (
      p.edges.some(
        (e) => (e.a === a.id && e.b === b.id) || (e.a === b.id && e.b === a.id),
      )
    )
      continue;
    const e = { ...props, id: `r${p.nextId++}`, a: a.id, b: b.id };
    p.edges.push(e);
    ids.push(e.id);
  }
  return ids;
}
export function cleanup(p) {
  const used = new Set(p.edges.flatMap((e) => [e.a, e.b]));
  p.nodes = p.nodes.filter((n) => used.has(n.id));
}
export function seedProject() {
  const p = newProject();
  const path = (points, preset, name, opts = {}) => {
    for (let i = 0; i < points.length - 1; i++)
      addRoad(
        p,
        { x: points[i][0], z: points[i][1] },
        { x: points[i + 1][0], z: points[i + 1][1] },
        preset,
        { name, ...opts },
      );
  };
  path(
    [
      [-260, 9],
      [-217, 0],
      [-170, -8],
      [-148, -10],
    ],
    "country",
    "Westhaven Road",
  );
  path(
    [
      [-148, -10],
      [-88, -10],
    ],
    "country",
    "Willow River Bridge",
    { structure: "bridge" },
  );
  path(
    [
      [-88, -10],
      [-48, -12],
      [0, -16],
      [58, -16],
      [115, -12],
      [172, 0],
      [232, 12],
      [270, 22],
    ],
    "avenue",
    "Harbor Avenue",
  );
  path(
    [
      [-48, -12],
      [-58, -64],
      [-30, -105],
      [22, -124],
      [78, -124],
      [126, -98],
      [151, -57],
      [172, 0],
      [168, 50],
      [142, 96],
      [92, 124],
      [36, 126],
      [-14, 100],
      [-42, 54],
      [-48, -12],
    ],
    "local",
    "Parkside Drive",
  );
  path(
    [
      [0, -16],
      [0, -68],
      [22, -124],
      [8, -165],
      [-34, -189],
      [-94, -180],
    ],
    "local",
    "Cedar Street",
  );
  path(
    [
      [-94, -180],
      [-147, -175],
    ],
    "country",
    "North River Crossing",
    { structure: "bridge" },
  );
  path(
    [
      [-147, -175],
      [-184, -154],
      [-209, -114],
      [-217, -61],
      [-217, 0],
      [-194, 53],
      [-181, 112],
      [-218, 165],
      [-261, 178],
    ],
    "country",
    "Old Mill Road",
  );
  path(
    [
      [58, -16],
      [64, -66],
      [78, -124],
      [108, -164],
      [165, -183],
      [228, -175],
    ],
    "local",
    "Birch Street",
  );
  path(
    [
      [-58, -64],
      [0, -68],
      [64, -66],
      [151, -57],
    ],
    "local",
    "Elm Street",
  );
  path(
    [
      [-30, -105],
      [126, -98],
    ],
    "alley",
    "Garden Mews",
  );
  path(
    [
      [0, -16],
      [8, 31],
      [18, 77],
      [36, 126],
      [23, 172],
      [-14, 198],
    ],
    "local",
    "Willow Lane",
  );
  path(
    [
      [58, -16],
      [66, 32],
      [72, 80],
      [92, 124],
      [127, 163],
      [193, 183],
      [258, 177],
    ],
    "local",
    "Oak Street",
  );
  path(
    [
      [-42, 54],
      [8, 31],
      [66, 32],
      [119, 38],
      [168, 50],
      [215, 69],
      [259, 110],
    ],
    "local",
    "Meadow Street",
  );
  path(
    [
      [-14, 100],
      [18, 77],
      [72, 80],
      [142, 96],
    ],
    "local",
    "Southbank Lane",
  );
  path(
    [
      [115, -12],
      [119, 38],
      [142, 96],
    ],
    "local",
    "Foundry Street",
  );
  path(
    [
      [-181, 112],
      [-161, 148],
      [-169, 190],
      [-201, 216],
    ],
    "trail",
    "Ridge Trail",
  );
  path(
    [
      [-209, -114],
      [-252, -130],
      [-269, -166],
    ],
    "trail",
    "Woodland Track",
  );
  path(
    [
      [228, -175],
      [244, -128],
      [238, -72],
      [232, 12],
    ],
    "country",
    "Eastfield Road",
  );
  p.corridors = [
    {
      id: "east-rail",
      kind: "rail",
      elevation: 10,
      points: Array.from({ length: 27 }, (_, i) => {
        const z = -234 + i * 18;
        return { x: 192 + Math.sin(z / 95) * 12, z };
      }),
    },
    {
      id: "south-monorail",
      kind: "monorail",
      elevation: 8,
      points: Array.from({ length: 18 }, (_, i) => {
        const x = -52 + i * 19;
        return { x, z: 153 + Math.sin(x / 95) * 15 };
      }),
    },
  ];
  return p;
}
export function validateProject(data) {
  if (
    !data ||
    typeof data.name !== "string" ||
    data.name.length > 120 ||
    data.version !== 1 ||
    !Array.isArray(data.nodes) ||
    !Array.isArray(data.edges) ||
    !Array.isArray(data.brushes) ||
    !Number.isInteger(data.nextId)
  )
    throw Error("Not a Frontier v1 project.");
  if (
    data.nodes.length > 5000 ||
    data.edges.length > 5000 ||
    data.brushes.length > 2000
  )
    throw Error("Project exceeds the editor safety limit.");
  const ids = new Set();
  for (const n of data.nodes) {
    if (
      typeof n.id !== "string" ||
      ids.has(n.id) ||
      !Number.isFinite(n.x) ||
      !Number.isFinite(n.z) ||
      Math.abs(n.x) > 10000 ||
      Math.abs(n.z) > 10000 ||
      (n.elevation !== undefined &&
        (!Number.isFinite(n.elevation) || Math.abs(n.elevation) > 200))
    )
      throw Error("Invalid node data.");
    ids.add(n.id);
  }
  if (
    data.sampleCityVersion !== undefined &&
    (data.sampleCityVersion !== 1 ||
      !data.cityOrigin ||
      ![data.cityOrigin.x, data.cityOrigin.z].every(
        (v) => Number.isFinite(v) && Math.abs(v) < 10000,
      ))
  )
    throw Error("Invalid sample-city metadata.");
  if (data.transportVersion !== undefined && data.transportVersion !== 1)
    throw Error("Invalid transport sample metadata.");
  if (
    data.bridgeNetworkVersion !== undefined &&
    data.bridgeNetworkVersion !== 1
  )
    throw Error("Invalid bridge network version.");
  if (
    data.bridgeNetworks !== undefined &&
    (!Array.isArray(data.bridgeNetworks) ||
      data.bridgeNetworks.length > 3 ||
      new Set(data.bridgeNetworks.map((n) => n.id)).size !==
        data.bridgeNetworks.length ||
      data.bridgeNetworks.some(
        (n) =>
          !["loop", "roundabout", "branch"].includes(n.id) ||
          !n.origin ||
          ![n.origin.x, n.origin.z, n.datum].every(
            (v) => Number.isFinite(v) && Math.abs(v) < 10000,
          ),
      ))
  )
    throw Error("Invalid bridge network metadata.");
  const edgeIds = new Set();
  for (const e of data.edges) {
    if (
      !ids.has(e.a) ||
      !ids.has(e.b) ||
      e.a === e.b ||
      edgeIds.has(e.id) ||
      typeof e.id !== "string" ||
      !PRESETS.some((p) => p.id === e.preset) ||
      !Number.isInteger(e.lanes) ||
      e.lanes < 1 ||
      e.lanes > 6 ||
      !Number.isFinite(e.laneWidth) ||
      e.laneWidth < 2 ||
      e.laneWidth > 6 ||
      !Number.isFinite(e.sidewalk) ||
      e.sidewalk < 0 ||
      e.sidewalk > 6 ||
      !["surface", "bridge"].includes(e.structure)
    )
      throw Error("Invalid road data.");
    if (
      e.elevationMode !== undefined &&
      (e.elevationMode !== "linear" || e.structure !== "bridge")
    )
      throw Error("Invalid elevated road profile.");
    if (
      e.bridgeNetwork !== undefined &&
      !["loop", "roundabout", "branch", "link"].includes(e.bridgeNetwork)
    )
      throw Error("Invalid bridge network identifier.");
    if (
      e.networkCurve !== undefined &&
      (typeof e.networkCurve !== "string" ||
        !Number.isFinite(e.networkCurveRadius) ||
        e.networkCurveRadius < 10 ||
        e.networkCurveRadius > 10000)
    )
      throw Error("Invalid bridge curve radius.");
    if (
      e.bridgeNetwork &&
      (e.sidewalk !== 0 ||
        e.bike ||
        (e.landscape && e.landscape !== "standard"))
    )
      throw Error(
        "Bridge networks currently use carriageways with concrete safety shoulders, not pedestrian/cycle street profiles.",
      );
    if (
      e.structure === "surface" &&
      [e.a, e.b].some((id) => {
        const n = data.nodes.find((n) => n.id === id);
        return (
          n.elevation !== undefined &&
          Math.abs(n.elevation - terrainHeight(n.x, n.z, data.brushes) - 0.12) >
            0.06
        );
      })
    )
      throw Error(
        "A ground road cannot join an elevated node. Use a bridge ramp.",
      );
    streetLayout(e);
    const streetEnums = {
      curbStyle: [
        "granite",
        "concrete",
        "rolled",
        "brick-roll",
        "spoon",
        "dished",
      ],
      rampStyle: ["flared", "blended", "grooved", "none"],
      paving: ["slab", "brick"],
      tactile: ["yellow", "stone", "red", "none"],
      drainage: [
        "none",
        "channel",
        "inlet",
        "brick-channel",
        "weep-channel",
        "slot-channel",
      ],
    };
    for (const [key, values] of Object.entries(streetEnums))
      if (e[key] !== undefined && !values.includes(e[key]))
        throw Error("Invalid streetscape option.");
    if (
      e.curbHeight !== undefined &&
      (!Number.isFinite(e.curbHeight) ||
        e.curbHeight < 0.08 ||
        e.curbHeight > 0.22)
    )
      throw Error("Invalid curb height.");
    if (
      (e.preset === "roundabout" ||
        [
          "roundabout",
          "roundaboutRole",
          "roundaboutRadius",
          "circulation",
        ].some((k) => e[k] !== undefined)) &&
      (typeof e.roundabout !== "string" ||
        !["ring", "entry"].includes(e.roundaboutRole) ||
        !Number.isFinite(e.roundaboutRadius) ||
        e.roundaboutRadius < 24 ||
        e.roundaboutRadius > 50 ||
        !["clockwise", "counterclockwise"].includes(e.circulation))
    )
      throw Error("Invalid roundabout metadata.");
    edgeIds.add(e.id);
  }
  for (const b of data.brushes)
    if (
      ![b.x, b.z, b.radius, b.amount].every(Number.isFinite) ||
      b.radius < 1 ||
      b.radius > 200 ||
      Math.abs(b.amount) > 20
    )
      throw Error("Invalid terrain data.");
  if (data.corridors !== undefined) {
    if (!Array.isArray(data.corridors) || data.corridors.length > 1000)
      throw Error("Invalid rail corridors.");
    for (const c of data.corridors)
      if (
        typeof c.id !== "string" ||
        !c.id ||
        c.id.length > 120 ||
        !["rail", "monorail"].includes(c.kind) ||
        !Number.isFinite(c.elevation) ||
        c.elevation < 0 ||
        c.elevation > 50 ||
        !Array.isArray(c.points) ||
        c.points.length < 2 ||
        c.points.length > 1000 ||
        c.points.some(
          (n) =>
            !Number.isFinite(n.x) ||
            !Number.isFinite(n.z) ||
            Math.abs(n.x) > 10000 ||
            Math.abs(n.z) > 10000,
        )
      )
        throw Error("Invalid rail geometry.");
  }
  if (
    new Set((data.corridors || []).map((c) => c.id)).size !==
    (data.corridors || []).length
  )
    throw Error("Duplicate rail corridor identifiers.");
  for (const e of data.edges) {
    if (
      e.bridgeRise !== undefined &&
      (!Number.isFinite(e.bridgeRise) || e.bridgeRise < 1 || e.bridgeRise > 15)
    )
      throw Error("Invalid bridge rise.");
    if (
      e.bridgeStyle !== undefined &&
      !["arch", "girder"].includes(e.bridgeStyle)
    )
      throw Error("Invalid bridge style.");
  }
  for (const c of data.corridors || []) {
    for (const [key, lo, hi] of [
      ["gauge", 1, 2],
      ["trackSpacing", 2, 5],
      ["turnRadius", 30, 200],
      ["exitRun", 20, 300],
      ["lead", 0, 100],
    ])
      if (
        c[key] !== undefined &&
        (!Number.isFinite(c[key]) || c[key] < lo || c[key] > hi)
      )
        throw Error("Invalid rail alignment parameters.");
    if (c.exitSide !== undefined && ![-1, 1].includes(c.exitSide))
      throw Error("Invalid rail turn direction.");
    if (
      c.sourceEdge !== undefined &&
      (!data.edges.some(
        (e) => e.id === c.sourceEdge && [e.a, e.b].includes(c.sourceNode),
      ) ||
        !Number.isFinite(c.turnRadius) ||
        !Number.isFinite(c.exitRun) ||
        ![-1, 1].includes(c.exitSide))
    )
      throw Error("Invalid rail attachment.");
  }
  const maxId = Math.max(
    0,
    ...data.nodes.map((n) => Number(n.id.slice(1)) || 0),
    ...data.edges.map((e) => Number(e.id.slice(1)) || 0),
  );
  if (data.nextId <= maxId)
    throw Error("Project identifier counter is invalid.");
  return data;
}

/** Rebuild incident edges atomically, re-noding any new at-grade crossings. */
export function moveNode(p, id, target) {
  const node = p.nodes.find((n) => n.id === id);
  if (!node || !Number.isFinite(target.x) || !Number.isFinite(target.z))
    return { ok: false, ids: [] };
  target = {
    ...target,
    ...(node.elevation !== undefined ? { elevation: node.elevation } : {}),
  };
  const incident = p.edges.filter((e) => e.a === id || e.b === id);
  if (!incident.length) return { ok: false, ids: [] };
  const trial = structuredClone(p),
    otherEnds = incident.map((e) =>
      p.nodes.find((n) => n.id === (e.a === id ? e.b : e.a)),
    );
  if (otherEnds.some((n) => dist(n, target) < 4)) return { ok: false, ids: [] };
  trial.edges = trial.edges.filter((e) => e.a !== id && e.b !== id);
  cleanup(trial);
  const ids = [];
  for (let i = 0; i < incident.length; i++) {
    const e = incident[i],
      options = { ...e };
    delete options.id;
    delete options.a;
    delete options.b;
    const added = addRoad(trial, target, otherEnds[i], e.preset, options);
    if (!added.length) return { ok: false, ids: [] };
    ids.push(...added);
    for (const c of trial.corridors || [])
      if (c.sourceEdge === e.id) {
        const pos = c.sourceNode === id ? target : otherEnds[i];
        const port = trial.nodes.find((n) => dist(n, pos) < 0.15);
        c.sourceNode = port.id;
        c.sourceEdge = added.find((id) => {
          const edge = trial.edges.find((e) => e.id === id);
          return edge.a === port.id || edge.b === port.id;
        });
        c.points = corridorPoints(trial, c);
      }
  }
  // Intersections created by a move can leave tiny residual stubs on OTHER roads.
  // Refuse the whole mutation before it can reach the renderer or saved project.
  if (
    trial.edges.some(
      (e) =>
        dist(
          trial.nodes.find((n) => n.id === e.a),
          trial.nodes.find((n) => n.id === e.b),
        ) <
        roadWidth(e) * 1.5,
    )
  )
    return { ok: false, ids: [] };
  Object.assign(p, trial);
  return { ok: true, ids };
}

// Upgrade only the old built-in widths, once. Explicit non-default widths survive.
export function upgradePedestrianSpace(p) {
  if (p.pavementLayoutVersion === 2) return false;
  const old = { local: 1.8, avenue: 2.4, alley: 0.6 };
  let changed = false;
  for (const e of p.edges)
    if (old[e.preset] === e.sidewalk) {
      e.sidewalk = presetById(e.preset).sidewalk;
      changed = true;
    }
  p.pavementLayoutVersion = 2;
  return changed;
}

/** Insert a filleted, connected ring and editable entry stubs as one graph edit. */
export function addRoundabout(
  p,
  center,
  { radius = 32, entries = 4, rotation = 0, clockwise = true } = {},
) {
  if (
    typeof clockwise !== "boolean" ||
    ![center.x, center.z, radius, rotation].every(Number.isFinite) ||
    radius < 24 ||
    radius > 50 ||
    ![3, 4].includes(entries) ||
    Math.abs(center.x) > 9900 ||
    Math.abs(center.z) > 9900
  )
    throw Error(
      "Roundabout needs a 24–50 m centerline radius and three or four entries.",
    );
  for (const e of p.edges) {
    const a = p.nodes.find((n) => n.id === e.a),
      b = p.nodes.find((n) => n.id === e.b),
      dx = b.x - a.x,
      dz = b.z - a.z,
      t = Math.max(
        0,
        Math.min(
          1,
          ((center.x - a.x) * dx + (center.z - a.z) * dz) / (dx * dx + dz * dz),
        ),
      );
    if (
      Math.hypot(center.x - a.x - t * dx, center.z - a.z - t * dz) <
      radius + roadWidth(e) / 2 + e.sidewalk + 4
    )
      throw Error(
        "Place the roundabout in clear space, then connect its entry roads. It will not cover an existing road.",
      );
  }
  const draft = structuredClone(p),
    group = `roundabout-${draft.nextId++}`,
    ids = [],
    ports = [],
    n = 12,
    sign = clockwise ? 1 : -1;
  const points = Array.from({ length: n }, (_, i) => {
    const a = (rotation * Math.PI) / 180 + (sign * i * Math.PI * 2) / n;
    return {
      x: center.x + radius * Math.cos(a),
      z: center.z + radius * Math.sin(a),
    };
  });
  const shared = {
    roundabout: group,
    roundaboutRadius: radius,
    circulation: clockwise ? "clockwise" : "counterclockwise",
  };
  for (let i = 0; i < n; i++)
    ids.push(
      ...addRoad(draft, points[i], points[(i + 1) % n], "roundabout", {
        ...shared,
        roundaboutRole: "ring",
        name: "Roundabout · circulating lane",
        curbStyle: "rolled",
        curbHeight: 0.1,
        drainage: "none",
        rampStyle: "none",
        tactile: "none",
      }),
    );
  for (let i = 0; i < entries; i++) {
    const k = (i * n) / entries,
      a = (rotation * Math.PI) / 180 + (sign * k * Math.PI * 2) / n,
      end = {
        x: center.x + (radius + 32) * Math.cos(a),
        z: center.z + (radius + 32) * Math.sin(a),
      };
    ports.push(end);
    const created = addRoad(draft, points[k], end, "access", {
      ...shared,
      roundaboutRole: "entry",
      name: `Roundabout · entry ${i + 1}`,
      rampStyle: "none",
      tactile: "none",
    });
    if (!created.length)
      throw Error(
        "A roundabout entry overlaps an existing road. Rotate or move the assembly.",
      );
    ids.push(...created);
  }
  Object.assign(p, draft);
  return { id: group, ids, ports };
}

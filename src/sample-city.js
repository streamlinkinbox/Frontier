import { addRoad, addRoundabout, roadWidth, validateProject } from "./model.js";
import {
  STREET_PRESETS,
  STREET_DEFAULTS,
  generateStreetscape,
  auditStreetscape,
} from "./streetscape.js";
import { generateRoadMesh, auditMesh } from "./geometry.js";
export const CITY_ID = "eastbank";
export function addSampleCity(project, options = {}) {
  if (project.sampleCityVersion === 1) return { added: false };
  const p = structuredClone(project),
    originalCount = p.edges.length;
  const anchor = options.anchor ??
    [...p.nodes].sort((a, b) => b.x - a.x || a.z - b.z)[0] ?? {
      x: 0,
      z: 0,
    };
  const origin = options.origin ?? {
    x: Math.max(420, anchor.x + 150),
    z: anchor.z,
  };
  const pos = ([x, z]) => ({ x: origin.x + x, z: origin.z + z });
  const road = (a, b, preset, name, extra = {}) =>
    addRoad(p, pos(a), pos(b), preset, { district: CITY_ID, name, ...extra });
  // Long straight spine: physical rails can continue through its two junctions.
  for (const [a, b] of [
    [0, 120],
    [120, 300],
    [300, 480],
  ])
    road([a, 0], [b, 0], "tram", "Eastbank · Central tram boulevard", {
      sampleKey: "road:tram",
      tramVehicle: a === 120,
    });
  const streets = [
    [[0, 0], [40, -130], "garden", "Botanical Avenue"],
    [[40, -130], [180, -190], "cycle", "Campus Cycleway"],
    [[180, -190], [330, -165], "parking", "Parking Avenue"],
    [[330, -165], [480, 0], "local", "Market Street"],
    [[0, 0], [80, 140], "access", "Station Access"],
    [[80, 140], [230, 185], "service", "Service Court"],
    [[230, 185], [390, 130], "alley", "Shared Lane"],
    [[390, 130], [480, 0], "local", "Terrace Street"],
    [[120, 0], [180, -190], "garden", "Garden Walk"],
    [[300, 0], [330, -165], "cycle", "Library Cycleway"],
    [[120, 0], [80, 140], "local", "Civic Street"],
    [[300, 0], [390, 130], "avenue", "South Avenue"],
  ];
  streets.forEach(([a, b, preset, name], i) => {
    const profile = STREET_PRESETS[i],
      settings = profile
        ? Object.fromEntries(
            Object.keys(STREET_DEFAULTS).map((k) => [
              k,
              profile[k] ?? STREET_DEFAULTS[k],
            ]),
          )
        : {};
    road(a, b, preset, `Eastbank · ${name}`, {
      ...settings,
      ...(profile
        ? {
            sidewalk: Math.max(2.4, preset === "garden" ? 3.2 : 2.4),
            curbSample: profile.id,
          }
        : {}),
      sampleKey: `road:${preset}`,
    });
  });
  const ring = addRoundabout(p, pos([650, 0]), { radius: 32 });
  for (const e of p.edges)
    if (e.roundabout === ring.id) {
      e.district = CITY_ID;
      e.sampleKey = "road:roundabout";
      e.name = `Eastbank · ${e.name}`;
    }
  road([480, 0], [586, 0], "access", "Eastbank · Roundabout Approach");
  road([650, 64], [650, 225], "highway", "Eastbank · Regional Highway", {
    sampleKey: "road:highway",
  });
  road([650, -64], [650, -225], "country", "Eastbank · Country Road", {
    sampleKey: "road:country",
  });
  road([650, -225], [520, -285], "trail", "Eastbank · Woodland Trail", {
    sampleKey: "road:trail",
  });
  if (project.nodes.length)
    addRoad(p, anchor, pos([0, 0]), "country", {
      district: CITY_ID,
      name: "Westhaven–Eastbank connector",
      sampleKey: "connector",
    });
  p.corridors ??= [];
  p.corridors.push(
    {
      id: "eastbank-rail",
      kind: "rail",
      elevation: 0.35,
      points: [pos([40, 245]), pos([285, 265]), pos([530, 245])],
    },
    {
      id: "eastbank-monorail",
      kind: "monorail",
      elevation: 8,
      points: [pos([20, -245]), pos([220, -260]), pos([420, -230])],
    },
  );
  p.sampleCityVersion = 1;
  p.cityOrigin = origin;
  validateProject(p);
  const result = generateRoadMesh(p),
    street = generateStreetscape(p, result),
    roadAudit = auditMesh(result, p),
    streetAudit = auditStreetscape(street);
  if (roadAudit.issues.length || !streetAudit.ok)
    throw Error(
      `Second city was not added: ${roadAudit.issues.join("; ") || JSON.stringify(streetAudit.issues.slice(0, 3))}`,
    );
  Object.assign(project, p);
  return {
    added: true,
    roads: p.edges.length - originalCount,
    result,
    streetscape: street,
    streetAudit,
  };
}
export function mapBounds(p) {
  const points = [...p.nodes, ...(p.corridors ?? []).flatMap((c) => c.points)];
  const margin = Math.max(
    55,
    ...p.edges.map((e) => roadWidth(e) / 2 + e.sidewalk + 25),
  );
  const minX = Math.min(-320, ...points.map((n) => n.x - margin)),
    maxX = Math.max(320, ...points.map((n) => n.x + margin)),
    minZ = Math.min(-235, ...points.map((n) => n.z - margin)),
    maxZ = Math.max(235, ...points.map((n) => n.z + margin));
  return {
    minX,
    maxX,
    minZ,
    maxZ,
    width: maxX - minX,
    height: maxZ - minZ,
    x: (minX + maxX) / 2,
    z: (minZ + maxZ) / 2,
  };
}

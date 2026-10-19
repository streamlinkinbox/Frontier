import { addRoad, cleanup, presetById, validateProject } from "./model.js";
import { addSampleCity, CITY_ID } from "./sample-city.js";
import { generateRoadMesh, auditMesh } from "./geometry.js";
import { generateStreetscape, auditStreetscape } from "./streetscape.js";
import { addRailExit, crossingIssues } from "./transport-geometry.js";
export function checkedProject(p) {
  validateProject(p);
  const result = generateRoadMesh(p),
    road = auditMesh(result, p);
  if (road.issues.length) throw Error(road.issues.join("; "));
  const streetscape = generateStreetscape(p, result),
    streetAudit = auditStreetscape(streetscape);
  if (!streetAudit.ok)
    throw Error(
      `Paving audit failed: ${JSON.stringify(streetAudit.issues.slice(0, 2))}`,
    );
  return { result, streetscape, streetAudit };
}
export function repairSampleCity(project) {
  if (project.sampleCityVersion !== 1 || !project.cityOrigin)
    throw Error("This project has no generated Eastbank district to repair.");
  const p = structuredClone(project),
    origin = { ...p.cityOrigin };
  const connector = p.edges.filter(
    (e) => e.district === CITY_ID && e.sampleKey === "connector",
  );
  const anchor = connector
    .flatMap((e) => [e.a, e.b])
    .map((id) => p.nodes.find((n) => n.id === id))
    .sort((a, b) => a.x - b.x)[0];
  if (!anchor)
    throw Error(
      "The original city connector is missing; export the project for a targeted repair instead.",
    );
  // Deliberately scoped to tagged generated objects; unrelated roads/terrain survive.
  p.edges = p.edges.filter((e) => e.district !== CITY_ID);
  p.corridors = p.corridors.filter(
    (c) =>
      ![
        "eastbank-rail",
        "eastbank-monorail",
        "eastbank-rail-exit",
        "eastbank-monorail-exit",
      ].includes(c.id),
  );
  cleanup(p);
  for (const c of p.corridors)
    if (c.sourceEdge && !p.edges.some((e) => e.id === c.sourceEdge)) {
      delete c.sourceEdge;
      delete c.sourceNode;
    }
  delete p.sampleCityVersion;
  delete p.transportVersion;
  addSampleCity(p, { origin, anchor });
  const built = installTransportSamples(p);
  Object.assign(project, p);
  return { ...built, repaired: true };
}
export function installTransportSamples(project) {
  if (project.transportVersion === 1) return { added: false };
  if (project.sampleCityVersion !== 1) throw Error("Add Eastbank first.");
  const p = structuredClone(project),
    o = p.cityOrigin,
    pos = ([x, z]) => ({ x: o.x + x, z: o.z + z });
  const road = (a, b, preset, name, extra = {}) =>
    addRoad(p, pos(a), pos(b), preset, {
      district: CITY_ID,
      name: `Eastbank · ${name}`,
      transportSample: true,
      ...extra,
    });
  const parking = p.edges.find(
    (e) =>
      e.district === CITY_ID &&
      (e.name === "Eastbank · Museum Avenue" ||
        (e.preset === "avenue" && e.curbSample)),
  );
  if (parking) {
    const preset = presetById("parking");
    for (const key of ["lanes", "laneWidth", "bike", "landscape", "speed"])
      parking[key] = preset[key];
    parking.preset = "parking";
    parking.sampleKey = "road:parking";
    if (parking.name === "Eastbank · Museum Avenue")
      parking.name = "Eastbank · Parking Avenue";
  }
  road([80, 140], [20, 365], "country", "South bridge approach");
  road([20, 365], [450, 365], "local", "South bowstring bridge", {
    structure: "bridge",
    bridgeRise: 7.5,
    bridgeStyle: "arch",
    sampleKey: "bridge:arch",
  });
  road([450, 365], [650, 225], "country", "South bridge return");
  road([230, 185], [230, 500], "country", "Railway crossing road", {
    sampleKey: "crossing",
  });
  road([330, -165], [440, -360], "country", "North bridge approach");
  road([440, -360], [860, -360], "local", "North girder bridge", {
    structure: "bridge",
    bridgeRise: 7.5,
    bridgeStyle: "girder",
    sampleKey: "bridge:girder",
  });
  road([860, -360], [650, -225], "country", "North bridge return");
  road([650, -225], [650, -500], "country", "North underpass");
  const [spur] = road([714, 0], [870, 0], "tram", "Tram departure street", {
    sampleKey: "rail-departure",
  });
  addRailExit(p, spur, { id: "eastbank-rail-exit", side: -1 });
  addRailExit(p, spur, {
    id: "eastbank-monorail-exit",
    kind: "monorail",
    side: 1,
  });
  p.transportVersion = 1;
  const issues = crossingIssues(p).filter((x) => x.includes("Eastbank"));
  if (issues.length) throw Error(issues.join(" "));
  const built = checkedProject(p);
  Object.assign(project, p);
  return { ...built, added: true };
}

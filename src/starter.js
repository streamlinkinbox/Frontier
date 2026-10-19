import { seedProject, presetById, addRoundabout } from "./model.js";
import { generateRoadMesh, auditMesh } from "./geometry.js";
import { generateStreetscape, auditStreetscape } from "./streetscape.js";
export function applyLandscape(e, mode) {
  if (e.roundabout)
    throw Error(
      "Keep the circulating ring and entry geometry intact; use a regular approach road for a landscape profile.",
    );
  if (mode === "standard") {
    e.landscape = "standard";
    e.preset = e.lanes >= 4 ? "avenue" : "local";
    return;
  }
  const preset = presetById(mode);
  if (!["garden", "cycle", "tram", "parking"].includes(mode))
    throw Error("Unknown landscape profile.");
  for (const key of [
    "lanes",
    "laneWidth",
    "sidewalk",
    "bike",
    "landscape",
    "speed",
    "curbStyle",
    "drainage",
  ])
    e[key] = preset[key];
  e.preset = mode;
  e.structure = "surface";
}
export function landscapedProject() {
  const p = seedProject();
  for (const [id, mode] of [
    ["r15", "garden"],
    ["r17", "tram"],
    ["r19", "cycle"],
  ]) {
    const e = p.edges.find((e) => e.id === id);
    applyLandscape(e, mode);
    e.name = `${presetById(mode).name} · Harbor`;
  }
  addRoundabout(p, { x: -230, z: 100 }, { radius: 28 });
  p.landscapeStarterVersion = 1;
  return p;
}
// Add only the requested roundabout to an older Westhaven save. Never replace
// existing roads/profiles or terrain, and never accept a colliding addition.
export function upgradeStarterRoundabout(p) {
  if (
    p.landscapeStarterVersion ||
    p.name !== "Westhaven County" ||
    p.edges.some((e) => e.roundabout)
  )
    return null;
  const draft = structuredClone(p);
  try {
    addRoundabout(draft, { x: -230, z: 100 }, { radius: 28 });
    const r = generateRoadMesh(draft);
    if (
      auditMesh(r, draft).issues.length ||
      !auditStreetscape(generateStreetscape(draft, r)).ok
    )
      return null;
    draft.landscapeStarterVersion = 1;
    return draft;
  } catch {
    return null;
  }
}

export function canInstallLandscapeExamples(p) {
  const base = seedProject();
  return ["r15", "r17", "r19"].every((id) => {
    const e = p.edges.find((e) => e.id === id),
      original = base.edges.find((e) => e.id === id);
    return (
      e &&
      !e.roundabout &&
      e.a === original.a &&
      e.b === original.b &&
      [e.a, e.b].every((id) => {
        const n = p.nodes.find((n) => n.id === id),
          b = base.nodes.find((n) => n.id === id);
        return n && n.x === b.x && n.z === b.z;
      })
    );
  });
}
export function installLandscapeExamples(p) {
  if (!canInstallLandscapeExamples(p))
    throw Error(
      "The original Harbor blocks have moved. Apply a landscape profile to a selected road instead.",
    );
  for (const [id, mode] of [
    ["r15", "garden"],
    ["r17", "tram"],
    ["r19", "cycle"],
  ])
    applyLandscape(
      p.edges.find((e) => e.id === id),
      mode,
    );
}

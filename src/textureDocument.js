import { normalizeSourceStroke } from "./textureStrokes.js";
import { normalizeTextureMask } from "./textureMasks.js";
import {
  PAINT_CHANNELS,
  normalizeChannelSettings,
} from "./paintChannelModel.js";
import { normalizePaintSettings } from "./paintToolCatalogue.js";
// Texture authoring starts as a small, portable document. This UI pass deliberately
// allocates no texture atlases and does not composite layers into the material.
export const TEXTURE_SCHEMA = "alloy.texture.v1";
export const TEXTURE_DRAFT_KEY = "alloy-texture-project-v1";
export const TEXTURE_LAYER_LIMIT = 64;
export const TEXTURE_FOLDER_DEPTH_LIMIT = 8;
export const TEXTURE_FILE_LIMIT = 1024 * 1024;
export const TEXTURE_KINDS = {
  folder: {
    label: "Folder",
    description:
      "A layer group with its own blend, opacity and optional coverage mask.",
    color: "#bfac88",
  },
  paint: {
    label: "Paint",
    description:
      "A hand-painted or imported 2D texture source; not yet composited onto the mesh.",
    color: "#d79564",
  },
  fill: {
    label: "Fill",
    description: "A layer for future uniform channel values.",
    color: "#779cd1",
  },
  material: {
    label: "Material",
    description: "A layer for a future material assignment.",
    color: "#aa98d0",
  },
  generator: {
    label: "Generator",
    description: "A layer for a future procedural source.",
    color: "#78ab96",
  },
};
export const TEXTURE_BLENDS = [
  "Normal",
  "Multiply",
  "Screen",
  "Overlay",
  "Add",
  "Darken",
];
export const TEXTURE_CHANNELS = PAINT_CHANNELS;
const channelIds = new Set(TEXTURE_CHANNELS.map((c) => c.id));
let token = 0;
function nextId() {
  return (
    globalThis.crypto?.randomUUID?.() ||
    `texture-${Date.now().toString(36)}-${++token}`
  );
}
function text(value, fallback, limit = 80) {
  return (
    (typeof value === "string"
      ? value.replace(/[\u0000-\u001f\u007f]/g, "").trim()
      : ""
    ).slice(0, limit) || fallback
  );
}
function number(value, fallback, min, max) {
  const n = Number(value);
  return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback;
}
function normalizeSourceAsset(raw) {
  if (
    !raw ||
    typeof raw !== "object" ||
    !["material", "brush", "generator"].includes(raw.type)
  )
    return null;
  if (typeof raw.id !== "string" || !/^[a-zA-Z0-9_-]{1,120}$/.test(raw.id))
    return null;
  return { type: raw.type, id: raw.id, name: text(raw.name, "Asset", 80) };
}
function normalizeEnabledChannels(ids) {
  // Imported normal targets obey the same Height dependency as the channel card.
  if (ids.includes("normal") && !ids.includes("height")) ids.push("height");
  return TEXTURE_CHANNELS.filter((c) => ids.includes(c.id)).map((c) => c.id);
}
function normalizeLayer(raw) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw))
    throw new Error("Every layer must be a layer record.");
  if (typeof raw.id !== "string" || !/^[a-zA-Z0-9_-]{1,80}$/.test(raw.id))
    throw new Error("A layer has an invalid identity.");
  if (typeof raw.kind !== "string" || !Object.hasOwn(TEXTURE_KINDS, raw.kind))
    throw new Error("A layer has an unsupported type.");
  if (
    raw.parentId != null &&
    raw.parentId !== "" &&
    (typeof raw.parentId !== "string" ||
      !/^[a-zA-Z0-9_-]{1,80}$/.test(raw.parentId))
  )
    throw new Error("A layer has an invalid folder identity.");
  return {
    id: raw.id,
    kind: raw.kind,
    name: text(
      raw.name,
      raw.kind === "folder"
        ? "Folder"
        : `${TEXTURE_KINDS[raw.kind].label} layer`,
    ),
    parentId:
      typeof raw.parentId === "string" && raw.parentId ? raw.parentId : null,
    visible: raw.visible !== false,
    locked: raw.locked === true,
    opacity: number(raw.opacity ?? 100, 100, 0, 100),
    blend: TEXTURE_BLENDS.includes(raw.blend) ? raw.blend : "Normal",
    channelSettings: normalizeChannelSettings(raw.channelSettings),
    sourceAsset:
      raw.kind === "folder" ? null : normalizeSourceAsset(raw.sourceAsset),
    channels: normalizeEnabledChannels([
      ...new Set(
        (Array.isArray(raw.channels)
          ? raw.channels
          : raw.kind === "folder"
            ? []
            : ["baseColor", "roughness"]
        )
          .map(
            (id) =>
              ({
                baseColour: "baseColor",
                metallic: "metalness",
                ambientOcclusion: "occlusion",
              })[id] || id,
          )
          .filter((id) => channelIds.has(id)),
      ),
    ]),
    mask: normalizeTextureMask(raw.mask),
  };
}
export function createTextureLayer(kind = "paint", options = {}) {
  return normalizeLayer({ id: nextId(), kind, ...options });
}
export function createTextureDocument() {
  return {
    schema: TEXTURE_SCHEMA,
    name: "Teapot study",
    painting: normalizePaintSettings(),
    lastStroke: null,
    scene: "teapot",
    // Topmost first, everywhere: the UI, history, files, and future compositor.
    layers: [
      createTextureLayer("paint", {
        id: "surface-detail",
        name: "Surface detail",
      }),
      createTextureLayer("fill", {
        id: "surface-finish",
        name: "Surface finish",
      }),
      createTextureLayer("material", {
        id: "base-material",
        name: "Base material",
        channels: [
          "baseColor",
          "metalness",
          "roughness",
          "height",
          "normal",
          "occlusion",
        ],
      }),
    ],
  };
}
export function validateTextureDocument(input) {
  if (!input || typeof input !== "object" || input.schema !== TEXTURE_SCHEMA)
    throw new Error("Choose an Alloy texture-layer project (.texture.json).");
  if (!Array.isArray(input.layers) || input.layers.length > TEXTURE_LAYER_LIMIT)
    throw new Error(
      `A layer project supports up to ${TEXTURE_LAYER_LIMIT} layers.`,
    );
  const layers = orderTextureLayers(input.layers.map(normalizeLayer));
  if (new Set(layers.map((l) => l.id)).size !== layers.length)
    throw new Error("Layer identities must be unique.");
  const stroke = input.lastStroke
    ? normalizeSourceStroke(input.lastStroke)
    : null;
  if (input.lastStroke && !stroke)
    throw new Error(
      "The recorded source stroke is invalid or exceeds its preview/point limit.",
    );
  const result = {
    schema: TEXTURE_SCHEMA,
    name: text(input.name, "Untitled surface"),
    scene: "teapot",
    painting: normalizePaintSettings(input.painting),
    lastStroke:
      stroke &&
      layers.some((l) => l.id === stroke.layerId && l.kind !== "folder")
        ? stroke
        : null,
    layers,
  };
  if (
    new TextEncoder().encode(JSON.stringify(result)).byteLength >
    TEXTURE_FILE_LIMIT
  )
    throw new Error("Layer project exceeds the 1 MB authoring limit.");
  return result;
}
export function parseTextureDocument(source) {
  if (
    typeof source !== "string" ||
    new TextEncoder().encode(source).byteLength > TEXTURE_FILE_LIMIT
  )
    throw new Error("Layer project files must be smaller than 1 MB.");
  let input;
  try {
    input = JSON.parse(source);
  } catch {
    throw new Error("The layer project is not valid JSON.");
  }
  return validateTextureDocument(input);
}

// Flat records stay portable; parent IDs define a validated, preorder folder tree.
export function textureAncestors(doc, id) {
  const records = new Map(doc.layers.map((l) => [l.id, l])),
    ancestors = [],
    seen = new Set([id]);
  let node = records.get(id);
  while (node?.parentId) {
    if (seen.has(node.parentId))
      throw new Error("Layer folders cannot form a cycle.");
    seen.add(node.parentId);
    node = records.get(node.parentId);
    if (!node) break;
    ancestors.push(node);
  }
  return ancestors;
}
export function isTextureLayerLocked(doc, id) {
  const layer = doc.layers.find((l) => l.id === id);
  return !!(layer?.locked || textureAncestors(doc, id).some((l) => l.locked));
}
export function isTextureLayerVisible(doc, id) {
  const layer = doc.layers.find((l) => l.id === id);
  return (
    !!layer &&
    layer.visible &&
    textureAncestors(doc, id).every((l) => l.visible)
  );
}
export function textureSubtree(doc, id) {
  return doc.layers.filter(
    (l) => l.id === id || textureAncestors(doc, l.id).some((a) => a.id === id),
  );
}
function orderTextureLayers(layers) {
  const records = new Map(layers.map((l) => [l.id, l]));
  if (records.size !== layers.length)
    throw new Error("Layer identities must be unique.");
  for (const layer of layers) {
    if (
      layer.parentId &&
      (!records.has(layer.parentId) ||
        records.get(layer.parentId).kind !== "folder")
    )
      throw new Error("A layer parent must be an existing folder.");
    const ancestors = textureAncestors({ layers }, layer.id);
    if (ancestors.length > TEXTURE_FOLDER_DEPTH_LIMIT)
      throw new Error(
        `Folders support up to ${TEXTURE_FOLDER_DEPTH_LIMIT} nested levels.`,
      );
  }
  const result = [];
  function walk(parent) {
    for (const l of layers.filter((l) => l.parentId === parent)) {
      result.push(l);
      if (l.kind === "folder") walk(l.id);
    }
  }
  walk(null);
  return result;
}
function immutableSubtree(doc, id) {
  return textureSubtree(doc, id).some((l) => isTextureLayerLocked(doc, l.id));
}
export function patchTextureLayer(doc, id, patch) {
  const layer = doc.layers.find((l) => l.id === id);
  if (!layer) return doc;
  const allowed = isTextureLayerLocked(doc, id)
    ? Object.fromEntries(
        Object.entries(patch).filter(([k]) =>
          ["locked", "visible"].includes(k),
        ),
      )
    : patch;
  // Membership changes go through moveTextureLayer so a whole subtree is moved.
  const next = normalizeLayer({
    ...layer,
    ...allowed,
    id: layer.id,
    kind: layer.kind,
    parentId: layer.parentId,
  });
  return JSON.stringify(next) === JSON.stringify(layer)
    ? doc
    : { ...doc, layers: doc.layers.map((l) => (l.id === id ? next : l)) };
}
export function insertTextureLayer(doc, kind, aboveId) {
  if (doc.layers.length >= TEXTURE_LAYER_LIMIT)
    throw new Error(`The ${TEXTURE_LAYER_LIMIT}-layer limit has been reached.`);
  const target = doc.layers.find((l) => l.id === aboveId);
  if (target && isTextureLayerLocked(doc, target.id))
    throw new Error(
      "Unlock the selected layer or folder before adding content.",
    );
  const parentId =
    target?.kind === "folder" ? target.id : target?.parentId || null;
  const layer = createTextureLayer(kind, { parentId }),
    layers = [...doc.layers],
    index = target
      ? target.kind === "folder"
        ? layers.indexOf(target) + 1
        : layers.indexOf(target)
      : 0;
  layers.splice(index, 0, layer);
  return { doc: validateTextureDocument({ ...doc, layers }), id: layer.id };
}
export function duplicateTextureLayer(doc, id) {
  const root = doc.layers.find((l) => l.id === id);
  if (!root || immutableSubtree(doc, id)) return { doc, id };
  const subtree = textureSubtree(doc, id);
  if (doc.layers.length + subtree.length > TEXTURE_LAYER_LIMIT)
    throw new Error(`The ${TEXTURE_LAYER_LIMIT}-layer limit has been reached.`);
  const identities = new Map(subtree.map((l) => [l.id, nextId()]));
  const copies = subtree.map((l) =>
    createTextureLayer(l.kind, {
      ...structuredClone(l),
      id: identities.get(l.id),
      parentId: l.id === id ? l.parentId : identities.get(l.parentId),
      name: l.id === id ? `${l.name} copy`.slice(0, 80) : l.name,
    }),
  );
  const layers = [...doc.layers];
  layers.splice(layers.indexOf(root), 0, ...copies);
  return {
    doc: validateTextureDocument({ ...doc, layers }),
    id: identities.get(id),
  };
}
export function removeTextureLayer(doc, id) {
  const index = doc.layers.findIndex((l) => l.id === id);
  if (index < 0 || immutableSubtree(doc, id)) return { doc, id };
  const ids = new Set(textureSubtree(doc, id).map((l) => l.id)),
    layers = doc.layers.filter((l) => !ids.has(l.id));
  return {
    doc: { ...doc, layers },
    id: layers[Math.min(index, layers.length - 1)]?.id || null,
  };
}
export function moveTextureLayer(doc, id, parentId = null, beforeId = null) {
  const root = doc.layers.find((l) => l.id === id),
    parent = doc.layers.find((l) => l.id === parentId),
    before = doc.layers.find((l) => l.id === beforeId);
  if (
    !root ||
    immutableSubtree(doc, id) ||
    (parentId &&
      (!parent ||
        parent.kind !== "folder" ||
        isTextureLayerLocked(doc, parentId))) ||
    (beforeId && (!before || before.parentId !== parentId))
  )
    return doc;
  const subtree = textureSubtree(doc, id),
    ids = new Set(subtree.map((l) => l.id));
  if (ids.has(parentId) || ids.has(beforeId)) return doc;
  const layers = doc.layers.filter((l) => !ids.has(l.id));
  let index = before
    ? layers.findIndex((l) => l.id === beforeId)
    : parentId
      ? layers.findIndex((l) => l.id === parentId) +
        textureSubtree({ layers }, parentId).length
      : layers.length;
  layers.splice(
    index,
    0,
    ...subtree.map((l) => (l.id === id ? { ...l, parentId } : l)),
  );
  const next = validateTextureDocument({ ...doc, layers });
  return JSON.stringify(next.layers) === JSON.stringify(doc.layers)
    ? doc
    : next;
}
export function reorderTextureLayer(doc, id, beforeId = null) {
  const before = doc.layers.find((l) => l.id === beforeId);
  if (beforeId && !before) return doc;
  return moveTextureLayer(doc, id, before?.parentId || null, beforeId);
}
export function ungroupTextureFolder(doc, id) {
  const folder = doc.layers.find((l) => l.id === id);
  if (folder?.kind !== "folder" || immutableSubtree(doc, id)) return doc;
  const layers = doc.layers
    .filter((l) => l.id !== id)
    .map((l) => (l.parentId === id ? { ...l, parentId: folder.parentId } : l));
  return validateTextureDocument({ ...doc, layers });
}

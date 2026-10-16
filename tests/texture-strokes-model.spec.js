import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  parseTextureDocument,
  appendTextureStroke,
  removeTextureStroke,
  duplicateTextureLayer,
  removeTextureLayer,
  insertTextureLayer,
  patchTextureLayer,
} from "../src/textureDocument.js";
import {
  SOURCE_STROKE_SCHEMA,
  LAYER_STROKE_LIMIT,
  normalizeLayerStrokes,
  strokeReplayPlan,
  layerStrokes,
  channelStrokes,
} from "../src/textureStrokes.js";
import { normalizePaintSettings } from "../src/paintToolCatalogue.js";
import { compositeContributors } from "../src/textureComposite.js";
const png = {
  name: "stroke.png",
  width: 1,
  height: 1,
  dataUrl:
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==",
};
const painted = (doc, id = "surface-detail") => {
  const layer = doc.layers.find((l) => l.id === id);
  return patchTextureLayer(doc, id, {
    channelSettings: {
      ...layer.channelSettings,
      baseColor: {
        ...layer.channelSettings.baseColor,
        mode: "texture",
        texture: { ...png, origin: "paint" },
      },
    },
  });
};
const stroke = (id, layerId = "surface-detail", extra = {}) => ({
  schema: SOURCE_STROKE_SCHEMA,
  id,
  layerId,
  layerName: "Surface detail",
  channelId: "baseColor",
  settings: normalizePaintSettings(),
  points: [
    { x: 0.1, y: 0.2, pressure: 1 },
    { x: 0.8, y: 0.7, pressure: 0.5 },
  ],
  image: png,
  ...extra,
});

test("unwritten layers allocate no stroke storage and the default project stays stroke-free", () => {
  const doc = createTextureDocument();
  for (const layer of doc.layers) expect(layer).not.toHaveProperty("strokes");
  expect(JSON.stringify(doc)).not.toMatch(/"strokes"/);
  expect(validateTextureDocument(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
});

test("appendTextureStroke records replayable strokes on their own layer only, in gesture order", () => {
  let doc = painted(createTextureDocument());
  const first = appendTextureStroke(doc, stroke("stroke-a"));
  expect(first).not.toBe(null);
  doc = first.doc;
  const second = appendTextureStroke(doc, stroke("stroke-b"));
  doc = second.doc;
  const layer = doc.layers.find((l) => l.id === "surface-detail");
  expect(layerStrokes(layer).map((s) => s.id)).toEqual(["stroke-a", "stroke-b"]);
  expect(channelStrokes(layer, "baseColor")).toHaveLength(2);
  expect(channelStrokes(layer, "roughness")).toEqual([]);
  for (const other of doc.layers.filter((l) => l.id !== "surface-detail"))
    expect(other).not.toHaveProperty("strokes");
  expect(parseTextureDocument(JSON.stringify(doc))).toEqual(doc);
});

test("stroke append is rejected for folders, locked layers, disabled channels and non-texture modes", () => {
  const doc = painted(createTextureDocument());
  expect(appendTextureStroke(doc, stroke("s1", "missing-layer"))).toBe(null);
  expect(
    appendTextureStroke(doc, { ...stroke("s1"), channelId: "roughness" }),
  ).toBe(null); // roughness is enabled but still in Value mode
  const foldered = insertTextureLayer(doc, "folder", "surface-detail").doc;
  const folder = foldered.layers.find((l) => l.kind === "folder");
  expect(appendTextureStroke(foldered, stroke("s1", folder.id))).toBe(null);
  const locked = patchTextureLayer(doc, "surface-detail", { locked: true });
  expect(appendTextureStroke(locked, stroke("s1"))).toBe(null);
  expect(appendTextureStroke(doc, { ...stroke("s1"), points: [] })).toBe(null);
  expect(
    appendTextureStroke(doc, { ...stroke("s1"), channelId: "normal" }),
  ).toBe(null);
});

test("the per-layer stroke limit is enforced and malformed stored strokes fail loudly, never drop silently", () => {
  let doc = painted(createTextureDocument());
  for (let i = 0; i < LAYER_STROKE_LIMIT; i++)
    doc = appendTextureStroke(doc, stroke(`stroke-${i}`)).doc;
  expect(appendTextureStroke(doc, stroke("one-too-many"))).toBe(null);
  expect(() =>
    normalizeLayerStrokes([{ ...stroke("bad"), points: [] }], "surface-detail"),
  ).toThrow(/stroke/);
  expect(() =>
    validateTextureDocument({
      ...createTextureDocument(),
      layers: [
        {
          ...createTextureLayer("paint", { id: "surface-detail" }),
          strokes: [{ ...stroke("bad"), image: { ...png, width: 9 } }],
        },
      ],
    }),
  ).toThrow(/stroke/);
});

test("a stroke's replay base is kept, and the replay plan replays remaining strokes over it in order", () => {
  let doc = painted(createTextureDocument());
  doc = appendTextureStroke(doc, stroke("stroke-a", "surface-detail", { base: png })).doc;
  doc = appendTextureStroke(doc, stroke("stroke-b")).doc;
  const layer = doc.layers.find((l) => l.id === "surface-detail");
  const plan = strokeReplayPlan(layer.strokes, "baseColor");
  expect(plan.base).toEqual(png);
  expect(plan.strokes.map((s) => s.id)).toEqual(["stroke-a", "stroke-b"]);
  const afterDelete = strokeReplayPlan(
    layer.strokes.filter((s) => s.id !== "stroke-a"),
    "baseColor",
  );
  expect(afterDelete.strokes.map((s) => s.id)).toEqual(["stroke-b"]);
});

test("removeTextureStroke drops only that record; removing the last one frees the stroke storage", () => {
  let doc = painted(createTextureDocument());
  doc = appendTextureStroke(doc, stroke("stroke-a")).doc;
  doc = appendTextureStroke(doc, stroke("stroke-b")).doc;
  const removed = removeTextureStroke(doc, "surface-detail", "stroke-a");
  expect(layerStrokes(removed.layers[0]).map((s) => s.id)).toEqual(["stroke-b"]);
  const emptied = removeTextureStroke(removed, "surface-detail", "stroke-b");
  expect(emptied.layers[0]).not.toHaveProperty("strokes");
  expect(removeTextureStroke(doc, "surface-detail", "nope")).toBe(doc);
  const locked = patchTextureLayer(doc, "surface-detail", { locked: true });
  expect(removeTextureStroke(locked, "surface-detail", "stroke-a")).toBe(locked);
});

test("duplicating a painted layer copies its strokes under fresh identities owned by the copy", () => {
  let doc = painted(createTextureDocument());
  doc = appendTextureStroke(doc, stroke("stroke-a")).doc;
  const { doc: copied, id } = duplicateTextureLayer(doc, "surface-detail");
  const copy = copied.layers.find((l) => l.id === id);
  expect(layerStrokes(copy)).toHaveLength(1);
  expect(copy.strokes[0].id).not.toBe("stroke-a");
  expect(copy.strokes[0].layerId).toBe(id);
  expect(copy.strokes[0].points).toEqual(
    doc.layers.find((l) => l.id === "surface-detail").strokes[0].points,
  );
  // Deleting a stroke from the copy leaves the original untouched.
  const pruned = removeTextureStroke(copied, id, copy.strokes[0].id);
  expect(
    layerStrokes(pruned.layers.find((l) => l.id === "surface-detail")),
  ).toHaveLength(1);
  expect(parseTextureDocument(JSON.stringify(copied))).toEqual(copied);
});

test("deleting a layer deletes its strokes with it; other layers keep theirs", () => {
  let doc = painted(createTextureDocument());
  doc = appendTextureStroke(doc, stroke("stroke-a")).doc;
  const { doc: after } = removeTextureLayer(doc, "surface-detail");
  expect(after.layers.some((l) => l.id === "surface-detail")).toBe(false);
  expect(JSON.stringify(after)).not.toMatch(/stroke-a/);
});

test("layer composite walks bottom-to-top, honouring visibility, folder inheritance, opacity and blend", () => {
  let doc = painted(createTextureDocument());
  // Second painted layer above Surface detail.
  const added = insertTextureLayer(doc, "paint", "surface-detail");
  doc = painted(added.doc, added.id);
  doc = patchTextureLayer(doc, added.id, { opacity: 50, blend: "Multiply", name: "Top paint" });
  doc = appendTextureStroke(doc, stroke("stroke-a")).doc;
  const contributors = compositeContributors(doc, "baseColor");
  // Topmost-first storage must composite in reverse: Surface detail (bottom
  // of the two painted layers) first, then the inserted layer.
  expect(contributors.map((c) => c.layerId)).toEqual(["surface-detail", added.id]);
  expect(contributors[1]).toMatchObject({ opacity: 0.5, composite: "multiply", strokes: 0 });
  expect(contributors[0].strokes).toBe(1);
  // Hiding the top layer removes exactly its contribution.
  const hidden = patchTextureLayer(doc, added.id, { visible: false });
  expect(compositeContributors(hidden, "baseColor").map((c) => c.layerId)).toEqual([
    "surface-detail",
  ]);
  // A hidden folder hides its children; a faded folder multiplies opacity.
  const foldered = insertTextureLayer(doc, "folder", added.id).doc;
  const folder = foldered.layers.find((l) => l.kind === "folder");
  const nested = compositeContributors(
    (() => {
      const withChild = {
        ...foldered,
        layers: foldered.layers.map((l) =>
          l.id === added.id ? { ...l, parentId: folder.id } : l,
        ),
      };
      return validateTextureDocument(withChild);
    })(),
    "baseColor",
  );
  expect(nested.map((c) => c.layerId)).toEqual(["surface-detail", added.id]);
  const fadedFolder = validateTextureDocument({
    ...foldered,
    layers: foldered.layers.map((l) =>
      l.id === added.id
        ? { ...l, parentId: folder.id }
        : l.id === folder.id
          ? { ...l, opacity: 40 }
          : l,
    ),
  });
  expect(
    compositeContributors(fadedFolder, "baseColor").find((c) => c.layerId === added.id)
      .opacity,
  ).toBeCloseTo(0.2);
  const hiddenFolder = validateTextureDocument({
    ...fadedFolder,
    layers: fadedFolder.layers.map((l) =>
      l.id === folder.id ? { ...l, visible: false } : l,
    ),
  });
  expect(compositeContributors(hiddenFolder, "baseColor").map((c) => c.layerId)).toEqual([
    "surface-detail",
  ]);
  // Channels without a texture raster never contribute.
  expect(compositeContributors(doc, "roughness")).toEqual([]);
});

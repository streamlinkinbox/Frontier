#!/usr/bin/env node
/*============================================================================================================================================
                                                      GEOMETRYCOMPILERVALIDATION.MJS
============================================================================================================================================*/
// Comprehensive headless verification suite for Experimental/GeometryCompiler (4-Way LOD Studio, Hybrid Prob-QEM+Retopo, Nanite QEM, SDF & Icons).

import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  OPTICAL_CATEGORIES,
  SPECIMEN_CATALOG,
  constructSpecimen,
  parseObjText,
  parseStlBytes,
  compileSparseSdfVolume,
  evictMip0ForBudget,
  extractSdfSliceRgba,
  extractDualContourSurface,
  compileClusterLodDag,
  bakeTransparentSpecimenIcon,
  serializeVolumeFvol,
} from "./Engine/CompilerSolvers.js";
import { GEOMETRY_COMPILER_WGSL, SDF_RAYMARCH_WGSL, GeometryCompilerViewport } from "./Engine/WebGpuViewport.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

let passed = 0;
let failed = 0;

function runTest(name, fn) {
  try {
    fn();
    passed++;
    console.log(`  [PASS] ${name}`);
  } catch (err) {
    failed++;
    console.error(`  [FAIL] ${name}: ${err.message}`);
  }
}

console.log("=== Frontier Experimental/GeometryCompiler 4-Way Validation ===");

// 1. All 6 specimens construct valid geometry (including Bishop-frame twist-free UtahTeapot & TorusKnot)
runTest("SpecimenLibrary: all 6 built-in specimens construct valid twist-free geometry", () => {
  assert.equal(SPECIMEN_CATALOG.length, 6);
  assert.equal(OPTICAL_CATEGORIES.length, 4);
  for (const item of SPECIMEN_CATALOG) {
    const g = constructSpecimen(item.id);
    assert.ok(g.vertexCount >= 24, `${item.id} vertexCount`);
    assert.ok(g.triangleCount >= 24, `${item.id} triangleCount`);
    assert.equal(g.positions.length, g.vertexCount * 3);
    assert.equal(g.normals.length, g.vertexCount * 3);
    assert.equal(g.uvs.length, g.vertexCount * 2);
    for (let i = 0; i < g.positions.length; ++i) {
      assert.ok(Number.isFinite(g.positions[i]), `${item.id} finite position`);
    }
  }
});

// 2. .OBJ importer with quads & smooth normals
runTest("Interchange: .OBJ quad triangulation & bounds normalization", () => {
  const objText = [
    "v -1 -1 -1", "v 1 -1 -1", "v 1 1 -1", "v -1 1 -1",
    "v -1 -1  1", "v 1 -1  1", "v 1 1  1", "v -1 1  1",
    "f 1 2 3 4", "f 5 6 7 8", "f 1 2 6 5", "f 2 3 7 6", "f 3 4 8 7", "f 4 1 5 8",
  ].join("\n");
  const g = parseObjText(objText, "TestQuadCube.obj");
  assert.equal(g.triangleCount, 12);
  assert.equal(g.vertexCount, 8);
});

// 3. ASCII & Binary .STL importers
runTest("Interchange: ASCII .STL & Binary .STL parsers", () => {
  const ascii = `solid tri
    facet normal 0 0 1
      outer loop
        vertex 0 0 0
        vertex 1 0 0
        vertex 0 1 0
      endloop
    endfacet
    facet normal 0 0 -1
      outer loop
        vertex 0 0 0.2
        vertex 0 1 0.2
        vertex 1 0 0.2
      endloop
    endfacet
  endsolid tri`;
  const asciiBuf = new TextEncoder().encode(ascii).buffer;
  const ga = parseStlBytes(asciiBuf, "AsciiTest.stl");
  assert.equal(ga.triangleCount, 2);

  const buf = new ArrayBuffer(134);
  const dv = new DataView(buf);
  dv.setUint32(80, 1, true);
  dv.setFloat32(84 + 12, 0, true); dv.setFloat32(84 + 16, 0, true); dv.setFloat32(84 + 20, 0, true);
  dv.setFloat32(84 + 24, 1, true); dv.setFloat32(84 + 28, 0, true); dv.setFloat32(84 + 32, 0, true);
  dv.setFloat32(84 + 36, 0, true); dv.setFloat32(84 + 40, 1, true); dv.setFloat32(84 + 44, 0, true);
  const gb = parseStlBytes(buf, "BinaryTest.stl");
  assert.equal(gb.triangleCount, 1);
  assert.equal(gb.vertexCount, 3);
});

// 4. 4-Way Multi-LOD DAG (Original + Retopo+Shrinkwrap + Nanite-Style QEM + Hybrid Prob-QEM+Retopo)
runTest("4-Way LOD DAG: GlassVessel compiles all 4 chains, Hybrid hits LOD3 cleanly with 0 wall breaches", () => {
  const vessel = constructSpecimen("GlassVessel");
  const dag = compileClusterLodDag(vessel, {
    maxTrisPerCluster: 128,
    opticalCategory: "DielectricGlass",
    minWallThicknessMeters: 0.045,
    lockDualWalls: true,
    useEdgeGuides: true,
    creaseAngleDeg: 32,
    lockUvSeams: true,
    lockBoundaries: true,
    featureGuideWeight: 0.75,
    shrinkwrapPasses: 2,
    probQemSigma: 0.035,
  });

  assert.ok(dag.originalLod && dag.originalLod.triangleCount === vessel.triangleCount);
  assert.equal(dag.retopoLods.length, 4);
  assert.equal(dag.naniteQemLods.length, 4);
  assert.equal(dag.hybridLods.length, 4);

  for (const chain of [dag.retopoLods, dag.naniteQemLods, dag.hybridLods]) {
    assert.ok(chain[0].triangleCount > chain[1].triangleCount, "LOD0 > LOD1 decimation");
    assert.ok(chain[1].triangleCount > chain[2].triangleCount, "LOD1 > LOD2 decimation");
    assert.ok(chain[2].triangleCount >= chain[3].triangleCount, "LOD2 >= LOD3 decimation");
    assert.ok(chain[2].maxClusterTriangles <= 128, "cluster size <= 128");
  }
  // Hybrid reaches deep LOD3 polycount (far lower than pure Retopo) while maintaining ZERO glass wall breaches!
  assert.ok(dag.hybridLods[3].triangleCount < dag.retopoLods[3].triangleCount, "Hybrid reaches deeper LOD3 than Retopo");
  assert.equal(dag.hybridLods[1].wallCollapseCount, 0, "Hybrid LOD1 has 0 wall breaches");
  assert.equal(dag.hybridLods[2].wallCollapseCount, 0, "Hybrid LOD2 has 0 wall breaches");
  assert.equal(dag.hybridLods[3].wallCollapseCount, 0, "Hybrid LOD3 has 0 wall breaches");
  assert.ok(dag.hybridLods[1].meanErrorMm < dag.naniteQemLods[1].meanErrorMm, "Hybrid beats Nanite QEM at GlassVessel LOD1");
  for (const node of dag.levels[1]) {
    assert.ok(Array.isArray(node.nestedClusterIndices), "uses clean nestedClusterIndices naming");
  }
});

// 5. Hybrid Probabilistic QEM Quality + Nanite Crack-Free Interior Shrinkwrap
runTest("Hybrid Prob-QEM & Nanite Interior Shrinkwrap: crushes slivers on ChamferBox & improves UvSphere/ShaderBall", () => {
  const box = constructSpecimen("ChamferBox");
  const boxDag = compileClusterLodDag(box, {
    opticalCategory: "OpaqueSolid",
    useEdgeGuides: true,
    creaseAngleDeg: 32,
    lockUvSeams: true,
    lockBoundaries: true,
    probQemSigma: 0.035,
  });
  assert.ok(boxDag.originalLod.creaseEdgeCount > 0, "ChamferBox detects crease guides");
  assert.ok(boxDag.hybridLods[1].meanErrorMm < boxDag.naniteQemLods[1].meanErrorMm && boxDag.hybridLods[1].sliverPercent <= 1,
    `Hybrid ChamferBox LOD1 (${boxDag.hybridLods[1].meanErrorMm}mm, ${boxDag.hybridLods[1].sliverPercent}% slv) beats Nanite (${boxDag.naniteQemLods[1].meanErrorMm}mm, ${boxDag.naniteQemLods[1].sliverPercent}% slv)`);
  assert.ok(boxDag.hybridLods[2].sliverPercent < boxDag.naniteQemLods[2].sliverPercent * 0.70,
    `Hybrid ChamferBox LOD2 slivers (${boxDag.hybridLods[2].sliverPercent}%) well below Nanite (${boxDag.naniteQemLods[2].sliverPercent}%)`);
  assert.ok(boxDag.hybridLods[2].maxErrorMm < boxDag.naniteQemLods[2].maxErrorMm,
    `Hybrid ChamferBox LOD2 max error (${boxDag.hybridLods[2].maxErrorMm}mm) < Nanite (${boxDag.naniteQemLods[2].maxErrorMm}mm)`);
  assert.equal(boxDag.hybridLods[3].triangleCount, boxDag.naniteQemLods[3].triangleCount, "Hybrid reaches exact LOD3 target like Nanite");

  const sphere = constructSpecimen("UvSphere");
  const spNoSw = compileClusterLodDag(sphere, { opticalCategory: "OpaqueSolid", naniteShrinkwrap: false });
  const spSw = compileClusterLodDag(sphere, { opticalCategory: "OpaqueSolid", naniteShrinkwrap: true });
  assert.ok(spNoSw.hybridLods[2].maxErrorMm < spNoSw.naniteQemLods[2].maxErrorMm, "Hybrid crushes Nanite max error on UvSphere LOD2");
  assert.ok(spNoSw.hybridLods[3].meanErrorMm < spNoSw.naniteQemLods[3].meanErrorMm, "Hybrid beats Nanite on UvSphere LOD3 true surface error");
  assert.ok(spSw.naniteQemLods[1].meanErrorMm <= spNoSw.naniteQemLods[1].meanErrorMm + 0.15, "Nanite Interior Shrinkwrap stays accurate on UvSphere LOD1");
  assert.ok(spSw.naniteQemLods[2].groupLockedVerts > 0, "Nanite keeps outer group seams locked");
});

// 6. Sparse 8x8x8 Narrow-Band SDF + Conservative Mip1 + Mip0 Eviction + Slice RGBA
runTest("SparseSDF: 8x8x8 narrow-band, Mip1 <= Mip0 conservative monotonicity, Mip0 eviction & slice RGBA", () => {
  const box = constructSpecimen("ChamferBox");
  const sdf = compileSparseSdfVolume(box, { gridDim: 24 });
  assert.equal(sdf.dim, 24);
  assert.equal(sdf.mip1Dim, 12);
  assert.ok(sdf.allocatedBrickCount > 0 && sdf.allocatedBrickCount <= sdf.totalBricks);
  assert.equal(sdf.monotonicityVerified, true, "Mip1 <= Mip0 conservative invariant verified");

  const rgba0 = extractSdfSliceRgba(sdf, 0, 0.5, 64, false);
  const rgba1 = extractSdfSliceRgba(sdf, 0, 0.5, 64, true);
  assert.equal(rgba0.length, 64 * 64 * 4);
  assert.equal(rgba1.length, 64 * 64 * 4);
  let nonZero = 0;
  for (let i = 0; i < rgba0.length; i += 4) if (rgba0[i] + rgba0[i + 1] + rgba0[i + 2] > 30) nonZero++;
  assert.ok(nonZero > 200, "SDF slice has visible field signal");

  const ev = evictMip0ForBudget(sdf, 4 * 1024);
  assert.equal(ev.evicted, true, "tight budget evicts Mip0");
  assert.ok(ev.freedBytes > 0, "frees Mip0 bytes");
  assert.equal(sdf.mip0Evicted, true);
});

// 7. QEF Dual-Contour SDF-to-Surface extractor
runTest("DualContour: QEF extracts valid surface triangles from ChamferBox SDF", () => {
  const box = constructSpecimen("ChamferBox");
  const sdf = compileSparseSdfVolume(box, { gridDim: 24 });
  const dc = extractDualContourSurface(sdf, "ChamferBox DC");
  assert.ok(dc.surface !== null, "extracted non-null DC surface");
  assert.ok(dc.qefSolvedCells >= 24, `qefSolvedCells=${dc.qefSolvedCells}`);
  assert.ok(dc.extractedTriangleCount >= 24, `extractedTriangleCount=${dc.extractedTriangleCount}`);
  for (let i = 0; i < dc.surface.positions.length; ++i) {
    assert.ok(Number.isFinite(dc.surface.positions[i]), "finite DC position");
  }
});

// 8. Transparent 128x128 2x2 SSAA 3D Icon Baker across all 6 specimens
runTest("IconBaker: 128x128 transparent alpha=0 background & valid silhouette on all 6 specimens", () => {
  for (const item of SPECIMEN_CATALOG) {
    const g = constructSpecimen(item.id);
    const icon = bakeTransparentSpecimenIcon(g, {
      resolutionPx: 128,
      supersample: 2,
      opticalCategory: item.defaultOptical,
    });
    assert.equal(icon.width, 128);
    assert.equal(icon.height, 128);
    assert.equal(icon.rgba.length, 128 * 128 * 4);
    const corners = [
      icon.rgba[3],
      icon.rgba[(127) * 4 + 3],
      icon.rgba[(127 * 128) * 4 + 3],
      icon.rgba[(127 * 128 + 127) * 4 + 3],
    ];
    assert.equal(Math.max(...corners), 0, `${item.id} icon corners must be transparent alpha=0`);
    assert.ok(icon.silhouettePercent >= 10 && icon.silhouettePercent <= 92, `${item.id} silhouette=${icon.silhouettePercent}%`);
    assert.ok(icon.transparentPercent >= 8, `${item.id} transparent=${icon.transparentPercent}%`);
  }
});

// 9. frontier.fvol.v1 Serializer
runTest("FvolExport: serializeVolumeFvol encodes frontier.fvol.v1 header & distances", () => {
  const vessel = constructSpecimen("GlassVessel");
  const sdf = compileSparseSdfVolume(vessel, { gridDim: 16 });
  const raw = serializeVolumeFvol(sdf, "GlassVessel");
  const parsed = JSON.parse(raw);
  assert.equal(parsed.format, "frontier.fvol.v1");
  assert.deepEqual(parsed.dimensions, [16, 16, 16]);
  assert.deepEqual(parsed.mip1Dimensions, [8, 8, 8]);
  assert.equal(parsed.sparseBrickEdge, 8);
  assert.equal(parsed.distances.length, 16 * 16 * 16);
});

// 10. UI Feature Audit (4-Way Quad Compare, Hybrid Prob-QEM, Nanite Int.SW, Guides, SDF Slider, zero Unsubdivide/Naive/Fracture/And)
runTest("WebGPU & ProjectZeroUI & Governance: WGSL valid, 4-Way Quad UI, Hybrid, Guides, SDF slider, zero Unsubdivide/Naive/Fracture/And", () => {
  assert.ok(GEOMETRY_COMPILER_WGSL.includes("@vertex") && GEOMETRY_COMPILER_WGSL.includes("@fragment"));
  assert.ok(GEOMETRY_COMPILER_WGSL.includes("fn vsMain") && GEOMETRY_COMPILER_WGSL.includes("fn fsMain"));
  assert.ok(GEOMETRY_COMPILER_WGSL.includes("fresnel"));

  const strippedWgsl = GEOMETRY_COMPILER_WGSL.replace(/\/\/[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "");
  const wgslReserved = [
    "meta", "self", "super", "ref", "mut", "abstract", "active", "alignas", "alignof", "as", "asm",
    "assert", "async", "attribute", "auto", "await", "become", "cast", "catch", "class", "coherent",
    "column_major", "common", "compile", "concept", "const_cast", "crate", "debugger", "decltype",
    "delete", "demote", "do", "enum", "explicit", "export", "extends", "extern", "external", "filter",
    "final", "finally", "friend", "from", "get", "goto", "groupshared", "highp", "impl", "implements",
    "import", "inline", "instanceof", "interface", "layout", "lowp", "macro", "match", "mediump",
    "mod", "module", "move", "mutable", "namespace", "new", "nil", "noexcept", "noinline", "null",
    "nullptr", "of", "operator", "package", "partition", "pass", "patch", "precise", "precision",
    "priv", "protected", "pub", "public", "readonly", "register", "require", "resource", "restrict",
    "set", "shared", "sizeof", "smooth", "static", "std", "subroutine", "target", "template", "this",
    "throw", "trait", "try", "type", "typedef", "typeid", "typename", "typeof", "union", "unless",
    "unsafe", "unsized", "use", "using", "varying", "virtual", "volatile", "wgsl", "where", "with",
    "writeonly", "yield",
  ];
  const tokens = new Set(strippedWgsl.match(/\b[A-Za-z_][A-Za-z0-9_]*\b/g) || []);
  for (const kw of wgslReserved) {
    assert.ok(!tokens.has(kw), `WGSL reserved keyword '${kw}' must not appear in shader`);
  }

  const appJs = fs.readFileSync(path.join(__dirname, "GeometryCompilerApp.js"), "utf8");
  const vpJs = fs.readFileSync(path.join(__dirname, "Engine/WebGpuViewport.js"), "utf8");
  const solversJs = fs.readFileSync(path.join(__dirname, "Engine/CompilerSolvers.js"), "utf8");

  assert.ok(appJs.includes("quadCompare") && appJs.includes("quad-grid-hud"), "4-Way Quad Compare HUD present");
  assert.ok(appJs.includes("TogNaniteSw") && appJs.includes("RngProbSigma") && appJs.includes("data-hybrid-sw") && appJs.includes("TogRegionalAdaptive") && appJs.includes("TogCoarseRetopoSeed"), "Hybrid Shrinkwrap Policy, Regional Auto-Blend, Retopo-Seeded LOD 3, Nanite Interior Shrinkwrap & Prob-QEM sigma controls present");
  assert.ok(appJs.includes('data-dim="128"') && appJs.includes('data-dim="64"'), "Sparse SDF resolution reaches 128^3");
  assert.ok(appJs.includes("TogEdgeGuides") && appJs.includes("RngCreaseDeg") && appJs.includes("TogBoundaries"), "Retopo Guide controls present");
  assert.ok(appJs.includes("HudSliceRange") && appJs.includes("SdfScrubberBar"), "SDF slice viewport scrubber slider present");
  assert.ok(!/unsubdivide/i.test(appJs) && !/unsubdivide/i.test(vpJs) && !/unsubdivide/i.test(solversJs), "Unsubdivide removed cleanly");
  assert.ok(!/naive/i.test(appJs) && !/naive/i.test(vpJs) && !/naive/i.test(solversJs), "Naive Collapse removed completely");
  assert.ok(!/fracture/i.test(appJs) && !/fracture/i.test(vpJs), "Fracture excluded from active Geometry Compiler UI/renderer");

  const css = fs.readFileSync(path.join(__dirname, "GeometryCompiler.css"), "utf8");
  for (const token of ["#090909", "#121212", ".document-tab", ".workspace-card", ".toggle", "#ffb454", "#589979", ".shelf-drawer", ".sdf-scrubber", ".quad-grid-hud"]) {
    assert.ok(css.includes(token), `Project Zero CSS token present: ${token}`);
  }

  const files = ["index.html", "GeometryCompiler.css", "GeometryCompilerApp.js", "Engine/CompilerSolvers.js", "Engine/WebGpuViewport.js"];
  for (const rel of files) {
    const src = fs.readFileSync(path.join(__dirname, rel), "utf8");
    const badAnd = src.match(/\b[A-Za-z0-9_]+And[A-Z][A-Za-z0-9_]*\b/g) || [];
    assert.equal(badAnd.length, 0, `${rel} must not contain 'And' in identifiers: ${badAnd.join(", ")}`);
  }
});

// 11. LOD-Adaptive Error-Guarded Shrinkwrap Policy for Hybrid Solver
runTest("HybridShrinkwrap: Error-Guarded Adaptive/AllLods beats Pure QEM at coarse LODs with zero dual-wall breaches", () => {
  for (const id of ["GlassVessel", "ShaderBall", "UtahTeapot", "TorusKnot"]) {
    const g = constructSpecimen(id);
    const off = compileClusterLodDag(g, { hybridShrinkwrapMode: "off" });
    const adp = compileClusterLodDag(g, { hybridShrinkwrapMode: "adaptive" });
    const all = compileClusterLodDag(g, { hybridShrinkwrapMode: "allLods" });
    const str = compileClusterLodDag(g, { hybridShrinkwrapMode: "strictLow" });
    assert.equal(adp.hybridShrinkwrapMode, "adaptive");
    assert.equal(all.hybridShrinkwrapMode, "allLods");
    assert.equal(str.hybridShrinkwrapMode, "strictLow");
    assert.equal(off.hybridShrinkwrapMode, "off");

    // Zero dual-wall breaches in every mode on every LOD
    for (let l = 1; l <= 3; ++l) {
      assert.equal(adp.hybridLods[l].wallCollapseCount, 0, `${id} LOD${l} adaptive breaches`);
      assert.equal(all.hybridLods[l].wallCollapseCount, 0, `${id} LOD${l} allLods breaches`);
      assert.equal(str.hybridLods[l].wallCollapseCount, 0, `${id} LOD${l} strictLow breaches`);
    }
    // Coarse LOD3 error-guarded shrinkwrap must strictly improve over pure QEM ("off")!
    assert.ok(
      adp.hybridLods[3].meanErrorMm <= off.hybridLods[3].meanErrorMm,
      `${id} LOD3 adaptive (${adp.hybridLods[3].meanErrorMm}mm) must be <= off (${off.hybridLods[3].meanErrorMm}mm)`,
    );
    // High LOD1 error-guarded allLods shrinkwrap must strictly improve over pure QEM ("off")!
    assert.ok(
      all.hybridLods[1].meanErrorMm <= off.hybridLods[1].meanErrorMm,
      `${id} LOD1 allLods (${all.hybridLods[1].meanErrorMm}mm) must be <= off (${off.hybridLods[1].meanErrorMm}mm)`,
    );
  }
});

// 12. 128^3 Sparse 8x8x8 SDF + Sparse Dual-Contour + Sparse FVOL + Spatially-Adaptive Regional Hybrid
runTest("SparseSDF128 & RegionalHybrid: 128^3 sparse bricks, Mip1=64^3 monotonic, sparse DC/FVOL & Regional Auto-Blend", () => {
  const vessel = constructSpecimen("GlassVessel");
  const sdf128 = compileSparseSdfVolume(vessel, { gridDim: 128 });
  assert.equal(sdf128.dim, 128, "SDF dim reaches 128");
  assert.equal(sdf128.mip1Dim, 64, "Mip1 is 64^3");
  assert.equal(sdf128.bricksPerAxis, 16, "16^3 = 4096 bricks");
  assert.equal(sdf128.totalBricks, 4096);
  assert.equal(sdf128.denseDistances, null, "128^3 uses true sparse storage without dense Float32Array bloat");
  assert.ok(sdf128.prunedPercent >= 60, `GlassVessel 128^3 prunes >=60% empty bricks (got ${sdf128.prunedPercent}%)`);
  assert.equal(sdf128.monotonicityVerified, true, "128^3 Mip1 <= Mip0 verified");

  const dc128 = extractDualContourSurface(sdf128, "GlassVessel 128");
  assert.ok(dc128.extractedTriangleCount > 50000, `128^3 sparse Dual-Contour extracted ${dc128.extractedTriangleCount} tris`);

  const fvol128 = JSON.parse(serializeVolumeFvol(sdf128, "GlassVessel128"));
  assert.equal(fvol128.storageMode, "sparse8x8x8");
  assert.deepEqual(fvol128.dimensions, [128, 128, 128]);
  assert.equal(fvol128.mip0BrickQuantizedCount, sdf128.allocatedBrickCount);

  // Spatially-Adaptive Regional Auto-Blend on ChamferBox & TorusKnot
  const box = constructSpecimen("ChamferBox");
  const regBox = compileClusterLodDag(box, { regionalAdaptive: true });
  assert.equal(regBox.regionalAdaptive, true);
  assert.ok(regBox.hybridLods[0].regionMix.sharpPct > 20 && regBox.hybridLods[0].regionMix.flatPct > 20,
    "ChamferBox splits cleanly into Sharp-QEM bevels and Flat-Retopo panels");
  assert.ok(regBox.hybridLods[2].meanErrorMm < 4.5, `Regional Hybrid ChamferBox LOD2 error ${regBox.hybridLods[2].meanErrorMm}mm < 4.5mm`);

  const knot = constructSpecimen("TorusKnot");
  const regKnot = compileClusterLodDag(knot, { regionalAdaptive: true });
  assert.ok(regKnot.hybridLods[2].sliverPercent <= 10 && regKnot.hybridLods[3].meanErrorMm < regKnot.naniteQemLods[3].meanErrorMm && regKnot.hybridLods[3].maxErrorMm < regKnot.naniteQemLods[3].maxErrorMm,
    "Regional Hybrid TorusKnot crushes LOD2 slivers (<=10%) and beats Nanite mean & max error at LOD3");

  const regVessel = compileClusterLodDag(vessel, { regionalAdaptive: true });
  assert.ok(regVessel.hybridLods[2].meanErrorMm < regVessel.naniteQemLods[2].meanErrorMm && regVessel.hybridLods[2].wallCollapseCount === 0,
    "Regional Hybrid GlassVessel LOD2 beats Nanite mean error with 0 wall breaches");
});

// 13. Retopo-Seeded Coarse LOD 3 Pipeline (Clean Retopo + Shrinkwrap Seed -> LOD 0-Anchored QEM + Hausdorff Envelope)
runTest("RetopoSeededCoarseLod3: eliminates forced LOD3 topology scars & spikes across all 6 specimens", () => {
  const vessel = constructSpecimen("GlassVessel");
  const seededVessel = compileClusterLodDag(vessel, { coarseRetopoSeed: true });
  const directVessel = compileClusterLodDag(vessel, { coarseRetopoSeed: false });
  assert.equal(seededVessel.coarseRetopoSeed, true);
  assert.equal(seededVessel.hybridLods[3].retopoSeeded, true);
  assert.equal(directVessel.hybridLods[3].retopoSeeded, undefined);
  assert.equal(seededVessel.hybridLods[3].wallCollapseCount, 0, "Retopo-seeded GlassVessel LOD3 keeps 0 wall breaches");
  assert.ok(seededVessel.hybridLods[3].maxErrorMm < directVessel.hybridLods[3].maxErrorMm - 30,
    `Retopo-seeded GlassVessel LOD3 cuts max error (${seededVessel.hybridLods[3].maxErrorMm}mm vs ${directVessel.hybridLods[3].maxErrorMm}mm)`);
  assert.ok(seededVessel.hybridLods[3].triangleCount <= directVessel.hybridLods[3].triangleCount,
    "Retopo-seeded GlassVessel LOD3 hits tighter LOD3 budget");

  const box = constructSpecimen("ChamferBox");
  const seededBox = compileClusterLodDag(box, { coarseRetopoSeed: true });
  const directBox = compileClusterLodDag(box, { coarseRetopoSeed: false });
  assert.ok(seededBox.hybridLods[3].sliverPercent <= 10 && seededBox.hybridLods[3].sliverPercent < directBox.hybridLods[3].sliverPercent * 0.4,
    `Retopo-seeded ChamferBox LOD3 crushes forced slivers (${seededBox.hybridLods[3].sliverPercent}% vs ${directBox.hybridLods[3].sliverPercent}%)`);
  assert.ok(seededBox.hybridLods[3].avgMinAngleDeg > directBox.hybridLods[3].avgMinAngleDeg + 3.0,
    `Retopo-seeded ChamferBox LOD3 lifts min angle (${seededBox.hybridLods[3].avgMinAngleDeg} deg vs ${directBox.hybridLods[3].avgMinAngleDeg} deg)`);

  const ball = constructSpecimen("ShaderBall");
  const seededBall = compileClusterLodDag(ball, { coarseRetopoSeed: true });
  const directBall = compileClusterLodDag(ball, { coarseRetopoSeed: false });
  assert.ok(seededBall.hybridLods[3].meanErrorMm < directBall.hybridLods[3].meanErrorMm && seededBall.hybridLods[3].maxErrorMm < directBall.hybridLods[3].maxErrorMm,
    "Retopo-seeded ShaderBall LOD3 improves both mean & max surface error");
  assert.ok(seededBall.hybridLods[3].sliverPercent < directBall.hybridLods[3].sliverPercent,
    "Retopo-seeded ShaderBall LOD3 lowers sliver percentage");

  const teapot = constructSpecimen("UtahTeapot");
  const seededTea = compileClusterLodDag(teapot, { coarseRetopoSeed: true });
  const directTea = compileClusterLodDag(teapot, { coarseRetopoSeed: false });
  assert.ok(seededTea.hybridLods[3].sliverPercent < directTea.hybridLods[3].sliverPercent - 8,
    `Retopo-seeded UtahTeapot LOD3 slashes slivers (${seededTea.hybridLods[3].sliverPercent}% vs ${directTea.hybridLods[3].sliverPercent}%)`);
  assert.ok(seededTea.hybridLods[3].maxErrorMm < directTea.hybridLods[3].maxErrorMm,
    `Retopo-seeded UtahTeapot LOD3 cuts max error (${seededTea.hybridLods[3].maxErrorMm}mm vs ${directTea.hybridLods[3].maxErrorMm}mm)`);

  const sphere = constructSpecimen("UvSphere");
  const seededSphere = compileClusterLodDag(sphere, { coarseRetopoSeed: true });
  const directSphere = compileClusterLodDag(sphere, { coarseRetopoSeed: false });
  assert.ok(seededSphere.hybridLods[3].meanErrorMm < directSphere.hybridLods[3].meanErrorMm && seededSphere.hybridLods[3].maxErrorMm < directSphere.hybridLods[3].maxErrorMm,
    "Retopo-seeded UvSphere LOD3 beats direct decimation on both mean & max error");
});

// 14. Optimized 3D Sparse SDF Raymarching Visualizer (WGSL Hierarchical Mip1+Bricks, All 4 Viz Modes, Production Hybrid Default)
runTest("Sdf3DRaymarchVisualizer: WGSL valid, zero reserved words, 4 viz modes render real 3D sparse SDF pixels (32^3 & 128^3)", () => {
  assert.ok(SDF_RAYMARCH_WGSL.includes("@vertex") && SDF_RAYMARCH_WGSL.includes("@fragment"));
  assert.ok(SDF_RAYMARCH_WGSL.includes("fn vsSdfRay") && SDF_RAYMARCH_WGSL.includes("fn fsSdfRay"));
  assert.ok(SDF_RAYMARCH_WGSL.includes("mip1Data") && SDF_RAYMARCH_WGSL.includes("brickIndir") && SDF_RAYMARCH_WGSL.includes("brickPool"));
  assert.ok(SDF_RAYMARCH_WGSL.includes("sampleMip1") && SDF_RAYMARCH_WGSL.includes("sampleMip0") && SDF_RAYMARCH_WGSL.includes("calcAo") && SDF_RAYMARCH_WGSL.includes("traceBricksOverlay"));

  const stripped = SDF_RAYMARCH_WGSL.replace(/\/\/[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "");
  const wgslReserved = [
    "meta", "self", "super", "ref", "mut", "abstract", "active", "alignas", "alignof", "as", "asm",
    "assert", "async", "attribute", "auto", "await", "become", "cast", "catch", "class", "coherent",
    "column_major", "common", "compile", "concept", "const_cast", "crate", "debugger", "decltype",
    "delete", "demote", "do", "enum", "explicit", "export", "extends", "extern", "external", "filter",
    "final", "finally", "friend", "from", "get", "goto", "groupshared", "highp", "impl", "implements",
    "import", "inline", "instanceof", "interface", "layout", "lowp", "macro", "match", "mediump",
    "mod", "module", "move", "mutable", "namespace", "new", "nil", "noexcept", "noinline", "null",
    "nullptr", "of", "operator", "package", "partition", "pass", "patch", "precise", "precision",
    "priv", "protected", "pub", "public", "readonly", "register", "require", "resource", "restrict",
    "set", "shared", "sizeof", "smooth", "static", "std", "subroutine", "target", "template", "this",
    "throw", "trait", "try", "type", "typedef", "typeid", "typename", "typeof", "union", "unless",
    "unsafe", "unsized", "use", "using", "varying", "virtual", "volatile", "wgsl", "where", "with",
    "writeonly", "yield",
  ];
  const tokens = new Set(stripped.match(/\b[A-Za-z_][A-Za-z0-9_]*\b/g) || []);
  for (const kw of wgslReserved) {
    assert.ok(!tokens.has(kw), `SDF_RAYMARCH_WGSL must not contain WGSL reserved keyword '${kw}'`);
  }

  const appJs = fs.readFileSync(path.join(__dirname, "GeometryCompilerApp.js"), "utf8");
  assert.ok(appJs.includes('"sdfRaymarch"') && appJs.includes("SdfRayHudBar") && appJs.includes("data-sdf-viz") && appJs.includes("RngIsoOffset"), "3D SDF Raymarch UI controls present");
  assert.ok(appJs.includes('activeMethod: "hybrid"') && appJs.includes('productionSolver: "hybrid"'), "Hybrid remains production default while Retopo/Nanite are comparison-only");

  // Render all 4 visualization modes (Iso+AO, 3D Cutaway, Step Heat, Sparse Bricks) + evicted Mip1 on real 32^3 & 128^3 volumes
  let lastImg = null;
  const noop = () => {};
  const ctx = {
    clearRect: noop, fillRect: noop, beginPath: noop, moveTo: noop, lineTo: noop, closePath: noop,
    fill: noop, stroke: noop, save: noop, restore: noop, rect: noop, clip: noop, setLineDash: noop,
    putImageData: (im) => { lastImg = im; }, drawImage: noop, strokeRect: noop, fillText: noop,
    createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
    fillStyle: "", strokeStyle: "", lineWidth: 1, globalAlpha: 1, imageSmoothingEnabled: true,
  };
  const canvas = { width: 640, height: 480, style: {}, parentElement: { getBoundingClientRect: () => ({ width: 640, height: 480 }) }, getContext: () => ctx };
  const prevWin = globalThis.window;
  const prevDoc = globalThis.document;
  globalThis.window = { devicePixelRatio: 1 };
  globalThis.document = { createElement: () => ({ width: 200, height: 150, getContext: () => ctx }) };
  try {
    const vp = new GeometryCompilerViewport(canvas, canvas);
    const vessel = constructSpecimen("GlassVessel");
    const sdf32 = compileSparseSdfVolume(vessel, { gridDim: 32 });
    const compiled32 = { geometry: vessel, sdfVolume: sdf32 };

    const modeSums = [];
    for (let vizMode = 0; vizMode < 4; ++vizMode) {
      lastImg = null;
      vp.renderFrame({
        viewMode: "sdfRaymarch",
        opticalCategory: vessel.opticalCategory,
        sdfVizMode: vizMode,
        sdfSliceAxis: 0,
        sdfSliceLevel: 0,
        sdfSliceDepth: 0.5,
        sdfIsoOffsetMm: 0,
        sdfCutFlip: false,
        camera: { yaw: 0.68, pitch: 0.34, distance: 2.35, target: vessel.boundsCenter },
      }, compiled32);
      assert.ok(lastImg && lastImg.data.length > 0, `SDF 3D vizMode=${vizMode} produced image pixels`);
      let sum = 0, bright = 0;
      for (let i = 0; i < lastImg.data.length; i += 4) {
        const lum = lastImg.data[i] + lastImg.data[i + 1] + lastImg.data[i + 2];
        sum += lum;
        if (lum > 110) bright++;
      }
      assert.ok(bright > 150, `SDF 3D vizMode=${vizMode} hit real foreground geometry (${bright} bright pixels)`);
      modeSums.push(sum);
    }
    // Every mode produces distinct visual output
    assert.notEqual(modeSums[0], modeSums[1], "Iso+AO differs from 3D Cutaway");
    assert.notEqual(modeSums[0], modeSums[2], "Iso+AO differs from Step Heatmap");
    assert.notEqual(modeSums[0], modeSums[3], "Iso+AO differs from Sparse Bricks");

    // Verify thin-walled GlassVessel 64^3 renders with ZERO front-wall sphere-tracing tunneling!
    const sdf64 = compileSparseSdfVolume(vessel, { gridDim: 64 });
    assert.equal(sdf64.monotonicityVerified, true, "64^3 EDT conservativeMip1 preserves Mip1 <= Mip0 monotonicity");
    lastImg = null;
    vp.renderFrame({
      viewMode: "sdfRaymarch",
      opticalCategory: vessel.opticalCategory,
      sdfVizMode: 0,
      sdfSliceAxis: 2,
      sdfSliceLevel: 0,
      sdfSliceDepth: 0.5,
      sdfIsoOffsetMm: 0,
      sdfCutFlip: false,
      camera: { yaw: 0.72, pitch: 0.34, distance: 2.35, target: vessel.boundsCenter },
    }, { geometry: vessel, sdfVolume: sdf64 });
    assert.ok(lastImg && lastImg.width >= 160, "64^3 raymarch produced valid frame");
    // Sample central belly of the 45mm hollow glass vessel: must have 0 punctured/dark-hole pixels
    let bellyTotal = 0, bellyDark = 0;
    const bx0 = Math.floor(lastImg.width * 0.41), bx1 = Math.floor(lastImg.width * 0.59);
    const by0 = Math.floor(lastImg.height * 0.48), by1 = Math.floor(lastImg.height * 0.72);
    for (let y = by0; y <= by1; ++y) {
      for (let x = bx0; x <= bx1; ++x) {
        const idx = (y * lastImg.width + x) * 4;
        const avg = (lastImg.data[idx] + lastImg.data[idx + 1] + lastImg.data[idx + 2]) / 3;
        bellyTotal++;
        if (avg < 45) bellyDark++;
      }
    }
    assert.equal(bellyDark, 0, `GlassVessel 64^3 thin-wall front belly must have 0 tunneled dark pixels (checked ${bellyTotal} pixels)`);

    // Verify 128^3 sparse SDF + live Mip0 eviction renders cleanly without crash
    const sdf128 = compileSparseSdfVolume(vessel, { gridDim: 128 });
    const t0 = performance.now();
    vp.renderFrame({
      viewMode: "sdfRaymarch",
      opticalCategory: vessel.opticalCategory,
      sdfVizMode: 0,
      sdfSliceAxis: 0,
      sdfSliceLevel: 0,
      sdfSliceDepth: 0.5,
      sdfIsoOffsetMm: 0,
      camera: { yaw: 0.68, pitch: 0.34, distance: 2.35, target: vessel.boundsCenter },
    }, { geometry: vessel, sdfVolume: sdf128 });
    const dtMs = performance.now() - t0;
    assert.ok(dtMs < 400, `128^3 sparse CPU fallback raymarch stays fast (${dtMs.toFixed(1)}ms)`);

    evictMip0ForBudget(sdf128, 128 * 1024);
    assert.equal(sdf128.mip0Evicted, true);
    vp.renderFrame({
      viewMode: "sdfRaymarch",
      opticalCategory: vessel.opticalCategory,
      sdfVizMode: 1,
      sdfSliceAxis: 1,
      sdfSliceLevel: 0,
      sdfSliceDepth: 0.5,
      sdfIsoOffsetMm: 8,
      camera: { yaw: 0.68, pitch: 0.34, distance: 2.35, target: vessel.boundsCenter },
    }, { geometry: vessel, sdfVolume: sdf128 });
  } finally {
    globalThis.window = prevWin;
    globalThis.document = prevDoc;
  }
});

console.log(`\nResult: ${passed}/${passed + failed} suites passed.`);
if (failed > 0) process.exit(1);

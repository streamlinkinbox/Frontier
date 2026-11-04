/*==========================================================================================================================================
                                                          GEOMETRYANDBAKERS.JS
==========================================================================================================================================*/
/* 🧩 Multi-part High/Low study & imported geometry, per-map parameterized 2D UV baker (up to 16K), and 3D Float32 Volume SDF baker. */

(function (global) {
  "use strict";

  const MESH_MAP_CATALOG = [
    {
      id: "volumeSdf",
      label: "Volume SDF",
      short: "3D distance grid",
      group: "Volume & Distance",
      swatch: "sdf",
      isVolume: true,
      description: "3D Float32 signed or unsigned Euclidean distance volume with 3D cutting planes and 2D slice inspection."
    },
    // Normals
    {
      id: "normalTangent",
      label: "Tangent normal",
      short: "Tangent-space RGB",
      group: "Normals",
      swatch: "normal",
      description: "Projects high-poly surface relief into the low target UV tangent basis."
    },
    {
      id: "normalWorld",
      label: "World normal",
      short: "Scene XYZ vector",
      group: "Normals",
      swatch: "normal-world",
      description: "Encodes world-space surface normals into RGB [0..1]."
    },
    {
      id: "normalObject",
      label: "Object normal",
      short: "Local XYZ vector",
      group: "Normals",
      swatch: "normal-object",
      description: "Encodes object-local normals independent of scene placement."
    },
    {
      id: "bentNormal",
      label: "Bent tangent normal",
      short: "Unoccluded cone",
      group: "Normals",
      swatch: "normal",
      description: "Average unoccluded hemisphere direction in tangent space for indirect specular/diffuse."
    },
    {
      id: "bevelNormal",
      label: "Bevel normal",
      short: "Rounded chamfer",
      group: "Normals",
      swatch: "normal",
      description: "Neighborhood-filtered tangent normal that softens hard mechanical edges."
    },
    {
      id: "vertexNormal",
      label: "Vertex normal",
      short: "Smooth low normal",
      group: "Normals",
      swatch: "normal-world",
      description: "Interpolated low-mesh shading normals encoded to RGB."
    },
    {
      id: "facetedNormal",
      label: "Faceted normal",
      short: "Flat face normal",
      group: "Normals",
      swatch: "normal-object",
      description: "Geometric flat triangle normals showing polygon faceting."
    },
    // Surface
    {
      id: "ao",
      label: "Ambient occlusion",
      short: "Hemisphere sky",
      group: "Surface",
      swatch: "ao",
      description: "Cosine-weighted hemisphere sky visibility across high-poly crevices."
    },
    {
      id: "secondaryAo",
      label: "Secondary AO",
      short: "Tight contact AO",
      group: "Surface",
      swatch: "ao",
      description: "Short-range crevice occlusion for fine mechanical seams."
    },
    {
      id: "curvature",
      label: "Curvature",
      short: "Signed ridge/valley",
      group: "Surface",
      swatch: "curvature",
      description: "Signed mean curvature centered at neutral gray (ridges bright, cavities dark)."
    },
    {
      id: "convexity",
      label: "Convexity",
      short: "Positive ridges",
      group: "Surface",
      swatch: "curvature",
      description: "Isolates exposed convex ridges as a grayscale highlight mask."
    },
    {
      id: "concavity",
      label: "Cavity / concavity",
      short: "Recessed grooves",
      group: "Surface",
      swatch: "ao",
      description: "Isolates recessed flutes, seams, and pockets."
    },
    {
      id: "thickness",
      label: "Thickness",
      short: "Interior depth",
      group: "Surface",
      swatch: "height",
      description: "Inward ray-traced cross-section depth for SSS and translucency."
    },
    {
      id: "height",
      label: "Projection height",
      short: "Signed relief",
      group: "Surface",
      swatch: "height",
      description: "Signed scalar displacement between high-detail features and base surface."
    },
    {
      id: "vectorDisplacement",
      label: "Vector displacement",
      short: "XYZ offset",
      group: "Surface",
      swatch: "position",
      description: "3D world-space offset vector encoded around 0.5 midgray."
    },
    // Coordinates & IDs
    {
      id: "position",
      label: "Position",
      short: "Normalized XYZ",
      group: "Coordinates & IDs",
      swatch: "position",
      description: "Normalized bounding-box or unit-sphere XYZ coordinates."
    },
    {
      id: "objectId",
      label: "Object ID",
      short: "Sub-mesh palette",
      group: "Coordinates & IDs",
      swatch: "id",
      description: "Distinct color per sub-mesh from the Outliner for fast mask selection."
    },
    {
      id: "uvIslandId",
      label: "UV island ID",
      short: "Per-chart color",
      group: "Coordinates & IDs",
      swatch: "id",
      description: "Distinct color per UV island/chart."
    },
    {
      id: "uvCoords",
      label: "UV coordinates",
      short: "RG UV ramp",
      group: "Coordinates & IDs",
      swatch: "position",
      description: "Direct U (red) and V (green) coordinate diagnostic map."
    },
    {
      id: "wireframe",
      label: "UV wireframe",
      short: "Triangle UV edges",
      group: "Coordinates & IDs",
      swatch: "wireframe",
      description: "Anti-aliased UV chart edges with configurable pixel stroke width."
    },
    // Masks
    {
      id: "coverage",
      label: "UV coverage",
      short: "Chart occupancy",
      group: "Masks",
      swatch: "coverage",
      description: "Binary occupancy mask of covered UV texels vs empty gutter."
    },
    {
      id: "hitMask",
      label: "Alpha / hit mask",
      short: "Projection hits",
      group: "Masks",
      swatch: "coverage",
      description: "White on valid projected surface texels, black/transparent on misses."
    },
    {
      id: "bevelMask",
      label: "Bevel mask",
      short: "Chamfer mask",
      group: "Masks",
      swatch: "curvature",
      description: "Grayscale angular deviation mask where bevel rounding occurs."
    },
    {
      id: "dustMask",
      label: "Dust mask",
      short: "Upward deposition",
      group: "Masks",
      swatch: "dust",
      description: "World-up alignment weighted by ambient sky exposure."
    },
    {
      id: "dirtMask",
      label: "Dirt mask",
      short: "Crevice grime",
      group: "Masks",
      swatch: "ao",
      description: "Recessed occlusion + concavity mask for accumulated grime."
    },
    {
      id: "edgeWear",
      label: "Edge wear mask",
      short: "Exposed abrasion",
      group: "Masks",
      swatch: "curvature",
      description: "Convexity weighted by open exposure for chipped edges."
    },
    // Color
    {
      id: "vertexColor",
      label: "Vertex color",
      short: "Source RGB",
      group: "Color",
      swatch: "color",
      description: "Transfers interpolated high-mesh vertex RGB color attributes."
    }
  ];

  // Authentic per-map Inspector property schemas!
  const MAP_PROPERTY_SCHEMAS = {
    normalTangent: [
      { key: "normalHandedness", label: "Green channel (Y)", type: "select", options: [["opengl", "OpenGL (+Y)"], ["directx", "DirectX (-Y)"]], default: "opengl" },
      { key: "tangentBasis", label: "Tangent basis", type: "select", options: [["mikk", "MikkTSpace"], ["uv", "Smooth UV"]], default: "mikk" },
      { key: "reliefStrength", label: "Relief intensity", type: "range", min: 0.2, max: 3.0, step: 0.05, default: 1.35 },
      { key: "dither8bit", label: "8-bit gradient dither", type: "toggle", default: false }
    ],
    normalWorld: [
      { key: "axisSwizzle", label: "World axis up", type: "select", options: [["yup", "Y-Up (XYZ)"], ["zup", "Z-Up (XZY)"]], default: "yup" },
      { key: "invertNormals", label: "Flip normal orientation", type: "toggle", default: false }
    ],
    normalObject: [
      { key: "axisSwizzle", label: "Local axis up", type: "select", options: [["yup", "Y-Up (XYZ)"], ["zup", "Z-Up (XZY)"]], default: "yup" },
      { key: "invertNormals", label: "Flip normal orientation", type: "toggle", default: false }
    ],
    bentNormal: [
      { key: "normalHandedness", label: "Green channel (Y)", type: "select", options: [["opengl", "OpenGL (+Y)"], ["directx", "DirectX (-Y)"]], default: "opengl" },
      { key: "aoRays", label: "Hemisphere rays", type: "select", options: [["32", "32 rays"], ["64", "64 rays"], ["128", "128 rays"], ["256", "256 rays"]], default: "64" },
      { key: "bentCone", label: "Cone spread", type: "range", min: 0.25, max: 1.8, step: 0.05, default: 1.0 },
      { key: "aoDistance", label: "Occlusion reach", type: "range", min: 0.1, max: 2.0, step: 0.05, default: 0.85 }
    ],
    bevelNormal: [
      { key: "normalHandedness", label: "Green channel (Y)", type: "select", options: [["opengl", "OpenGL (+Y)"], ["directx", "DirectX (-Y)"]], default: "opengl" },
      { key: "bevelSamples", label: "Filter samples", type: "select", options: [["8", "8 taps"], ["16", "16 taps"], ["32", "32 taps"]], default: "16" },
      { key: "bevelRadius", label: "Bevel width", type: "range", min: 0.2, max: 2.5, step: 0.05, default: 1.15 },
      { key: "bevelStrength", label: "Edge roundness", type: "range", min: 0.2, max: 2.8, step: 0.05, default: 1.45 }
    ],
    vertexNormal: [
      { key: "axisSwizzle", label: "Axis convention", type: "select", options: [["yup", "Y-Up (XYZ)"], ["zup", "Z-Up (XZY)"]], default: "yup" },
      { key: "invertNormals", label: "Flip orientation", type: "toggle", default: false }
    ],
    facetedNormal: [
      { key: "axisSwizzle", label: "Axis convention", type: "select", options: [["yup", "Y-Up (XYZ)"], ["zup", "Z-Up (XZY)"]], default: "yup" },
      { key: "invertNormals", label: "Flip orientation", type: "toggle", default: false }
    ],
    ao: [
      { key: "aoRays", label: "Hemisphere rays", type: "select", options: [["32", "32 rays"], ["64", "64 rays"], ["128", "128 rays"], ["256", "256 rays"]], default: "64" },
      { key: "aoDistribution", label: "Distribution", type: "select", options: [["cosine", "Cosine-weighted"], ["uniform", "Uniform dome"]], default: "cosine" },
      { key: "aoStrength", label: "Occlusion intensity", type: "range", min: 0.2, max: 2.4, step: 0.05, default: 1.05 },
      { key: "aoGamma", label: "Falloff gamma", type: "range", min: 0.4, max: 2.4, step: 0.05, default: 1.0 },
      { key: "ignoreBackfaces", label: "Ignore backfaces", type: "toggle", default: true }
    ],
    secondaryAo: [
      { key: "aoRays", label: "Crevice rays", type: "select", options: [["32", "32 rays"], ["64", "64 rays"], ["128", "128 rays"]], default: "64" },
      { key: "aoStrength", label: "Contact intensity", type: "range", min: 0.4, max: 2.8, step: 0.05, default: 1.45 },
      { key: "aoGamma", label: "Tightness power", type: "range", min: 0.6, max: 3.0, step: 0.05, default: 1.65 }
    ],
    curvature: [
      { key: "curvAlgorithm", label: "Solver", type: "select", options: [["neighbor", "Mesh neighborhood"], ["sharp", "High-pass ridge"]], default: "neighbor" },
      { key: "curvatureBoost", label: "Edge contrast", type: "range", min: 0.3, max: 3.2, step: 0.05, default: 1.35 },
      { key: "curvNeutral", label: "Flat midpoint", type: "range", min: 0.3, max: 0.7, step: 0.02, default: 0.5 },
      { key: "invertMask", label: "Invert ridge / valley", type: "toggle", default: false }
    ],
    convexity: [
      { key: "curvatureBoost", label: "Ridge gain", type: "range", min: 0.4, max: 3.5, step: 0.05, default: 1.5 },
      { key: "maskThreshold", label: "Cutoff floor", type: "range", min: 0.0, max: 0.55, step: 0.02, default: 0.04 },
      { key: "invertMask", label: "Invert mask", type: "toggle", default: false }
    ],
    concavity: [
      { key: "curvatureBoost", label: "Cavity gain", type: "range", min: 0.4, max: 3.5, step: 0.05, default: 1.5 },
      { key: "maskThreshold", label: "Pocket threshold", type: "range", min: 0.0, max: 0.55, step: 0.02, default: 0.04 },
      { key: "invertMask", label: "Invert mask", type: "toggle", default: false }
    ],
    thickness: [
      { key: "aoRays", label: "Inward cone rays", type: "select", options: [["16", "16 rays"], ["32", "32 rays"], ["64", "64 rays"], ["128", "128 rays"]], default: "32" },
      { key: "thicknessScale", label: "Thickness range", type: "range", min: 0.25, max: 2.5, step: 0.05, default: 1.0 },
      { key: "aoGamma", label: "Translucency curve", type: "range", min: 0.4, max: 2.4, step: 0.05, default: 1.0 },
      { key: "invertMask", label: "Invert (thin = bright)", type: "toggle", default: false }
    ],
    height: [
      { key: "heightGain", label: "Relief range scale", type: "range", min: 2.0, max: 24.0, step: 0.5, default: 10.5 },
      { key: "curvNeutral", label: "Zero reference", type: "range", min: 0.0, max: 0.8, step: 0.05, default: 0.5 },
      { key: "invertMask", label: "Invert height", type: "toggle", default: false }
    ],
    vectorDisplacement: [
      { key: "axisSwizzle", label: "Vector space", type: "select", options: [["yup", "World XYZ (Y-Up)"], ["zup", "World XZY (Z-Up)"]], default: "yup" },
      { key: "heightGain", label: "Displacement gain", type: "range", min: 2.0, max: 22.0, step: 0.5, default: 9.5 }
    ],
    position: [
      { key: "posNormalization", label: "Normalization", type: "select", options: [["bbox", "Bounding box [0..1]"], ["sphere", "Bounding sphere"]], default: "bbox" },
      { key: "axisSwizzle", label: "Axis order", type: "select", options: [["yup", "XYZ (Y-Up)"], ["zup", "XZY (Z-Up)"]], default: "yup" }
    ],
    objectId: [
      { key: "idPalette", label: "Color palette", type: "select", options: [["studio", "Studio distinct"], ["pastel", "Soft pastel"], ["vivid", "High-contrast vivid"]], default: "studio" },
      { key: "idSeed", label: "Color hash seed", type: "range", min: 0, max: 32, step: 1, default: 0 }
    ],
    uvIslandId: [
      { key: "idPalette", label: "Chart palette", type: "select", options: [["studio", "Studio distinct"], ["pastel", "Soft pastel"], ["vivid", "High-contrast vivid"]], default: "studio" },
      { key: "idSeed", label: "Chart hash seed", type: "range", min: 0, max: 32, step: 1, default: 0 }
    ],
    uvCoords: [
      { key: "uvFlipV", label: "Flip V coordinate", type: "toggle", default: false },
      { key: "uvBlue", label: "Blue channel value", type: "range", min: 0.0, max: 1.0, step: 0.05, default: 0.14 }
    ],
    wireframe: [
      { key: "wireWidth", label: "Edge width", type: "range", min: 0.4, max: 4.0, step: 0.1, default: 1.2 },
      { key: "wireBrightness", label: "Stroke brightness", type: "range", min: 0.3, max: 1.0, step: 0.05, default: 0.9 }
    ],
    coverage: [
      { key: "invertMask", label: "Invert gutter / chart", type: "toggle", default: false }
    ],
    hitMask: [
      { key: "transparentMiss", label: "Transparent gutter alpha", type: "toggle", default: true },
      { key: "invertMask", label: "Invert mask", type: "toggle", default: false }
    ],
    bevelMask: [
      { key: "bevelStrength", label: "Angle sensitivity", type: "range", min: 0.4, max: 3.0, step: 0.05, default: 1.4 },
      { key: "maskThreshold", label: "Noise floor", type: "range", min: 0.0, max: 0.45, step: 0.02, default: 0.02 }
    ],
    dustMask: [
      { key: "dustAxis", label: "Deposition up axis", type: "select", options: [["+y", "+Y (Top)"], ["+z", "+Z (Front)"], ["-y", "-Y (Underside)"]], default: "+y" },
      { key: "dustSlope", label: "Slope falloff", type: "range", min: 0.4, max: 3.5, step: 0.05, default: 1.6 },
      { key: "aoStrength", label: "Crevice shielding", type: "range", min: 0.0, max: 2.0, step: 0.05, default: 1.0 }
    ],
    dirtMask: [
      { key: "aoStrength", label: "Occlusion grime", type: "range", min: 0.2, max: 2.5, step: 0.05, default: 1.0 },
      { key: "curvatureBoost", label: "Cavity weight", type: "range", min: 0.0, max: 2.5, step: 0.05, default: 0.85 },
      { key: "maskThreshold", label: "Clean surface floor", type: "range", min: 0.0, max: 0.45, step: 0.02, default: 0.0 }
    ],
    edgeWear: [
      { key: "curvatureBoost", label: "Edge sharpness", type: "range", min: 0.4, max: 3.5, step: 0.05, default: 1.35 },
      { key: "aoStrength", label: "Exposure shielding", type: "range", min: 0.0, max: 2.0, step: 0.05, default: 1.0 },
      { key: "maskThreshold", label: "Wear threshold", type: "range", min: 0.0, max: 0.5, step: 0.02, default: 0.04 }
    ],
    vertexColor: [
      { key: "colorGamma", label: "Transfer gamma", type: "range", min: 0.45, max: 2.2, step: 0.05, default: 1.0 }
    ]
  };

  function clamp(v, lo, hi) {
    return v < lo ? lo : v > hi ? hi : v;
  }

  function smoothstep(e0, e1, x) {
    const t = clamp((x - e0) / (e1 - e0 || 1e-6), 0, 1);
    return t * t * (3 - 2 * t);
  }

  function normalize3(x, y, z) {
    const len = Math.hypot(x, y, z) || 1;
    return [x / len, y / len, z / len];
  }

  function computeBounds(positions) {
    if (!positions || positions.length < 3) {
      return { min: [-1, -1, -1], max: [1, 1, 1], size: [2, 2, 2], diag: 3.464 };
    }
    const min = [Infinity, Infinity, Infinity];
    const max = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < positions.length; i += 3) {
      const x = positions[i], y = positions[i + 1], z = positions[i + 2];
      if (x < min[0]) min[0] = x;
      if (y < min[1]) min[1] = y;
      if (z < min[2]) min[2] = z;
      if (x > max[0]) max[0] = x;
      if (y > max[1]) max[1] = y;
      if (z > max[2]) max[2] = z;
    }
    const size = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    const diag = Math.hypot(size[0], size[1], size[2]) || 1;
    return { min, max, size, diag };
  }

  function computeSmoothNormals(positions, indices) {
    const normals = new Float32Array(positions.length);
    const keyMap = new Map();
    const vertToCanon = new Int32Array(positions.length / 3);
    for (let v = 0; v < vertToCanon.length; v++) {
      const qx = Math.round(positions[v * 3] * 1e5);
      const qy = Math.round(positions[v * 3 + 1] * 1e5);
      const qz = Math.round(positions[v * 3 + 2] * 1e5);
      const key = qx + "," + qy + "," + qz;
      let canon = keyMap.get(key);
      if (canon === undefined) {
        canon = v;
        keyMap.set(key, v);
      }
      vertToCanon[v] = canon;
    }
    const accum = new Float32Array(positions.length);
    for (let t = 0; t < indices.length; t += 3) {
      const ia = indices[t] * 3, ib = indices[t + 1] * 3, ic = indices[t + 2] * 3;
      const abx = positions[ib] - positions[ia];
      const aby = positions[ib + 1] - positions[ia + 1];
      const abz = positions[ib + 2] - positions[ia + 2];
      const acx = positions[ic] - positions[ia];
      const acy = positions[ic + 1] - positions[ia + 1];
      const acz = positions[ic + 2] - positions[ia + 2];
      const nx = aby * acz - abz * acy;
      const ny = abz * acx - abx * acz;
      const nz = abx * acy - aby * acx;
      const ca = vertToCanon[indices[t]] * 3;
      const cb = vertToCanon[indices[t + 1]] * 3;
      const cc = vertToCanon[indices[t + 2]] * 3;
      accum[ca] += nx; accum[ca + 1] += ny; accum[ca + 2] += nz;
      accum[cb] += nx; accum[cb + 1] += ny; accum[cb + 2] += nz;
      accum[cc] += nx; accum[cc + 1] += ny; accum[cc + 2] += nz;
    }
    for (let v = 0; v < vertToCanon.length; v++) {
      const c = vertToCanon[v] * 3;
      const [nx, ny, nz] = normalize3(accum[c], accum[c + 1], accum[c + 2]);
      normals[v * 3] = nx;
      normals[v * 3 + 1] = ny;
      normals[v * 3 + 2] = nz;
    }
    return normals;
  }

  /* ----------------------------------------------------------------------------------------------------------------------------------------
   * Multi-Part Sub-Mesh Splitter & Merger for Dual High / Low Outliner
   * -------------------------------------------------------------------------------------------------------------------------------------- */

  function splitMeshIntoParts(fullMesh, partNames) {
    const triCount = fullMesh.indices.length / 3;
    const buckets = new Map();

    for (let t = 0; t < triCount; t++) {
      const i0 = fullMesh.indices[t * 3];
      const pid = Math.round(fullMesh.partIds[i0] || 0);
      if (!buckets.has(pid)) buckets.set(pid, []);
      buckets.get(pid).push(t);
    }

    const parts = [];
    let idxCounter = 0;
    for (const [pid, triList] of [...buckets.entries()].sort((a, b) => a[0] - b[0])) {
      const remap = new Map();
      const pos = [], nrm = [], uv = [], col = [], chart = [], rel = [], ind = [];

      for (let k = 0; k < triList.length; k++) {
        const t = triList[k];
        for (let c = 0; c < 3; c++) {
          const oldV = fullMesh.indices[t * 3 + c];
          let newV = remap.get(oldV);
          if (newV === undefined) {
            newV = pos.length / 3;
            remap.set(oldV, newV);
            pos.push(fullMesh.positions[oldV * 3], fullMesh.positions[oldV * 3 + 1], fullMesh.positions[oldV * 3 + 2]);
            nrm.push(fullMesh.normals[oldV * 3], fullMesh.normals[oldV * 3 + 1], fullMesh.normals[oldV * 3 + 2]);
            uv.push(fullMesh.uvs[oldV * 2], fullMesh.uvs[oldV * 2 + 1]);
            col.push(fullMesh.colors[oldV * 3], fullMesh.colors[oldV * 3 + 1], fullMesh.colors[oldV * 3 + 2]);
            chart.push(fullMesh.chartIds[oldV]);
            rel.push(fullMesh.relief[oldV]);
          }
          ind.push(newV);
        }
      }

      const positions = new Float32Array(pos);
      const indices = new Uint32Array(ind);
      parts.push({
        id: `part-${idxCounter}-${pid}`,
        name: partNames[idxCounter] || `${fullMesh.name}_${idxCounter + 1}`,
        enabled: true,
        partIndex: idxCounter,
        triangles: indices.length / 3,
        positions,
        normals: new Float32Array(nrm),
        uvs: new Float32Array(uv),
        colors: new Float32Array(col),
        chartIds: new Float32Array(chart),
        partIds: new Float32Array(positions.length / 3).fill(idxCounter),
        relief: new Float32Array(rel),
        indices,
        bounds: computeBounds(positions)
      });
      idxCounter++;
    }
    return parts;
  }

  function mergeActiveParts(parts, fallbackLabel) {
    const active = (parts || []).filter((p) => p.enabled);
    if (active.length === 0) {
      throw new Error(`Enable at least one sub-mesh in the ${fallbackLabel} outliner.`);
    }
    let totalVerts = 0, totalIdx = 0;
    for (const p of active) {
      totalVerts += p.positions.length / 3;
      totalIdx += p.indices.length;
    }
    const positions = new Float32Array(totalVerts * 3);
    const normals = new Float32Array(totalVerts * 3);
    const uvs = new Float32Array(totalVerts * 2);
    const colors = new Float32Array(totalVerts * 3);
    const chartIds = new Float32Array(totalVerts);
    const partIds = new Float32Array(totalVerts);
    const relief = new Float32Array(totalVerts);
    const indices = new Uint32Array(totalIdx);

    let vOff = 0, iOff = 0;
    active.forEach((p, slotIdx) => {
      const vCount = p.positions.length / 3;
      positions.set(p.positions, vOff * 3);
      normals.set(p.normals, vOff * 3);
      uvs.set(p.uvs, vOff * 2);
      colors.set(p.colors, vOff * 3);
      chartIds.set(p.chartIds, vOff);
      partIds.fill(p.partIndex ?? slotIdx, vOff, vOff + vCount);
      relief.set(p.relief, vOff);
      for (let i = 0; i < p.indices.length; i++) {
        indices[iOff + i] = p.indices[i] + vOff;
      }
      vOff += vCount;
      iOff += p.indices.length;
    });

    return {
      name: fallbackLabel,
      positions,
      normals,
      uvs,
      colors,
      chartIds,
      partIds,
      relief,
      indices,
      bounds: computeBounds(positions)
    };
  }

  /* ----------------------------------------------------------------------------------------------------------------------------------------
   * Watertight Study Meshes (Multi-Part Low & High Outliner Hierarchies)
   * -------------------------------------------------------------------------------------------------------------------------------------- */

  function buildTorusMesh(majorSegs, minorSegs, sculpted) {
    const R = 0.72, rBase = 0.30;
    const cols = majorSegs + 1, rows = minorSegs + 1;
    const vCount = cols * rows;
    const positions = new Float32Array(vCount * 3);
    const uvs = new Float32Array(vCount * 2);
    const colors = new Float32Array(vCount * 3);
    const chartIds = new Float32Array(vCount);
    const partIds = new Float32Array(vCount);
    const relief = new Float32Array(vCount);
    const indices = new Uint32Array(majorSegs * minorSegs * 6);

    let vi = 0;
    for (let i = 0; i <= majorSegs; i++) {
      const u = i / majorSegs;
      const phi = (i === majorSegs ? 0 : u) * Math.PI * 2;
      const cp = Math.cos(phi), sp = Math.sin(phi);
      for (let j = 0; j <= minorSegs; j++) {
        const v = j / minorSegs;
        const theta = (j === minorSegs ? 0 : v) * Math.PI * 2;
        const ct = Math.cos(theta), st = Math.sin(theta);

        const collar = Math.cos(phi * 6);
        const part = collar > 0.55 ? 1 : collar < -0.65 ? 2 : 0;
        let dr = 0;
        if (sculpted) {
          const flutes = Math.cos(theta * 8) * 0.022 * smoothstep(-0.2, 0.5, collar);
          const ringBand = collar > 0.55 ? 0.028 : collar < -0.65 ? -0.018 : 0;
          dr = flutes + ringBand;
        }

        const r = rBase + dr;
        positions[vi * 3] = (R + r * ct) * cp;
        positions[vi * 3 + 1] = r * st;
        positions[vi * 3 + 2] = (R + r * ct) * sp;

        uvs[vi * 2] = 0.04 + u * 0.92;
        uvs[vi * 2 + 1] = 0.04 + v * 0.92;
        chartIds[vi] = (u < 0.5 ? 1 : 2) + (v < 0.5 ? 0 : 2);
        partIds[vi] = part;
        relief[vi] = dr;

        colors[vi * 3] = part === 1 ? 0.82 : part === 2 ? 0.44 : 0.66;
        colors[vi * 3 + 1] = part === 1 ? 0.68 : part === 2 ? 0.62 : 0.72;
        colors[vi * 3 + 2] = part === 1 ? 0.45 : part === 2 ? 0.76 : 0.64;
        vi++;
      }
    }

    let ti = 0;
    for (let i = 0; i < majorSegs; i++) {
      for (let j = 0; j < minorSegs; j++) {
        const a = i * rows + j, b = (i + 1) * rows + j, c = (i + 1) * rows + (j + 1), d = i * rows + (j + 1);
        indices[ti++] = a; indices[ti++] = b; indices[ti++] = c;
        indices[ti++] = a; indices[ti++] = c; indices[ti++] = d;
      }
    }
    return {
      name: "Toroidal Valve",
      positions,
      normals: computeSmoothNormals(positions, indices),
      uvs, colors, chartIds, partIds, relief, indices,
      bounds: computeBounds(positions)
    };
  }

  function buildClosedSphereDeformed(uSegs, vSegs, mode, sculpted) {
    const cols = uSegs + 1;
    const positions = [], uvs = [], colors = [], chartIds = [], partIds = [], relief = [], indices = [];

    for (let j = 0; j <= vSegs; j++) {
      const v = j / vSegs;
      const theta = v * Math.PI;
      const st = Math.sin(theta), ct = Math.cos(theta);
      for (let i = 0; i <= uSegs; i++) {
        const u = i / uSegs;
        const phi = (i === uSegs ? 0 : u) * Math.PI * 2;
        const cp = Math.cos(phi), sp = Math.sin(phi);

        let nx = st * cp, ny = ct, nz = st * sp;
        if (j === 0) { nx = 0; ny = 1; nz = 0; }
        if (j === vSegs) { nx = 0; ny = -1; nz = 0; }

        let x = nx, y = ny, z = nz, h = 0, part = 0, chart = 1;

        if (mode === "Cube") {
          const power = sculpted ? 0.34 : 0.22;
          x = Math.sign(nx) * Math.pow(Math.abs(nx), power) * 0.78;
          y = Math.sign(ny) * Math.pow(Math.abs(ny), power) * 0.78;
          z = Math.sign(nz) * Math.pow(Math.abs(nz), power) * 0.78;
          if (sculpted) {
            const panelInset = (Math.abs(nx) > 0.72 || Math.abs(ny) > 0.72 || Math.abs(nz) > 0.72) ? -0.026 : 0.012;
            const grooves = Math.sin(nx * 14) * Math.sin(nz * 14) * 0.012;
            h = panelInset + grooves;
            x += nx * h; y += ny * h; z += nz * h;
          }
          const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
          if (ay >= ax && ay >= az) { chart = ny > 0 ? 1 : 2; part = 1; }
          else if (ax >= ay && ax >= az) { chart = nx > 0 ? 3 : 4; part = 0; }
          else { chart = nz > 0 ? 5 : 6; part = 2; }
        } else if (mode === "Teapot") {
          const profile = 0.76 + 0.22 * Math.sin(v * Math.PI * 1.15) - 0.14 * Math.cos(v * Math.PI * 2);
          const lidCollar = Math.exp(-Math.pow((v - 0.22) / 0.045, 2)) * 0.075;
          const footRing = Math.exp(-Math.pow((v - 0.88) / 0.05, 2)) * 0.055;
          const spoutLobe = st * st * Math.exp(-Math.pow(Math.sin((phi - 0.55) * 0.5) / 0.18, 2)) * Math.exp(-Math.pow((v - 0.45) / 0.16, 2)) * 0.34;
          const handleLobe = st * st * Math.exp(-Math.pow(Math.sin((phi - (Math.PI + 0.55)) * 0.5) / 0.21, 2)) * Math.exp(-Math.pow((v - 0.50) / 0.22, 2)) * 0.28;
          let flutes = 0;
          if (sculpted) {
            flutes = Math.cos(phi * 16) * 0.022 * smoothstep(0.26, 0.40, v) * (1 - smoothstep(0.74, 0.85, v));
            flutes += lidCollar * 0.45;
          }
          const rad = profile + lidCollar + footRing + spoutLobe + handleLobe + flutes;
          x = st * cp * rad;
          y = ct * 0.78 + spoutLobe * 0.28;
          z = st * sp * rad;
          h = flutes + lidCollar * 0.5;
          part = v < 0.27 ? 1 : (spoutLobe > 0.06 || handleLobe > 0.06 ? 2 : 0);
          chart = v < 0.25 ? 1 : v > 0.82 ? 2 : u < 0.5 ? 3 : 4;
        } else {
          let rad = 0.88;
          if (sculpted) {
            const ribs = Math.cos(phi * 12) * Math.sin(theta * 6) * 0.036;
            const eqBand = Math.exp(-Math.pow((v - 0.5) / 0.06, 2)) * 0.032;
            h = ribs + eqBand;
            rad += h;
          }
          x = nx * rad; y = ny * rad; z = nz * rad;
          part = Math.abs(v - 0.5) < 0.12 ? 1 : v < 0.22 || v > 0.78 ? 2 : 0;
          chart = (u < 0.5 ? 1 : 2) + (v < 0.5 ? 0 : 2);
        }

        positions.push(x, y, z);
        uvs.push(0.04 + u * 0.92, 0.04 + (1 - v) * 0.92);
        chartIds.push(chart);
        partIds.push(part);
        relief.push(h);
        colors.push(
          part === 1 ? 0.84 : part === 2 ? 0.48 : 0.65,
          part === 1 ? 0.69 : part === 2 ? 0.76 : 0.71,
          part === 1 ? 0.49 : part === 2 ? 0.82 : 0.62
        );
      }
    }

    for (let j = 0; j < vSegs; j++) {
      for (let i = 0; i < uSegs; i++) {
        const a = j * cols + i, b = j * cols + (i + 1), c = (j + 1) * cols + (i + 1), d = (j + 1) * cols + i;
        if (j > 0) indices.push(a, d, b);
        if (j < vSegs - 1) indices.push(b, d, c);
      }
    }

    const posArr = new Float32Array(positions);
    const idxArr = new Uint32Array(indices);
    return {
      name: mode === "Teapot" ? "Teapot Vessel" : mode === "Cube" ? "Beveled Cube" : "Fluted Sphere",
      positions: posArr,
      normals: computeSmoothNormals(posArr, idxArr),
      uvs: new Float32Array(uvs),
      colors: new Float32Array(colors),
      chartIds: new Float32Array(chartIds),
      partIds: new Float32Array(partIds),
      relief: new Float32Array(relief),
      indices: idxArr,
      bounds: computeBounds(posArr)
    };
  }

  function createStudyAsset(shape) {
    let lowFull, highFull, label, baseNames;
    if (shape === "Torus") {
      label = "Toroidal Valve";
      lowFull = buildTorusMesh(36, 22, false);
      highFull = buildTorusMesh(88, 52, true);
      baseNames = ["valve_housing", "valve_collars", "valve_recess"];
    } else if (shape === "Cube") {
      label = "Beveled Cube";
      lowFull = buildClosedSphereDeformed(32, 20, "Cube", false);
      highFull = buildClosedSphereDeformed(76, 52, "Cube", true);
      baseNames = ["chassis_sides", "top_bottom_caps", "front_back_plates"];
    } else if (shape === "Sphere") {
      label = "Fluted Sphere";
      lowFull = buildClosedSphereDeformed(32, 20, "Sphere", false);
      highFull = buildClosedSphereDeformed(76, 52, "Sphere", true);
      baseNames = ["orb_rib_shell", "equator_band", "polar_crowns"];
    } else {
      label = "Teapot Vessel";
      lowFull = buildClosedSphereDeformed(38, 24, "Teapot", false);
      highFull = buildClosedSphereDeformed(84, 56, "Teapot", true);
      baseNames = ["teapot_body", "teapot_lid_collar", "teapot_spout_handle"];
    }

    const lowParts = splitMeshIntoParts(lowFull, baseNames.map((n) => `${n}_low`));
    const highParts = splitMeshIntoParts(highFull, baseNames.map((n) => `${n}_high`));
    return {
      id: shape,
      label,
      lowParts,
      highParts,
      get low() {
        return mergeActiveParts(this.lowParts, `${this.label} (Low)`);
      },
      get high() {
        return mergeActiveParts(this.highParts, `${this.label} (High)`);
      }
    };
  }

  /* ----------------------------------------------------------------------------------------------------------------------------------------
   * Multi-Object OBJ & STL Parsers -> Outliner Part Nodes
   * -------------------------------------------------------------------------------------------------------------------------------------- */

  function autoGenerateUvs(positions) {
    const b = computeBounds(positions);
    const cx = (b.min[0] + b.max[0]) * 0.5;
    const cy = (b.min[1] + b.max[1]) * 0.5;
    const cz = (b.min[2] + b.max[2]) * 0.5;
    const vCount = positions.length / 3;
    const uvs = new Float32Array(vCount * 2);
    for (let i = 0; i < vCount; i++) {
      const dx = positions[i * 3] - cx, dy = positions[i * 3 + 1] - cy, dz = positions[i * 3 + 2] - cz;
      const r = Math.hypot(dx, dy, dz) || 1;
      const u = 0.5 + Math.atan2(dz, dx) / (Math.PI * 2);
      const v = 0.5 + Math.asin(clamp(dy / r, -1, 1)) / Math.PI;
      uvs[i * 2] = 0.04 + u * 0.92;
      uvs[i * 2 + 1] = 0.04 + v * 0.92;
    }
    return uvs;
  }

  function parseObjToParts(text, fallbackName) {
    const vp = [], vt = [];
    const groups = [];
    let cur = { name: fallbackName || "mesh_part", pos: [], uv: [] };

    const lines = text.split(/\r?\n/);
    for (let l = 0; l < lines.length; l++) {
      const line = lines[l].trim();
      if (!line || line[0] === "#") continue;
      const parts = line.split(/\s+/);
      const head = parts[0];
      if (head === "o" || head === "g") {
        if (cur.pos.length >= 9) groups.push(cur);
        cur = { name: parts.slice(1).join("_") || `${fallbackName}_${groups.length + 1}`, pos: [], uv: [] };
      } else if (head === "v") {
        vp.push(parseFloat(parts[1]) || 0, parseFloat(parts[2]) || 0, parseFloat(parts[3]) || 0);
      } else if (head === "vt") {
        vt.push(parseFloat(parts[1]) || 0, parseFloat(parts[2]) || 0);
      } else if (head === "f" && parts.length >= 4) {
        const face = [];
        for (let k = 1; k < parts.length; k++) {
          const tok = parts[k].split("/");
          let pi = parseInt(tok[0], 10);
          let ti = tok[1] ? parseInt(tok[1], 10) : 0;
          if (pi < 0) pi = vp.length / 3 + pi + 1;
          if (ti < 0) ti = vt.length / 2 + ti + 1;
          face.push([pi - 1, ti - 1]);
        }
        for (let k = 1; k < face.length - 1; k++) {
          for (const [pi, ti] of [face[0], face[k], face[k + 1]]) {
            cur.pos.push(vp[pi * 3] || 0, vp[pi * 3 + 1] || 0, vp[pi * 3 + 2] || 0);
            if (ti >= 0 && ti * 2 + 1 < vt.length) {
              cur.uv.push(clamp(vt[ti * 2], 0, 1), clamp(vt[ti * 2 + 1], 0, 1));
            }
          }
        }
      }
    }
    if (cur.pos.length >= 9) groups.push(cur);
    if (!groups.length) throw new Error("OBJ file contains no valid triangles.");

    return groups.map((g, idx) => {
      const positions = new Float32Array(g.pos);
      const vCount = positions.length / 3;
      const indices = Uint32Array.from({ length: vCount }, (_, i) => i);
      const uvs = g.uv.length === vCount * 2 ? new Float32Array(g.uv) : autoGenerateUvs(positions);
      return {
        id: `imp-${Date.now()}-${idx}`,
        name: g.name,
        enabled: true,
        partIndex: idx,
        triangles: vCount / 3,
        positions,
        normals: computeSmoothNormals(positions, indices),
        uvs,
        colors: new Float32Array(vCount * 3).fill(0.74),
        chartIds: new Float32Array(vCount).fill(idx + 1),
        partIds: new Float32Array(vCount).fill(idx),
        relief: new Float32Array(vCount),
        indices,
        bounds: computeBounds(positions)
      };
    });
  }

  function parseStlToParts(buffer, name) {
    const view = new DataView(buffer);
    let positions;
    if (buffer.byteLength > 84) {
      const triCount = view.getUint32(80, true);
      if (84 + triCount * 50 === buffer.byteLength && triCount > 0) {
        positions = new Float32Array(triCount * 9);
        let off = 84;
        for (let t = 0; t < triCount; t++) {
          off += 12;
          for (let k = 0; k < 9; k++) {
            positions[t * 9 + k] = view.getFloat32(off, true);
            off += 4;
          }
          off += 2;
        }
      }
    }
    if (!positions) {
      const text = new TextDecoder().decode(buffer);
      const out = [];
      const re = /vertex\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)/gi;
      let m;
      while ((m = re.exec(text)) !== null) {
        out.push(parseFloat(m[1]) || 0, parseFloat(m[2]) || 0, parseFloat(m[3]) || 0);
      }
      if (out.length < 9) throw new Error("STL file contains no valid triangles.");
      positions = new Float32Array(out);
    }
    const vCount = positions.length / 3;
    const indices = Uint32Array.from({ length: vCount }, (_, i) => i);
    return [
      {
        id: `imp-${Date.now()}-0`,
        name: name || "stl_mesh",
        enabled: true,
        partIndex: 0,
        triangles: vCount / 3,
        positions,
        normals: computeSmoothNormals(positions, indices),
        uvs: autoGenerateUvs(positions),
        colors: new Float32Array(vCount * 3).fill(0.74),
        chartIds: new Float32Array(vCount).fill(1),
        partIds: new Float32Array(vCount).fill(0),
        relief: new Float32Array(vCount),
        indices,
        bounds: computeBounds(positions)
      }
    ];
  }

  /* ----------------------------------------------------------------------------------------------------------------------------------------
   * Mesh Topology Audit + Non-Cubic [Nx, Ny, Nz] Float32 3D Volume SDF Baker
   * -------------------------------------------------------------------------------------------------------------------------------------- */

  function auditTriangleMesh(mesh) {
    const { positions, indices } = mesh;
    const vertCanon = new Map();
    const vMap = new Int32Array(positions.length / 3);
    for (let i = 0; i < vMap.length; i++) {
      const key =
        Math.round(positions[i * 3] * 1e5) + "," +
        Math.round(positions[i * 3 + 1] * 1e5) + "," +
        Math.round(positions[i * 3 + 2] * 1e5);
      let c = vertCanon.get(key);
      if (c === undefined) {
        c = vertCanon.size;
        vertCanon.set(key, c);
      }
      vMap[i] = c;
    }

    const edgeCounts = new Map();
    let degenerateTriangles = 0;
    const triCount = indices.length / 3;
    for (let t = 0; t < indices.length; t += 3) {
      const a = vMap[indices[t]], b = vMap[indices[t + 1]], c = vMap[indices[t + 2]];
      if (a === b || b === c || c === a) {
        degenerateTriangles++;
        continue;
      }
      for (const [p0, p1] of [[a, b], [b, c], [c, a]]) {
        const k = Math.min(p0, p1) + ":" + Math.max(p0, p1);
        edgeCounts.set(k, (edgeCounts.get(k) || 0) + 1);
      }
    }

    let boundaryEdges = 0, nonManifoldEdges = 0;
    for (const c of edgeCounts.values()) {
      if (c === 1) boundaryEdges++;
      else if (c > 2) nonManifoldEdges++;
    }
    return {
      triangles: triCount,
      uniqueVertices: vertCanon.size,
      boundaryEdges,
      nonManifoldEdges,
      degenerateTriangles,
      closedManifold: triCount > 0 && boundaryEdges === 0 && nonManifoldEdges === 0
    };
  }

  function closestPointTriangleSq(px, py, pz, t, tris) {
    const o = t * 18;
    const ax = tris[o], ay = tris[o + 1], az = tris[o + 2];
    const abx = tris[o + 9], aby = tris[o + 10], abz = tris[o + 11];
    const acx = tris[o + 12], acy = tris[o + 13], acz = tris[o + 14];

    const apx = px - ax, apy = py - ay, apz = pz - az;
    const d1 = abx * apx + aby * apy + abz * apz;
    const d2 = acx * apx + acy * apy + acz * apz;
    if (d1 <= 0 && d2 <= 0) return apx * apx + apy * apy + apz * apz;

    const bx = tris[o + 3], by = tris[o + 4], bz = tris[o + 5];
    const bpx = px - bx, bpy = py - by, bpz = pz - bz;
    const d3 = abx * bpx + aby * bpy + abz * bpz;
    const d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0 && d4 <= d3) return bpx * bpx + bpy * bpy + bpz * bpz;

    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
      const v = d1 / (d1 - d3);
      const dx = px - (ax + v * abx), dy = py - (ay + v * aby), dz = pz - (az + v * abz);
      return dx * dx + dy * dy + dz * dz;
    }

    const cx = tris[o + 6], cy = tris[o + 7], cz = tris[o + 8];
    const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
    const d5 = abx * cpx + aby * cpy + abz * cpz;
    const d6 = acx * cpx + acy * cpy + acz * cpz;
    if (d6 >= 0 && d5 <= d6) return cpx * cpx + cpy * cpy + cpz * cpz;

    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      const w = d2 / (d2 - d6);
      const dx = px - (ax + w * acx), dy = py - (ay + w * acy), dz = pz - (az + w * acz);
      return dx * dx + dy * dy + dz * dz;
    }

    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
      const bcx = cx - bx, bcy = cy - by, bcz = cz - bz;
      const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      const dx = px - (bx + w * bcx), dy = py - (by + w * bcy), dz = pz - (bz + w * bcz);
      return dx * dx + dy * dy + dz * dz;
    }

    const nx = tris[o + 15], ny = tris[o + 16], nz = tris[o + 17];
    const dist = apx * nx + apy * ny + apz * nz;
    return dist * dist;
  }

  function edt1d(f, n, stepSq, out, v, z) {
    let k = 0;
    v[0] = 0;
    z[0] = -Infinity;
    z[1] = Infinity;
    for (let q = 1; q < n; q++) {
      const fq = f[q];
      if (fq >= 1e18) continue;
      if (f[v[k]] >= 1e18) {
        v[0] = q;
        continue;
      }
      let s;
      while (true) {
        const vk = v[k];
        s = ((fq + q * q * stepSq) - (f[vk] + vk * vk * stepSq)) / (2 * (q - vk) * stepSq);
        if (s > z[k]) break;
        k--;
      }
      k++;
      v[k] = q;
      z[k] = s;
      z[k + 1] = Infinity;
    }
    if (f[v[0]] >= 1e18) {
      for (let q = 0; q < n; q++) out[q] = 1e18;
      return;
    }
    k = 0;
    for (let q = 0; q < n; q++) {
      while (z[k + 1] < q) k++;
      const dx = q - v[k];
      out[q] = dx * dx * stepSq + f[v[k]];
    }
  }

  function collectAxisHits(o1, o2, axis, bucketList, tris) {
    const j1 = 1.37e-6, j2 = -2.11e-6;
    const ox = axis === 0 ? -1e5 : o1 + j1;
    const oy = axis === 1 ? -1e5 : axis === 0 ? o1 + j2 : o2 + j2;
    const oz = axis === 2 ? -1e5 : o2 + j1;
    const dx = axis === 0 ? 1 : 0, dy = axis === 1 ? 1 : 0, dz = axis === 2 ? 1 : 0;
    const hits = [];

    for (let i = 0; i < bucketList.length; i++) {
      const o = bucketList[i] * 18;
      const ax = tris[o], ay = tris[o + 1], az = tris[o + 2];
      const e1x = tris[o + 9], e1y = tris[o + 10], e1z = tris[o + 11];
      const e2x = tris[o + 12], e2y = tris[o + 13], e2z = tris[o + 14];

      const hx = dy * e2z - dz * e2y, hy = dz * e2x - dx * e2z, hz = dx * e2y - dy * e2x;
      const det = e1x * hx + e1y * hy + e1z * hz;
      if (det > -1e-9 && det < 1e-9) continue;

      const invDet = 1 / det;
      const sx = ox - ax, sy = oy - ay, sz = oz - az;
      const u = (sx * hx + sy * hy + sz * hz) * invDet;
      if (u < 0 || u > 1) continue;

      const qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
      const v = (dx * qx + dy * qy + dz * qz) * invDet;
      if (v < 0 || u + v > 1) continue;

      const t = (e2x * qx + e2y * qy + e2z * qz) * invDet;
      hits.push(-1e5 + t);
    }
    if (hits.length > 1) hits.sort((a, b) => a - b);
    return hits;
  }

  function parityFromSortedHits(coord, hits) {
    let count = 0;
    for (let i = hits.length - 1; i >= 0; i--) {
      if (hits[i] > coord + 1e-7) count++;
      else break;
    }
    return (count & 1) === 1;
  }

  async function bakeVolumeSdf(mesh, options, onProgress) {
    const t0 = performance.now();
    const nx = clamp(Math.round(options.nx || options.resolution || 48), 8, 256);
    const ny = clamp(Math.round(options.ny || options.resolution || 48), 8, 256);
    const nz = clamp(Math.round(options.nz || options.resolution || 48), 8, 256);
    const maxDim = Math.max(nx, ny, nz);
    const padding = clamp(Number(options.padding ?? 0.14), 0.02, 0.5);
    const signMode = options.signMode || "signed";
    const narrowBand = Number(options.narrowBand || 0);

    const audit = auditTriangleMesh(mesh);
    const effectiveSignMode = signMode === "signed" && !audit.closedManifold ? "approximate" : signMode;

    const { positions, indices, bounds } = mesh;
    const padWorld = bounds.diag * padding;
    const min = [bounds.min[0] - padWorld, bounds.min[1] - padWorld, bounds.min[2] - padWorld];
    const max = [bounds.max[0] + padWorld, bounds.max[1] + padWorld, bounds.max[2] + padWorld];
    const span = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    const step = [span[0] / nx, span[1] / ny, span[2] / nz];

    const triCount = indices.length / 3;
    const tris = new Float32Array(triCount * 18);
    const gDim = clamp(Math.round(maxDim * 0.45), 8, 32);
    const rayBucketsX = Array.from({ length: gDim * gDim }, () => []);
    const rayBucketsY = Array.from({ length: gDim * gDim }, () => []);
    const rayBucketsZ = Array.from({ length: gDim * gDim }, () => []);

    for (let t = 0; t < triCount; t++) {
      const ia = indices[t * 3] * 3, ib = indices[t * 3 + 1] * 3, ic = indices[t * 3 + 2] * 3;
      const ax = positions[ia], ay = positions[ia + 1], az = positions[ia + 2];
      const bx = positions[ib], by = positions[ib + 1], bz = positions[ib + 2];
      const cx = positions[ic], cy = positions[ic + 1], cz = positions[ic + 2];
      const abx = bx - ax, aby = by - ay, abz = bz - az;
      const acx = cx - ax, acy = cy - ay, acz = cz - az;
      const [tnx, tny, tnz] = normalize3(aby * acz - abz * acy, abz * acx - abx * acz, abx * acy - aby * acx);
      const o = t * 18;
      tris[o] = ax; tris[o + 1] = ay; tris[o + 2] = az;
      tris[o + 3] = bx; tris[o + 4] = by; tris[o + 5] = bz;
      tris[o + 6] = cx; tris[o + 7] = cy; tris[o + 8] = cz;
      tris[o + 9] = abx; tris[o + 10] = aby; tris[o + 11] = abz;
      tris[o + 12] = acx; tris[o + 13] = acy; tris[o + 14] = acz;
      tris[o + 15] = tnx; tris[o + 16] = tny; tris[o + 17] = tnz;

      const x0 = clamp(Math.floor(((Math.min(ax, bx, cx) - min[0]) / span[0]) * gDim), 0, gDim - 1);
      const x1 = clamp(Math.floor(((Math.max(ax, bx, cx) - min[0]) / span[0]) * gDim), 0, gDim - 1);
      const y0 = clamp(Math.floor(((Math.min(ay, by, cy) - min[1]) / span[1]) * gDim), 0, gDim - 1);
      const y1 = clamp(Math.floor(((Math.max(ay, by, cy) - min[1]) / span[1]) * gDim), 0, gDim - 1);
      const z0 = clamp(Math.floor(((Math.min(az, bz, cz) - min[2]) / span[2]) * gDim), 0, gDim - 1);
      const z1 = clamp(Math.floor(((Math.max(az, bz, cz) - min[2]) / span[2]) * gDim), 0, gDim - 1);

      for (let gz = z0; gz <= z1; gz++) {
        for (let gy = y0; gy <= y1; gy++) rayBucketsX[gz * gDim + gy].push(t);
        for (let gx = x0; gx <= x1; gx++) rayBucketsY[gz * gDim + gx].push(t);
      }
      for (let gy = y0; gy <= y1; gy++) {
        for (let gx = x0; gx <= x1; gx++) rayBucketsZ[gy * gDim + gx].push(t);
      }
    }

    const total = nx * ny * nz;
    const distSq = new Float64Array(total).fill(1e20);

    for (let t = 0; t < triCount; t++) {
      const o = t * 18;
      const ax = tris[o], ay = tris[o + 1], az = tris[o + 2];
      const bx = tris[o + 3], by = tris[o + 4], bz = tris[o + 5];
      const cx = tris[o + 6], cy = tris[o + 7], cz = tris[o + 8];

      const x0 = clamp(Math.floor((Math.min(ax, bx, cx) - min[0]) / step[0]), 0, nx - 1);
      const x1 = clamp(Math.ceil((Math.max(ax, bx, cx) - min[0]) / step[0]), 0, nx - 1);
      const y0 = clamp(Math.floor((Math.min(ay, by, cy) - min[1]) / step[1]), 0, ny - 1);
      const y1 = clamp(Math.ceil((Math.max(ay, by, cy) - min[1]) / step[1]), 0, ny - 1);
      const z0 = clamp(Math.floor((Math.min(az, bz, cz) - min[2]) / step[2]), 0, nz - 1);
      const z1 = clamp(Math.ceil((Math.max(az, bz, cz) - min[2]) / step[2]), 0, nz - 1);

      for (let vz = z0; vz <= z1; vz++) {
        const pz = min[2] + (vz + 0.5) * step[2];
        for (let vy = y0; vy <= y1; vy++) {
          const py = min[1] + (vy + 0.5) * step[1];
          const rowOff = (vz * ny + vy) * nx;
          for (let vx = x0; vx <= x1; vx++) {
            const px = min[0] + (vx + 0.5) * step[0];
            const d2 = closestPointTriangleSq(px, py, pz, t, tris);
            if (d2 < distSq[rowOff + vx]) distSq[rowOff + vx] = d2;
          }
        }
      }
    }

    if (onProgress) {
      onProgress(0.35);
      await new Promise((r) => setTimeout(r, 0));
    }

    const fBuf = new Float64Array(maxDim);
    const outBuf = new Float64Array(maxDim);
    const vBuf = new Int32Array(maxDim + 1);
    const zBuf = new Float64Array(maxDim + 2);
    const sx2 = step[0] * step[0], sy2 = step[1] * step[1], sz2 = step[2] * step[2];

    for (let z = 0; z < nz; z++) {
      for (let y = 0; y < ny; y++) {
        const base = (z * ny + y) * nx;
        for (let x = 0; x < nx; x++) fBuf[x] = distSq[base + x];
        edt1d(fBuf, nx, sx2, outBuf, vBuf, zBuf);
        for (let x = 0; x < nx; x++) distSq[base + x] = outBuf[x];
      }
    }
    for (let z = 0; z < nz; z++) {
      for (let x = 0; x < nx; x++) {
        for (let y = 0; y < ny; y++) fBuf[y] = distSq[(z * ny + y) * nx + x];
        edt1d(fBuf, ny, sy2, outBuf, vBuf, zBuf);
        for (let y = 0; y < ny; y++) distSq[(z * ny + y) * nx + x] = outBuf[y];
      }
    }
    for (let y = 0; y < ny; y++) {
      for (let x = 0; x < nx; x++) {
        for (let z = 0; z < nz; z++) fBuf[z] = distSq[(z * ny + y) * nx + x];
        edt1d(fBuf, nz, sz2, outBuf, vBuf, zBuf);
        for (let z = 0; z < nz; z++) distSq[(z * ny + y) * nx + x] = outBuf[z];
      }
    }

    if (onProgress) {
      onProgress(0.72);
      await new Promise((r) => setTimeout(r, 0));
    }

    const hitsX = Array.from({ length: nz * ny }, () => null);
    const hitsY = Array.from({ length: nz * nx }, () => null);
    const hitsZ = Array.from({ length: ny * nx }, () => null);

    if (effectiveSignMode !== "unsigned") {
      for (let z = 0; z < nz; z++) {
        const pz = min[2] + (z + 0.5) * step[2];
        const gz = clamp(Math.floor(((pz - min[2]) / span[2]) * gDim), 0, gDim - 1);
        for (let y = 0; y < ny; y++) {
          const py = min[1] + (y + 0.5) * step[1];
          if (py >= bounds.min[1] && py <= bounds.max[1] && pz >= bounds.min[2] && pz <= bounds.max[2]) {
            const gy = clamp(Math.floor(((py - min[1]) / span[1]) * gDim), 0, gDim - 1);
            hitsX[z * ny + y] = collectAxisHits(py, pz, 0, rayBucketsX[gz * gDim + gy], tris);
          }
        }
        for (let x = 0; x < nx; x++) {
          const px = min[0] + (x + 0.5) * step[0];
          if (px >= bounds.min[0] && px <= bounds.max[0] && pz >= bounds.min[2] && pz <= bounds.max[2]) {
            const gx = clamp(Math.floor(((px - min[0]) / span[0]) * gDim), 0, gDim - 1);
            hitsY[z * nx + x] = collectAxisHits(px, pz, 1, rayBucketsY[gz * gDim + gx], tris);
          }
        }
      }
      for (let y = 0; y < ny; y++) {
        const py = min[1] + (y + 0.5) * step[1];
        const gy = clamp(Math.floor(((py - min[1]) / span[1]) * gDim), 0, gDim - 1);
        for (let x = 0; x < nx; x++) {
          const px = min[0] + (x + 0.5) * step[0];
          if (px >= bounds.min[0] && px <= bounds.max[0] && py >= bounds.min[1] && py <= bounds.max[1]) {
            const gx = clamp(Math.floor(((px - min[0]) / span[0]) * gDim), 0, gDim - 1);
            hitsZ[y * nx + x] = collectAxisHits(px, py, 2, rayBucketsZ[gy * gDim + gx], tris);
          }
        }
      }
    }

    const distances = new Float32Array(total);
    const confidence = new Float32Array(total);
    let minDist = Infinity, maxDist = -Infinity, insideCount = 0;

    for (let z = 0; z < nz; z++) {
      const pz = min[2] + (z + 0.5) * step[2];
      for (let y = 0; y < ny; y++) {
        const py = min[1] + (y + 0.5) * step[1];
        const hx = hitsX[z * ny + y];
        for (let x = 0; x < nx; x++) {
          const idx = (z * ny + y) * nx + x;
          const px = min[0] + (x + 0.5) * step[0];
          let d = Math.sqrt(distSq[idx]);
          let conf = 1.0;

          if (effectiveSignMode !== "unsigned" && hx) {
            const hy = hitsY[z * nx + x];
            if (hy && px >= bounds.min[0] && px <= bounds.max[0]) {
              const inX = parityFromSortedHits(px, hx);
              const inY = parityFromSortedHits(py, hy);
              let inside;
              if (inX === inY) inside = inX;
              else {
                const hz = hitsZ[y * nx + x];
                inside = hz ? parityFromSortedHits(pz, hz) : inX;
                conf = 0.66;
              }
              if (inside) {
                d = -d;
                insideCount++;
              }
            }
          }

          if (narrowBand > 0) d = clamp(d, -narrowBand, narrowBand);
          distances[idx] = d;
          confidence[idx] = conf;
          if (d < minDist) minDist = d;
          if (d > maxDist) maxDist = d;
        }
      }
    }

    return {
      kind: "volumeSdf",
      meshName: mesh.name,
      dims: [nx, ny, nz],
      bounds: { min, max, span, step },
      signMode: effectiveSignMode,
      narrowBand,
      padding,
      audit,
      distances,
      confidence,
      minDistance: minDist,
      maxDistance: maxDist,
      insideCount,
      voxelCount: total,
      byteLength: distances.byteLength,
      elapsedMs: Math.round(performance.now() - t0)
    };
  }

  /* ----------------------------------------------------------------------------------------------------------------------------------------
   * 2D SDF Slice & Interactive 3D Volume Slice-Stack / Iso Renderer
   * -------------------------------------------------------------------------------------------------------------------------------------- */

  function sampleSdfColor(d, conf, sdf, displayMode) {
    const maxAbs = Math.max(Math.abs(sdf.minDistance), Math.abs(sdf.maxDistance), 1e-4);
    const voxelDiag = Math.hypot(sdf.bounds.step[0], sdf.bounds.step[1], sdf.bounds.step[2]);

    if (displayMode === "confidence") {
      const c = clamp(conf, 0, 1);
      return [Math.round(235 - c * 155), Math.round(88 + c * 135), Math.round(72 + c * 92), 255];
    }
    if (displayMode === "inside") {
      if (Math.abs(d) <= voxelDiag * 0.68) return [245, 250, 246, 255];
      return d < 0 ? [62, 212, 168, 255] : [16, 18, 21, 255];
    }
    if (displayMode === "normalized") {
      const t = sdf.signMode === "unsigned" ? clamp(d / maxAbs, 0, 1) : clamp(0.5 + d / (2 * maxAbs), 0, 1);
      const g = Math.round(t * 255);
      return [g, g, g, 255];
    }

    const norm = d / maxAbs;
    const bandFreq = displayMode === "iso" ? 18 : 11;
    const ring = Math.abs(norm * bandFreq - Math.round(norm * bandFreq));
    const contour = ring < 0.075 ? 1 : 0;
    const zeroGlow = Math.exp(-Math.pow(Math.abs(d) / (voxelDiag * 0.72), 2));

    let r, g, b;
    if (d < 0) {
      const t = clamp(-norm * 1.45, 0, 1);
      const wave = 0.88 + 0.12 * Math.cos(norm * Math.PI * 2 * bandFreq);
      r = (76 * (1 - t) + 20 * t) * wave;
      g = (212 * (1 - t) + 96 * t) * wave;
      b = (176 * (1 - t) + 104 * t) * wave;
      if (displayMode === "iso" && contour) {
        r += 54; g += 68; b += 60;
      }
    } else {
      const t = clamp(norm * 1.15, 0, 1);
      const near = Math.exp(-t * 3.2);
      const lineBoost = contour ? (displayMode === "iso" ? 36 : 16) * (0.35 + 0.65 * near) : 0;
      r = 13 + 32 * near + lineBoost;
      g = 15 + 38 * near + lineBoost * 1.06;
      b = 18 + 44 * near + lineBoost * 1.12;
    }

    r = r * (1 - zeroGlow) + 245 * zeroGlow;
    g = g * (1 - zeroGlow) + 249 * zeroGlow;
    b = b * (1 - zeroGlow) + 246 * zeroGlow;
    return [clamp(Math.round(r), 0, 255), clamp(Math.round(g), 0, 255), clamp(Math.round(b), 0, 255), 255];
  }

  function getSdfVoxelIndex(nx, ny, nz, axis, sliceIdx, u, v) {
    if (axis === "x") return (v * ny + u) * nx + sliceIdx;
    if (axis === "y") return (v * ny + sliceIdx) * nx + u;
    return (sliceIdx * ny + v) * nx + u;
  }

  function renderSdfSliceToCanvas(canvas, sdf, axis, sliceIndex, displayMode, options = {}) {
    if (!canvas || !sdf) return;
    const [nx, ny, nz] = sdf.dims;
    const maxSlice = (axis === "x" ? nx : axis === "y" ? ny : nz) - 1;
    const sIdx = clamp(Math.round(sliceIndex), 0, maxSlice);
    const wVox = axis === "x" ? ny : nx;
    const hVox = axis === "z" ? ny : nz;

    const scale = options.thumbnail ? Math.max(1, Math.floor(88 / Math.max(wVox, hVox))) : Math.max(3, Math.floor(380 / Math.max(wVox, hVox)));
    const W = wVox * scale, H = hVox * scale;
    if (canvas.width !== W || canvas.height !== H) {
      canvas.width = W;
      canvas.height = H;
    }

    const ctx = canvas.getContext("2d");
    const small = document.createElement("canvas");
    small.width = wVox;
    small.height = hVox;
    const sctx = small.getContext("2d");
    const img = sctx.createImageData(wVox, hVox);
    const data = img.data;

    for (let v = 0; v < hVox; v++) {
      const vv = hVox - 1 - v;
      for (let u = 0; u < wVox; u++) {
        const idx = getSdfVoxelIndex(nx, ny, nz, axis, sIdx, u, vv);
        const rgba = sampleSdfColor(sdf.distances[idx], sdf.confidence[idx], sdf, displayMode);
        const o = (v * wVox + u) * 4;
        data[o] = rgba[0]; data[o + 1] = rgba[1]; data[o + 2] = rgba[2]; data[o + 3] = 255;
      }
    }
    sctx.putImageData(img, 0, 0);
    ctx.imageSmoothingEnabled = !options.pixelated;
    ctx.clearRect(0, 0, W, H);
    ctx.drawImage(small, 0, 0, W, H);

    if (!options.thumbnail && sdf.signMode !== "unsigned") {
      ctx.save();
      ctx.strokeStyle = "#f4f8f5";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let v = 0; v < hVox - 1; v++) {
        const vv0 = hVox - 1 - v, vv1 = hVox - 2 - v;
        for (let u = 0; u < wVox - 1; u++) {
          const d00 = sdf.distances[getSdfVoxelIndex(nx, ny, nz, axis, sIdx, u, vv0)];
          const d10 = sdf.distances[getSdfVoxelIndex(nx, ny, nz, axis, sIdx, u + 1, vv0)];
          const d11 = sdf.distances[getSdfVoxelIndex(nx, ny, nz, axis, sIdx, u + 1, vv1)];
          const d01 = sdf.distances[getSdfVoxelIndex(nx, ny, nz, axis, sIdx, u, vv1)];
          const pts = [];
          if ((d00 < 0) !== (d10 < 0)) pts.push([(u + 0.5 + d00 / (d00 - d10)) * scale, (v + 0.5) * scale]);
          if ((d10 < 0) !== (d11 < 0)) pts.push([(u + 1.5) * scale, (v + 0.5 + d10 / (d10 - d11)) * scale]);
          if ((d01 < 0) !== (d11 < 0)) pts.push([(u + 0.5 + d01 / (d01 - d11)) * scale, (v + 1.5) * scale]);
          if ((d00 < 0) !== (d01 < 0)) pts.push([(u + 0.5) * scale, (v + 0.5 + d00 / (d00 - d01)) * scale]);
          if (pts.length >= 2) {
            ctx.moveTo(pts[0][0], pts[0][1]);
            ctx.lineTo(pts[1][0], pts[1][1]);
          }
        }
      }
      ctx.stroke();
      ctx.restore();
    }
  }

  function probeSdfSlice(sdf, axis, sliceIndex, normX, normY) {
    if (!sdf) return null;
    const [nx, ny, nz] = sdf.dims;
    const maxSlice = (axis === "x" ? nx : axis === "y" ? ny : nz) - 1;
    const sIdx = clamp(Math.round(sliceIndex), 0, maxSlice);
    const wVox = axis === "x" ? ny : nx;
    const hVox = axis === "z" ? ny : nz;
    const u = clamp(Math.floor(normX * wVox), 0, wVox - 1);
    const v = clamp(Math.floor((1 - normY) * hVox), 0, hVox - 1);
    const vx = axis === "x" ? sIdx : u;
    const vy = axis === "x" ? u : axis === "y" ? sIdx : v;
    const vz = axis === "z" ? sIdx : v;
    const idx = (vz * ny + vy) * nx + vx;
    return {
      voxel: [vx, vy, vz],
      world: [
        sdf.bounds.min[0] + (vx + 0.5) * sdf.bounds.step[0],
        sdf.bounds.min[1] + (vy + 0.5) * sdf.bounds.step[1],
        sdf.bounds.min[2] + (vz + 0.5) * sdf.bounds.step[2]
      ],
      distance: sdf.distances[idx]
    };
  }

  function renderSdf3dPreview(canvas, sdf, state) {
    if (!canvas || !sdf) return;
    const rectW = Math.max(280, canvas.clientWidth || 440);
    const rectH = Math.max(240, canvas.clientHeight || 340);
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const W = Math.round(rectW * dpr), H = Math.round(rectH * dpr);
    if (canvas.width !== W || canvas.height !== H) {
      canvas.width = W;
      canvas.height = H;
    }

    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, rectW, rectH);

    const yaw = state.yaw ?? -0.68, pitch = state.pitch ?? 0.44, zoom = state.zoom ?? 1.0;
    const mode3d = state.mode3d || "combo", displayMode = state.displayMode || "bands", activeAxis = state.axis || "z";
    const [nx, ny, nz] = sdf.dims;

    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    const radius = Math.min(rectW, rectH) * 0.34 * zoom;
    const cx0 = rectW * 0.5, cy0 = rectH * 0.52;

    function project(ux, uy, uz) {
      const x1 = ux * cy - uz * sy, z1 = ux * sy + uz * cy;
      const y2 = uy * cp - z1 * sp, z2 = uy * sp + z1 * cp;
      const persp = 3.4 / (3.4 + z2 * 0.55);
      return [cx0 + x1 * radius * persp, cy0 - y2 * radius * persp, z2];
    }

    const corners = [
      [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
      [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]
    ].map((c) => project(c[0], c[1], c[2]));
    const boxEdges = [
      [0, 1], [1, 2], [2, 3], [3, 0],
      [4, 5], [5, 6], [6, 7], [7, 4],
      [0, 4], [1, 5], [2, 6], [3, 7]
    ];

    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    ctx.lineWidth = 1;
    for (const [a, b] of boxEdges) {
      if ((corners[a][2] + corners[b][2]) * 0.5 > 0) {
        ctx.beginPath(); ctx.moveTo(corners[a][0], corners[a][1]); ctx.lineTo(corners[b][0], corners[b][1]); ctx.stroke();
      }
    }

    const drawList = [];
    const maxDim = Math.max(nx, ny, nz);
    const stride = Math.max(1, Math.ceil(maxDim / 34));
    const voxelDiag = Math.hypot(sdf.bounds.step[0], sdf.bounds.step[1], sdf.bounds.step[2]);

    function pushSlicePlane(axis, sliceIdx, alpha, highlightBorder) {
      const axisLen = axis === "x" ? nx : axis === "y" ? ny : nz;
      const sNorm = ((sliceIdx + 0.5) / axisLen) * 2 - 1;
      const uMax = axis === "x" ? ny : nx;
      const vMax = axis === "z" ? ny : nz;
      const stepUV = Math.max(1, Math.ceil(Math.max(uMax, vMax) / 36));

      for (let v = 0; v < vMax; v += stepUV) {
        const v0 = (v / vMax) * 2 - 1, v1 = (Math.min(vMax, v + stepUV) / vMax) * 2 - 1;
        for (let u = 0; u < uMax; u += stepUV) {
          const u0 = (u / uMax) * 2 - 1, u1 = (Math.min(uMax, u + stepUV) / uMax) * 2 - 1;
          const idx = getSdfVoxelIndex(nx, ny, nz, axis, sliceIdx, u, v);
          const d = sdf.distances[idx];
          const rgba = sampleSdfColor(d, sdf.confidence[idx], sdf, displayMode);
          const isZero = Math.abs(d) <= voxelDiag * 0.85;
          const cellAlpha = isZero ? Math.min(0.95, alpha + 0.35) : d > 0 ? alpha * 0.45 : alpha;

          const q = axis === "x"
            ? [project(sNorm, u0, v0), project(sNorm, u1, v0), project(sNorm, u1, v1), project(sNorm, u0, v1)]
            : axis === "y"
              ? [project(u0, sNorm, v0), project(u1, sNorm, v0), project(u1, sNorm, v1), project(u0, sNorm, v1)]
              : [project(u0, v0, sNorm), project(u1, v0, sNorm), project(u1, v1, sNorm), project(u0, v1, sNorm)];
          drawList.push({ type: "quad", q, rgba, alpha: cellAlpha, depth: (q[0][2] + q[2][2]) * 0.5 });
        }
      }

      if (highlightBorder) {
        const bCorners = axis === "x"
          ? [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => project(sNorm, a, b))
          : axis === "y"
            ? [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => project(a, sNorm, b))
            : [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => project(a, b, sNorm));
        drawList.push({ type: "frame", corners: bCorners, depth: (bCorners[0][2] + bCorners[2][2]) * 0.5 - 0.01 });
      }
    }

    const sxIdx = clamp(activeAxis === "x" ? state.sliceIndex : Math.floor(nx / 2), 0, nx - 1);
    const syIdx = clamp(activeAxis === "y" ? state.sliceIndex : Math.floor(ny / 2), 0, ny - 1);
    const szIdx = clamp(activeAxis === "z" ? state.sliceIndex : Math.floor(nz / 2), 0, nz - 1);

    if (mode3d === "stack") {
      const maxS = (activeAxis === "x" ? nx : activeAxis === "y" ? ny : nz) - 1;
      for (let k = 0; k < 7; k++) {
        const idx = Math.round((k / 6) * maxS);
        const cur = Math.abs(idx - state.sliceIndex) <= Math.ceil(maxS / 12);
        pushSlicePlane(activeAxis, cur ? state.sliceIndex : idx, cur ? 0.78 : 0.22, cur);
      }
    } else if (mode3d === "triplane") {
      pushSlicePlane("x", sxIdx, activeAxis === "x" ? 0.8 : 0.36, activeAxis === "x");
      pushSlicePlane("y", syIdx, activeAxis === "y" ? 0.8 : 0.36, activeAxis === "y");
      pushSlicePlane("z", szIdx, activeAxis === "z" ? 0.8 : 0.36, activeAxis === "z");
    } else if (mode3d === "combo") {
      const curIdx = activeAxis === "x" ? sxIdx : activeAxis === "y" ? syIdx : szIdx;
      pushSlicePlane(activeAxis, curIdx, 0.78, true);
    }

    if (mode3d === "combo") {
      const isoBand = voxelDiag * 0.62;
      const dotSize = Math.max(2.1, (radius / maxDim) * stride * 1.3);
      for (let z = 1; z < nz - 1; z += stride) {
        const uz = ((z + 0.5) / nz) * 2 - 1;
        for (let y = 1; y < ny - 1; y += stride) {
          const uy = ((y + 0.5) / ny) * 2 - 1;
          for (let x = 1; x < nx - 1; x += stride) {
            const idx = (z * ny + y) * nx + x;
            if (Math.abs(sdf.distances[idx]) > isoBand) continue;
            const gx = sdf.distances[idx + 1] - sdf.distances[idx - 1];
            const gy = sdf.distances[idx + nx] - sdf.distances[idx - nx];
            const gz = sdf.distances[idx + nx * ny] - sdf.distances[idx - nx * ny];
            const [nnx, nny, nnz] = normalize3(gx, gy, gz);
            const p = project(((x + 0.5) / nx) * 2 - 1, uy, uz);
            const lit = 0.28 + clamp(nnx * 0.48 + nny * 0.72 + nnz * 0.5, 0, 1) * 0.68;
            drawList.push({ type: "iso", x: p[0], y: p[1], r: dotSize, lit, depth: p[2] });
          }
        }
      }
    }

    drawList.sort((a, b) => b.depth - a.depth);
    for (let i = 0; i < drawList.length; i++) {
      const it = drawList[i];
      if (it.type === "quad") {
        ctx.fillStyle = `rgba(${it.rgba[0]},${it.rgba[1]},${it.rgba[2]},${it.alpha.toFixed(3)})`;
        ctx.beginPath();
        ctx.moveTo(it.q[0][0], it.q[0][1]); ctx.lineTo(it.q[1][0], it.q[1][1]);
        ctx.lineTo(it.q[2][0], it.q[2][1]); ctx.lineTo(it.q[3][0], it.q[3][1]);
        ctx.closePath(); ctx.fill();
      } else if (it.type === "iso") {
        const g = Math.round(205 * it.lit + 28);
        const r = Math.round(115 * it.lit + 22);
        const b = Math.round(176 * it.lit + 32);
        ctx.fillStyle = `rgba(${r},${g},${b},0.78)`;
        ctx.beginPath(); ctx.arc(it.x, it.y, it.r, 0, Math.PI * 2); ctx.fill();
      } else if (it.type === "frame") {
        ctx.strokeStyle = "rgba(255,255,255,0.45)";
        ctx.lineWidth = 1.3;
        ctx.beginPath();
        ctx.moveTo(it.corners[0][0], it.corners[0][1]); ctx.lineTo(it.corners[1][0], it.corners[1][1]);
        ctx.lineTo(it.corners[2][0], it.corners[2][1]); ctx.lineTo(it.corners[3][0], it.corners[3][1]);
        ctx.closePath(); ctx.stroke();
      }
    }

    ctx.strokeStyle = "rgba(255,255,255,0.16)";
    for (const [a, b] of boxEdges) {
      if ((corners[a][2] + corners[b][2]) * 0.5 <= 0) {
        ctx.beginPath(); ctx.moveTo(corners[a][0], corners[a][1]); ctx.lineTo(corners[b][0], corners[b][1]); ctx.stroke();
      }
    }
  }

  /* ----------------------------------------------------------------------------------------------------------------------------------------
   * Parameterized 2D UV Mesh-Map Baker (Supports Custom W x H up to 16384 x 16384 + All Per-Map Properties)
   * -------------------------------------------------------------------------------------------------------------------------------------- */

  function hashColor(id, paletteMode, seed) {
    const idx = Math.abs(Math.round(id) + Math.round(seed || 0) * 3);
    if (paletteMode === "pastel") {
      const p = [[224, 178, 164], [166, 202, 224], [176, 218, 179], [208, 178, 222], [230, 216, 162], [162, 216, 208]];
      return p[idx % p.length];
    }
    if (paletteMode === "vivid") {
      const p = [[242, 78, 62], [48, 164, 246], [74, 222, 128], [192, 92, 246], [248, 198, 48], [38, 214, 196]];
      return p[idx % p.length];
    }
    const studio = [[219, 122, 92], [110, 174, 214], [142, 198, 125], [194, 134, 196], [222, 196, 108], [104, 196, 178]];
    return studio[idx % studio.length];
  }

  async function bakeMeshMap2D(mapId, asset, settings, onProgress) {
    const t0 = performance.now();
    const targetW = clamp(Math.round(settings.width || settings.resolution || 512), 64, 16384);
    const targetH = clamp(Math.round(settings.height || settings.resolution || 512), 64, 16384);

    // Cap interactive browser canvas buffer to 2048px max axis so 8K / 16K bakes never OOM-crash the browser tab!
    const maxCanvasAxis = 2048;
    const scaleDown = Math.min(1, maxCanvasAxis / Math.max(targetW, targetH));
    const bufW = Math.max(64, Math.round(targetW * scaleDown));
    const bufH = Math.max(64, Math.round(targetH * scaleDown));

    const padPx = clamp(Math.round((settings.paddingPx ?? 8) * scaleDown), 0, 48);
    const flipY = settings.normalHandedness === "directx" ? -1 : 1;
    const invertNormals = !!settings.invertNormals ? -1 : 1;
    const zUp = settings.axisSwizzle === "zup";
    const invertMask = !!settings.invertMask;

    const highMesh = asset.high;
    const lowMesh = asset.low;
    // When sub-meshes are toggled in High vs Low outliner, honor active High parts!
    const activeHighIndices = new Set((asset.highParts || []).filter((p) => p.enabled).map((p) => p.partIndex));
    const mesh = highMesh && highMesh.uvs ? highMesh : lowMesh;
    const { positions, normals, uvs, colors, chartIds, partIds, relief, indices, bounds } = mesh;

    const rgba = new Uint8ClampedArray(bufW * bufH * 4);
    const mask = new Uint8Array(bufW * bufH);
    const triCount = indices.length / 3;

    const vCount = positions.length / 3;
    const vertCurv = new Float32Array(vCount);
    const vertAo = new Float32Array(vCount).fill(1);
    const vertThick = new Float32Array(vCount).fill(0.5);

    const neighbors = Array.from({ length: vCount }, () => []);
    for (let t = 0; t < indices.length; t += 3) {
      const a = indices[t], b = indices[t + 1], c = indices[t + 2];
      neighbors[a].push(b, c); neighbors[b].push(a, c); neighbors[c].push(a, b);
    }

    const curvBoost = Number(settings.curvatureBoost ?? 1.35) * (settings.curvAlgorithm === "sharp" ? 1.45 : 1.0);
    const aoStrength = Number(settings.aoStrength ?? 1.05);
    const aoGamma = Number(settings.aoGamma ?? 1.0);
    const rayQuality = Math.log2(Number(settings.aoRays || 64)) / 6;
    const thickScale = Number(settings.thicknessScale ?? 1.0);

    for (let v = 0; v < vCount; v++) {
      const nb = neighbors[v];
      if (!nb.length) continue;
      const px = positions[v * 3], py = positions[v * 3 + 1], pz = positions[v * 3 + 2];
      const nx = normals[v * 3], ny = normals[v * 3 + 1], nz = normals[v * 3 + 2];
      let curvAcc = 0, aoAcc = 0;
      for (let k = 0; k < nb.length; k++) {
        const u = nb[k];
        const dx = positions[u * 3] - px, dy = positions[u * 3 + 1] - py, dz = positions[u * 3 + 2] - pz;
        const dist = Math.hypot(dx, dy, dz) || 1e-5;
        const dn = (normals[u * 3] - nx) * dx + (normals[u * 3 + 1] - ny) * dy + (normals[u * 3 + 2] - nz) * dz;
        curvAcc += dn / (dist * dist * 14);
        const horizon = (dx * nx + dy * ny + dz * nz) / dist;
        if (horizon > 0) aoAcc += horizon;
      }
      const cVal = clamp((curvAcc / nb.length) * curvBoost, -1, 1);
      vertCurv[v] = cVal;
      const distFactor = Number(settings.aoDistance ?? 0.85) * (settings.aoDistribution === "uniform" ? 0.88 : 1.05);
      const cavityOcc = clamp((-cVal) * 0.62 * rayQuality + (aoAcc / nb.length) * 0.55 * distFactor - (relief[v] || 0) * 4.5, 0, 0.95);
      vertAo[v] = Math.pow(clamp(1 - cavityOcc * aoStrength, 0.04, 1), aoGamma);
      const radial = Math.hypot(px, py, pz) / (bounds.diag * 0.5);
      vertThick[v] = Math.pow(clamp((0.28 + (1 - radial * 0.55) * 0.65 + (relief[v] || 0) * 3.0) * thickScale, 0.03, 1), aoGamma);
    }

    if (onProgress) {
      onProgress(0.4);
      await new Promise((r) => setTimeout(r, 0));
    }

    const reliefStrength = Number(settings.reliefStrength ?? 1.35);
    const bevelRadius = Number(settings.bevelRadius ?? 1.15);
    const bevelStrength = Number(settings.bevelStrength ?? 1.45);
    const curvNeutral = Number(settings.curvNeutral ?? 0.5);
    const maskThreshold = Number(settings.maskThreshold ?? 0.02);
    const heightGain = Number(settings.heightGain ?? 10.5);
    const wireWidth = Number(settings.wireWidth ?? 1.2);
    const wireBright = Number(settings.wireBrightness ?? 0.9);
    const dustAxis = settings.dustAxis || "+y";
    const dustSlope = Number(settings.dustSlope ?? 1.6);
    const colorGamma = Number(settings.colorGamma ?? 1.0);

    for (let t = 0; t < triCount; t++) {
      const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
      if (activeHighIndices.size > 0 && !activeHighIndices.has(Math.round(partIds[i0]))) {
        if (mapId !== "coverage" && mapId !== "wireframe" && mapId !== "uvCoords" && mapId !== "uvIslandId") continue;
      }

      const u0 = uvs[i0 * 2] * (bufW - 1), v0 = (1 - uvs[i0 * 2 + 1]) * (bufH - 1);
      const u1 = uvs[i1 * 2] * (bufW - 1), v1 = (1 - uvs[i1 * 2 + 1]) * (bufH - 1);
      const u2 = uvs[i2 * 2] * (bufW - 1), v2 = (1 - uvs[i2 * 2 + 1]) * (bufH - 1);

      const denom = (v1 - v2) * (u0 - u2) + (u2 - u1) * (v0 - v2);
      if (Math.abs(denom) < 1e-6) continue;
      if (Math.max(Math.abs(u0 - u1), Math.abs(u1 - u2), Math.abs(u2 - u0)) > bufW * 0.65) continue;

      const minX = clamp(Math.floor(Math.min(u0, u1, u2)), 0, bufW - 1);
      const maxX = clamp(Math.ceil(Math.max(u0, u1, u2)), 0, bufW - 1);
      const minY = clamp(Math.floor(Math.min(v0, v1, v2)), 0, bufH - 1);
      const maxY = clamp(Math.ceil(Math.max(v0, v1, v2)), 0, bufH - 1);

      const p0x = positions[i0 * 3], p0y = positions[i0 * 3 + 1], p0z = positions[i0 * 3 + 2];
      const e1x = positions[i1 * 3] - p0x, e1y = positions[i1 * 3 + 1] - p0y, e1z = positions[i1 * 3 + 2] - p0z;
      const e2x = positions[i2 * 3] - p0x, e2y = positions[i2 * 3 + 1] - p0y, e2z = positions[i2 * 3 + 2] - p0z;
      const [fnx, fny, fnz] = normalize3(e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x);

      for (let py = minY; py <= maxY; py++) {
        for (let px = minX; px <= maxX; px++) {
          const w0 = ((v1 - v2) * (px - u2) + (u2 - u1) * (py - v2)) / denom;
          const w1 = ((v2 - v0) * (px - u2) + (u0 - u2) * (py - v2)) / denom;
          const w2 = 1 - w0 - w1;
          if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) continue;

          const idx = py * bufW + px;
          mask[idx] = 1;

          const [nxRaw, nyRaw, nzRaw] = normalize3(
            normals[i0 * 3] * w0 + normals[i1 * 3] * w1 + normals[i2 * 3] * w2,
            normals[i0 * 3 + 1] * w0 + normals[i1 * 3 + 1] * w1 + normals[i2 * 3 + 1] * w2,
            normals[i0 * 3 + 2] * w0 + normals[i1 * 3 + 2] * w1 + normals[i2 * 3 + 2] * w2
          );
          const nx = nxRaw * invertNormals, ny = nyRaw * invertNormals, nz = nzRaw * invertNormals;
          const wx = p0x + e1x * w1 + e2x * w2;
          const wy = p0y + e1y * w1 + e2y * w2;
          const wz = p0z + e1z * w1 + e2z * w2;
          const curv = vertCurv[i0] * w0 + vertCurv[i1] * w1 + vertCurv[i2] * w2;
          const ao = vertAo[i0] * w0 + vertAo[i1] * w1 + vertAo[i2] * w2;
          const thick = vertThick[i0] * w0 + vertThick[i1] * w1 + vertThick[i2] * w2;
          const rel = (relief[i0] || 0) * w0 + (relief[i1] || 0) * w1 + (relief[i2] || 0) * w2;

          const upRefX = Math.abs(ny) > 0.94 ? 1 : 0, upRefY = Math.abs(ny) > 0.94 ? 0 : 1;
          const [txS, tyS, tzS] = normalize3(upRefY * nz, -upRefX * nz, upRefX * ny - upRefY * nx);
          const bx = ny * tzS - nz * tyS, by = nz * txS - nx * tzS, bz = nx * tyS - ny * txS;
          const [bnx, bny, bnz] = normalize3(wx, wy * 0.85, wz);
          const dNx = nx - bnx, dNy = ny - bny, dNz = nz - bnz;

          let r = 128, g = 128, b = 255;

          switch (mapId) {
            case "normalTangent":
            case "bevelNormal":
            case "bentNormal": {
              const gain = mapId === "bevelNormal" ? bevelStrength * bevelRadius : reliefStrength;
              const bentUp = mapId === "bentNormal" ? (1 - ao) * 0.52 * Number(settings.bentCone ?? 1.0) : 0;
              const basisSign = settings.tangentBasis === "uv" ? 0.85 : 1.0;
              const tComp = (dNx * txS + dNy * tyS + dNz * tzS) * gain * basisSign;
              const bComp = ((dNx * bx + dNy * by + dNz * bz) * gain + bentUp) * flipY;
              const [tnx, tny, tnz] = normalize3(tComp, bComp, 1.0);
              const dither = settings.dither8bit ? (((px * 73 + py * 179) & 7) - 3.5) / 255 : 0;
              r = clamp(Math.round((tnx * 0.5 + 0.5 + dither) * 255), 0, 255);
              g = clamp(Math.round((tny * 0.5 + 0.5 + dither) * 255), 0, 255);
              b = clamp(Math.round((tnz * 0.5 + 0.5) * 255), 0, 255);
              break;
            }
            case "normalWorld":
            case "vertexNormal": {
              const sx = nx, sy = zUp ? nz : ny, sz = zUp ? ny : nz;
              r = Math.round((sx * 0.5 + 0.5) * 255);
              g = Math.round((sy * 0.5 + 0.5) * 255);
              b = Math.round((sz * 0.5 + 0.5) * 255);
              break;
            }
            case "normalObject": {
              const sx = nx * 0.92 + bx * 0.08, sy = zUp ? nz : ny, sz = zUp ? ny : nz;
              r = Math.round((sx * 0.5 + 0.5) * 255);
              g = Math.round((sy * 0.5 + 0.5) * 255);
              b = Math.round((sz * 0.5 + 0.5) * 255);
              break;
            }
            case "facetedNormal": {
              const sx = fnx * invertNormals, sy = (zUp ? fnz : fny) * invertNormals, sz = (zUp ? fny : fnz) * invertNormals;
              r = Math.round((sx * 0.5 + 0.5) * 255);
              g = Math.round((sy * 0.5 + 0.5) * 255);
              b = Math.round((sz * 0.5 + 0.5) * 255);
              break;
            }
            case "ao":
            case "secondaryAo": {
              const v = Math.round(ao * 255);
              r = g = b = v;
              break;
            }
            case "curvature": {
              let s = clamp(curvNeutral + curv * 0.5, 0, 1);
              if (invertMask) s = 1 - s;
              r = g = b = Math.round(s * 255);
              break;
            }
            case "convexity": {
              let s = clamp((Math.max(0, curv) - maskThreshold) / (1 - maskThreshold), 0, 1);
              if (invertMask) s = 1 - s;
              r = g = b = Math.round(s * 255);
              break;
            }
            case "concavity": {
              let s = clamp((Math.max(0, -curv) - maskThreshold) / (1 - maskThreshold), 0, 1);
              if (invertMask) s = 1 - s;
              r = g = b = Math.round(s * 255);
              break;
            }
            case "thickness": {
              let s = thick;
              if (invertMask) s = 1 - s;
              r = g = b = Math.round(s * 255);
              break;
            }
            case "height": {
              let s = clamp(curvNeutral + rel * heightGain, 0, 1);
              if (invertMask) s = 1 - s;
              r = g = b = Math.round(s * 255);
              break;
            }
            case "vectorDisplacement": {
              const vy = zUp ? nz : ny, vz = zUp ? ny : nz;
              r = Math.round(clamp(0.5 + nx * rel * heightGain, 0, 1) * 255);
              g = Math.round(clamp(0.5 + vy * rel * heightGain, 0, 1) * 255);
              b = Math.round(clamp(0.5 + vz * rel * heightGain, 0, 1) * 255);
              break;
            }
            case "position": {
              let nxp, nyp, nzp;
              if (settings.posNormalization === "sphere") {
                const rad = bounds.diag * 0.5 || 1;
                nxp = wx / (2 * rad) + 0.5;
                nyp = wy / (2 * rad) + 0.5;
                nzp = wz / (2 * rad) + 0.5;
              } else {
                nxp = (wx - bounds.min[0]) / (bounds.size[0] || 1);
                nyp = (wy - bounds.min[1]) / (bounds.size[1] || 1);
                nzp = (wz - bounds.min[2]) / (bounds.size[2] || 1);
              }
              r = Math.round(clamp(nxp, 0, 1) * 255);
              g = Math.round(clamp(zUp ? nzp : nyp, 0, 1) * 255);
              b = Math.round(clamp(zUp ? nyp : nzp, 0, 1) * 255);
              break;
            }
            case "objectId":
              [r, g, b] = hashColor(partIds[i0], settings.idPalette, settings.idSeed);
              break;
            case "uvIslandId":
              [r, g, b] = hashColor(chartIds[i0], settings.idPalette, settings.idSeed);
              break;
            case "uvCoords": {
              const vCoord = settings.uvFlipV ? py / (bufH - 1) : 1 - py / (bufH - 1);
              r = Math.round((px / (bufW - 1)) * 255);
              g = Math.round(vCoord * 255);
              b = Math.round(clamp(Number(settings.uvBlue ?? 0.14), 0, 1) * 255);
              break;
            }
            case "wireframe": {
              const minBary = Math.min(w0, w1, w2);
              const edge = 1 - smoothstep(0.008 * wireWidth, 0.055 * wireWidth, minBary);
              const v = Math.round((0.08 + edge * 0.92 * wireBright) * 255);
              r = g = b = v;
              break;
            }
            case "coverage":
            case "hitMask": {
              let s = 0.96;
              if (invertMask) s = 1 - s;
              r = g = b = Math.round(s * 255);
              break;
            }
            case "bevelMask": {
              const s = clamp((Math.abs(curv) * bevelStrength - maskThreshold) / (1 - maskThreshold), 0, 1);
              r = g = b = Math.round(s * 255);
              break;
            }
            case "dustMask": {
              const align = dustAxis === "+z" ? nz : dustAxis === "-y" ? -ny : ny;
              const up = clamp(align * 0.5 + 0.5, 0, 1);
              const s = clamp(Math.pow(up, dustSlope) * Math.pow(ao, clamp(aoStrength, 0, 2)), 0, 1);
              r = g = b = Math.round(s * 255);
              break;
            }
            case "dirtMask": {
              const raw = (1 - ao) * 0.78 + Math.max(0, -curv) * 0.55;
              const s = clamp((raw - maskThreshold) / (1 - maskThreshold), 0, 1);
              r = g = b = Math.round(s * 255);
              break;
            }
            case "edgeWear": {
              const raw = Math.max(0, curv) * Math.pow(ao, clamp(aoStrength, 0, 2));
              const s = clamp((raw - maskThreshold) / (1 - maskThreshold), 0, 1);
              r = g = b = Math.round(s * 255);
              break;
            }
            case "vertexColor": {
              const cr = Math.pow(clamp(colors[i0 * 3] * w0 + colors[i1 * 3] * w1 + colors[i2 * 3] * w2, 0, 1), colorGamma);
              const cg = Math.pow(clamp(colors[i0 * 3 + 1] * w0 + colors[i1 * 3 + 1] * w1 + colors[i2 * 3 + 1] * w2, 0, 1), colorGamma);
              const cb = Math.pow(clamp(colors[i0 * 3 + 2] * w0 + colors[i1 * 3 + 2] * w1 + colors[i2 * 3 + 2] * w2, 0, 1), colorGamma);
              r = Math.round(cr * 255); g = Math.round(cg * 255); b = Math.round(cb * 255);
              break;
            }
          }

          const o = idx * 4;
          rgba[o] = r; rgba[o + 1] = g; rgba[o + 2] = b; rgba[o + 3] = 255;
        }
      }
    }

    let finalMask = mask;
    if (padPx > 0 && mapId !== "coverage" && mapId !== "wireframe") {
      let curMask = mask;
      for (let pass = 0; pass < padPx; pass++) {
        const nextMask = new Uint8Array(curMask);
        for (let y = 1; y < bufH - 1; y++) {
          for (let x = 1; x < bufW - 1; x++) {
            const idx = y * bufW + x;
            if (curMask[idx]) continue;
            const nIdx = curMask[idx - 1]
              ? idx - 1
              : curMask[idx + 1]
                ? idx + 1
                : curMask[idx - bufW]
                  ? idx - bufW
                  : curMask[idx + bufW]
                    ? idx + bufW
                    : -1;
            if (nIdx >= 0) {
              nextMask[idx] = 1;
              rgba[idx * 4] = rgba[nIdx * 4];
              rgba[idx * 4 + 1] = rgba[nIdx * 4 + 1];
              rgba[idx * 4 + 2] = rgba[nIdx * 4 + 2];
              rgba[idx * 4 + 3] = 255;
            }
          }
        }
        curMask = nextMask;
      }
      finalMask = curMask;
    }

    const isNormalMap = mapId.includes("Normal") || mapId.includes("normal");
    const gutterBg = isNormalMap
      ? [128, 128, 255, 255]
      : mapId === "curvature" || mapId === "height"
        ? [Math.round(curvNeutral * 255), Math.round(curvNeutral * 255), Math.round(curvNeutral * 255), 255]
        : [15, 16, 19, 255];
    const transparentMiss = mapId === "hitMask" && settings.transparentMiss !== false;

    for (let i = 0; i < finalMask.length; i++) {
      if (!finalMask[i]) {
        const vInv = invertMask && (mapId === "coverage" || mapId === "hitMask") ? 245 : gutterBg[0];
        rgba[i * 4] = vInv;
        rgba[i * 4 + 1] = mapId === "coverage" || mapId === "hitMask" ? vInv : gutterBg[1];
        rgba[i * 4 + 2] = mapId === "coverage" || mapId === "hitMask" ? vInv : gutterBg[2];
        rgba[i * 4 + 3] = transparentMiss ? 0 : 255;
      }
    }

    const offscreen = document.createElement("canvas");
    offscreen.width = bufW;
    offscreen.height = bufH;
    offscreen.getContext("2d").putImageData(new ImageData(rgba, bufW, bufH), 0, 0);

    return {
      kind: "map2d",
      mapId,
      meshName: asset.label,
      width: targetW,
      height: targetH,
      resolution: targetW,
      bufferWidth: bufW,
      bufferHeight: bufH,
      format: settings.format || "png8",
      paddingPx: settings.paddingPx ?? 8,
      rgba,
      canvas: offscreen,
      elapsedMs: Math.round(performance.now() - t0)
    };
  }

  function render2dMapWithChannel(destCanvas, bakedMap, lowMesh, viewOptions) {
    if (!destCanvas || !bakedMap) return;
    const w = bakedMap.bufferWidth, h = bakedMap.bufferHeight;
    if (destCanvas.width !== w || destCanvas.height !== h) {
      destCanvas.width = w;
      destCanvas.height = h;
    }
    const ctx = destCanvas.getContext("2d");
    const channel = viewOptions.channel || "rgb";

    if (channel === "rgb") {
      ctx.clearRect(0, 0, w, h);
      ctx.drawImage(bakedMap.canvas, 0, 0);
    } else {
      const src = bakedMap.rgba;
      const out = new Uint8ClampedArray(src.length);
      for (let i = 0; i < src.length; i += 4) {
        let v = src[i];
        if (channel === "g") v = src[i + 1];
        else if (channel === "b") v = src[i + 2];
        else if (channel === "luma") v = Math.round(src[i] * 0.2126 + src[i + 1] * 0.7152 + src[i + 2] * 0.0722);
        out[i] = out[i + 1] = out[i + 2] = v;
        out[i + 3] = src[i + 3];
      }
      ctx.putImageData(new ImageData(out, w, h), 0, 0);
    }

    if (viewOptions.uvOverlay && lowMesh && lowMesh.uvs) {
      ctx.save();
      ctx.strokeStyle = "rgba(255,255,255,0.28)";
      ctx.lineWidth = Math.max(1, Math.max(w, h) / 680);
      ctx.beginPath();
      const { uvs, indices } = lowMesh;
      for (let t = 0; t < indices.length; t += 3) {
        const i0 = indices[t] * 2, i1 = indices[t + 1] * 2, i2 = indices[t + 2] * 2;
        const x0 = uvs[i0] * w, y0 = (1 - uvs[i0 + 1]) * h;
        const x1 = uvs[i1] * w, y1 = (1 - uvs[i1 + 1]) * h;
        const x2 = uvs[i2] * w, y2 = (1 - uvs[i2 + 1]) * h;
        if (Math.max(Math.abs(x0 - x1), Math.abs(x1 - x2), Math.abs(x2 - x0)) > w * 0.65) continue;
        ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.lineTo(x2, y2); ctx.closePath();
      }
      ctx.stroke();
      ctx.restore();
    }
  }

  global.FrontierBakingCore = {
    MESH_MAP_CATALOG,
    MAP_PROPERTY_SCHEMAS,
    createStudyAsset,
    parseObjToParts,
    parseStlToParts,
    auditTriangleMesh,
    bakeVolumeSdf,
    renderSdfSliceToCanvas,
    probeSdfSlice,
    renderSdf3dPreview,
    bakeMeshMap2D,
    render2dMapWithChannel
  };
})(window);

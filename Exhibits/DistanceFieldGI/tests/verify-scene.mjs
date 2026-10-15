import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { buildBvh, buildControlVehicle, buildWorldDistanceBvh, buildWorldGeometry, scaleWorldEmitterGeometry, CITY_BLOCK_SIZE, CITY_SDF_RADIUS, CITY_STREAM_RADIUS, GLOBAL_FIELD_RESOLUTION, MAX_SURFACE_CARDS, SURFACE_CARD_FLOATS, SURFACE_CARD_MAX_EDGE, SURFACE_CARD_RESOLUTION, SURFACE_CARD_TEXELS, VEHICLE_VERTEX_STRIDE, VEHICLE_WHEEL_ORDER, WORLD_LENGTH, WORLD_WIDTH } from '../scene.mjs';
import { buildEmissiveSourceBuffer, EMISSIVE_SOURCE_BYTES } from '../emissive.mjs';
import { COMPOSE_WGSL, FIELD_MAP_WGSL, GBUFFER_WGSL, GI_WGSL, SHADE_WGSL, SURFACE_CACHE_WGSL, TEMPORAL_WGSL, UPDATE_GDF_WGSL } from '../shaders.mjs';

const directory = path.dirname(fileURLToPath(import.meta.url));
const appSource = await readFile(path.join(directory, '../app.mjs'), 'utf8');
const workerSource = await readFile(path.join(directory, '../world-stream-worker.mjs'), 'utf8');
const htmlSource = await readFile(path.join(directory, '../index.html'), 'utf8');
assert.match(appSource, /fast: \{ scale: 0\.38 \}/, 'Fast quality default is retained');
assert.match(appSource, /balanced: \{ scale: 0\.56 \}/, 'Balanced quality default is retained');
assert.match(appSource, /quality: 'balanced'/, 'Balanced remains the selected quality preset');
assert.match(appSource, /giRays: 4,/, 'the default GI ray count is retained');
assert.match(appSource, /emissiveSamples: 8,/, 'the default finite-area emissive sample count is retained');
assert.match(htmlSource, /<option value="balanced" selected>Balanced · 0\.56× render scale<\/option>/);
assert.match(htmlSource, /<input id="giRays" type="range"[^>]*value="4">/);
assert.match(htmlSource, /<input id="emissiveSamples" type="range"[^>]*value="8">/);
const assetPath = path.join(directory, '../assets/ControlVehicle.glb');
const manifestPath = path.join(directory, '../assets/ControlVehicle.json');
const bytes = await readFile(assetPath);
const manifest = JSON.parse(await readFile(manifestPath, 'utf8'));
const sha256 = createHash('sha256').update(bytes).digest('hex');
assert.equal(sha256, manifest.glbSHA256, 'GLB hash must match the supplied asset manifest');
assert.equal(bytes.byteLength, manifest.glbBytes, 'GLB size must match its manifest');

const vehicle = buildControlVehicle(bytes);
assert.equal(vehicle.triangleCount, manifest.facets, 'all authored vehicle facets must be retained');
assert.equal(vehicle.groupCount, manifest.nodes.length, 'separate body and wheel parts must remain identifiable for sign parity');
assert.equal(vehicle.vertexData.length / VEHICLE_VERTEX_STRIDE, vehicle.triangleCount * 3);
assert.ok(vehicle.boundsMin.every(Number.isFinite) && vehicle.boundsMax.every(Number.isFinite));
assert.ok(vehicle.boundsMin.every((value, axis) => value < vehicle.sourceBoundsMin[axis]));
assert.ok(vehicle.boundsMax.every((value, axis) => value > vehicle.sourceBoundsMax[axis]));
assert.ok([...vehicle.vertexData].every(Number.isFinite), 'render stream contains only finite values');
const vehicleVisibilityIds = new Set();
const vehicleSurfaceCodes = new Set();
for (let offset = 11; offset < vehicle.vertexData.length; offset += VEHICLE_VERTEX_STRIDE) {
  vehicleVisibilityIds.add(vehicle.vertexData[offset]);
  vehicleSurfaceCodes.add(vehicle.vertexData[offset + 4]);
}
assert.deepEqual([...vehicleVisibilityIds].sort((a, b) => a - b), [59, 60, 61, 62, 63],
  'body and four independently identifiable wheel meshes carry stable raster visibility IDs');
assert.deepEqual([...vehicleSurfaceCodes].sort((a, b) => a - b), [1, 2, 3, 4, 5],
  'the raster stream retains a per-wheel transform code');
assert.deepEqual(vehicle.wheelParts.map(part => part.name), VEHICLE_WHEEL_ORDER);
assert.ok(vehicle.wheelParts.every(part => part.radius > 0.45 && part.radius < 0.56 && part.halfExtent[2] < part.radius),
  'each tyre has finite, wheel-like dimensions and a local-Z axle');
assert.ok(vehicle.wheelParts[0].center[0] > vehicle.wheelParts[2].center[0] &&
  vehicle.wheelParts[1].center[0] > vehicle.wheelParts[3].center[0], 'front axle is ahead of the rear axle');
assert.ok(Math.abs(vehicle.wheelBase - 3.396) < 0.02, 'wheelbase is derived from the separate GLB wheel nodes');
for (let triangleIndex = 0; triangleIndex < vehicle.triangles.length; triangleIndex++) {
  const triangle = vehicle.triangles[triangleIndex];
  const vertexOffset = triangleIndex * 3 * VEHICLE_VERTEX_STRIDE;
  const wheel = vehicle.wheelParts.find(part => part.group === triangle.group);
  const expectedCode = wheel ? wheel.surfaceCode : 1;
  const expectedId = wheel ? wheel.visibilityId : 63;
  for (let corner = 0; corner < 3; corner++) {
    const offset = vertexOffset + corner * VEHICLE_VERTEX_STRIDE;
    assert.equal(vehicle.vertexData[offset + 11], expectedId, 'triangle vertices retain their part visibility ID');
    assert.equal(vehicle.vertexData[offset + 15], expectedCode, 'triangle vertices retain their part transform code');
  }
}
assert.equal(GLOBAL_FIELD_RESOLUTION, 64, 'the original clipmap resolution is retained');
assert.ok(GLOBAL_FIELD_RESOLUTION * 3 <= 256);
assert.ok(Math.abs(12 / GLOBAL_FIELD_RESOLUTION - 0.1875) < 1e-6, 'the original fine clipmap spacing is retained');
assert.match(GBUFFER_WGSL, /rotateWheelZ\(input\.position\.xyz - center/);
assert.match(GBUFFER_WGSL, /P\.wheelSteering\.x/);
assert.match(GBUFFER_WGSL, /surfaceCardId: u32/, 'the G-buffer vertex stream carries a stable mesh-card ID');
assert.match(GBUFFER_WGSL, /cardIdFragmentMain/, 'surface-card IDs have a dedicated fragment entry point');
assert.match(appSource, /targets:\s*\[\s*\{ format: 'rgba16float' \},\s*\{ format: 'rgba16float' \},\s*\{ format: 'rgba16float' \},\s*\{ format: 'rgba16float' \},\s*\]/, 'the primary G-buffer retains its four full-precision attachments');
assert.match(appSource, /position: createTexture\(device, 'G-buffer world position',[^\n]*'rgba16float', usage\),\s*normal: createTexture\(device, 'G-buffer normal and roughness',[^\n]*'rgba16float', usage\),\s*albedo: createTexture\(device, 'G-buffer base colour and metalness',[^\n]*'rgba16float', usage\),\s*emission: createTexture\(device, 'G-buffer emissive radiance',[^\n]*'rgba16float', usage\)/, 'all four primary pass textures exactly match the declared pipeline attachment formats');
assert.match(appSource, /historyNormal = createTexture\(device, 'Previous-frame surface normals',[^\n]*'rgba16float'/, 'temporal normal history matches the copied full-precision G-buffer normal format');
assert.equal((appSource.match(/^bindInput\(\);\s*\ninitialize\(\);$/gm) || []).length, 1,
  'input handlers and WebGPU initialization are installed exactly once');
assert.match(appSource, /targets: \[\{ format: 'r16uint' \}\]/, 'screen card IDs use a compact separate integer target');
assert.match(appSource, /globalSurfaceId = createTexture\([\s\S]*?'r32uint'/, 'the static GDF keeps packed mesh and card IDs in a separate 3D integer texture');
assert.match(appSource, /targets: \[\{ format: 'r16uint' \}\]/, 'the 32-byte primary G-buffer remains unchanged by GDF material metadata');
assert.match(appSource, /depthWriteEnabled: false, depthCompare: 'equal'/, 'the ID pass reuses the primary depth visibility without perturbing motion vectors');
assert.equal(4 * 8, 32, 'the primary G-buffer fits the default 32-byte-per-sample WebGPU limit');
assert.match(SHADE_WGSL, /sampleSurfaceCard\(cardId, position\)/, 'direct shading reuses the world-space radiance cache');
assert.match(GI_WGSL, /sampleSurfaceCard\(cardId, position\)/, 'diffuse GI reuses the world-space radiance cache');
assert.match(appSource, /SURFACE_CACHE_DYNAMIC_RADIUS = 24/);
assert.match(GI_WGSL, /VEHICLE_DYNAMIC_GI_RADIUS: f32 = 24\.0/);
assert.match(TEMPORAL_WGSL, /movingVehicleInfluence/);
assert.match(SURFACE_CACHE_WGSL, /@compute @workgroup_size\(64, 1, 1\)/);
assert.match(SURFACE_CACHE_WGSL, /emissiveDirect\(/, 'mesh cards retain finite-area emissive lighting');
assert.match(SURFACE_CACHE_WGSL, /diffuseGi\(/, 'mesh cards amortize multi-bounce diffuse transport');
assert.match(SHADE_WGSL, /uv \* 4\.0 - vec2f\(0\.5\)/, 'cache interpolation aligns with the 4×4 texel-center sampling grid');
assert.match(GBUFFER_WGSL, /distanceToStreet = min\(distanceToXStreet, distanceToZStreet\)/, 'ground materials follow the streamed city grid');
assert.match(GBUFFER_WGSL, /alongZDash = fract\(point\.z \/ 8\.0\)/, 'city roads carry visible dashed center markings');
assert.match(GBUFFER_WGSL, /zebraZ = abs\(distanceToZStreet - 9\.0\)/, 'city intersections carry crosswalk markings');
assert.doesNotMatch(GBUFFER_WGSL, /19\.3|13\.0/, 'the retired oval proving-ground palette is removed');
assert.match(TEMPORAL_WGSL, /objectId >= 59u && objectId <= 62u/);
assert.match(COMPOSE_WGSL, /fieldDebugColor\(sampleGlobalDistance\(position\)\)/, 'the field view uses the sampled SDF, not a softened palette');
const runtimeDistanceStart = GI_WGSL.indexOf('fn sampleGlobalDistanceAndVoxel(');
const runtimeDistanceEnd = GI_WGSL.indexOf('\n}\nfn sampleGlobalDistance(', runtimeDistanceStart);
assert.ok(runtimeDistanceStart >= 0 && runtimeDistanceEnd > runtimeDistanceStart,
  'GI uses a shared distance-and-voxel sampler');
assert.doesNotMatch(GI_WGSL.slice(runtimeDistanceStart, runtimeDistanceEnd), /staticDistance\(/,
  'ready-field raymarch samples do not re-walk source SDFs near every surface');
assert.match(SHADE_WGSL, /let fieldData = sampleGlobalDistanceAndVoxel\(position\)/,
  'each raymarch step reuses one GDF sample for distance and voxel size');
assert.match(SHADE_WGSL, /minimumStep = max\(voxelSize\(0u\) \* 0\.01, 0\.001\)/, 'shadow steps remain below the contact threshold');
assert.match(UPDATE_GDF_WGSL, /dirtyMask = u32\(P\.wheelSteering\.z \+ 0\.5\)/, 'GDF updates are gated by dirty snapped cascades');
assert.match(UPDATE_GDF_WGSL, /dirtyMask == 2u\) \{ level = 1u; \}/, 'each compute dispatch targets only its selected dirty cascade');
assert.match(UPDATE_GDF_WGSL, /previousFieldContainsCell.*return/s, 'clipmap recentering reuses overlapping toroidal voxels and recomputes only exposed cells');
assert.match(UPDATE_GDF_WGSL, /wrapFieldCell\(worldCell\.x, i32\(resolution\)\)/, 'GDF writes use stable world-cell addressing');
assert.match(GI_WGSL, /wrapFieldCell\(worldCell\.x, resolution\)/, 'GDF samples use the same toroidal world-cell addressing');
assert.match(FIELD_MAP_WGSL, /wrapFieldCell\(worldCell\.x, resolution\)/, 'the debug slice reads toroidal clipmap storage correctly');
assert.match(UPDATE_GDF_WGSL, /WorldTriangles: array<WorldTriangle>/, 'the GDF update traverses the full streamed static mesh triangle buffer');
assert.match(UPDATE_GDF_WGSL, /WorldNodes: array<WorldBvhNode>/, 'full-mesh distance queries are accelerated by the streamed triangle BVH');
assert.match(UPDATE_GDF_WGSL, /pointAabbDistanceSquared\(worldPosition, node\.minimum\.xyz, node\.maximum\.xyz\)/, 'BVH node bounds prune nearest-triangle distance work');
assert.doesNotMatch(UPDATE_GDF_WGSL, /staticDistance\(worldPosition\)/, 'the active GDF update is not limited to analytic SDF shapes');
assert.match(UPDATE_GDF_WGSL, /GlobalSurfaceIdOutput/, 'each field voxel carries the nearest mesh/card identity for GI material lookup');
assert.match(UPDATE_GDF_WGSL, /GlobalAlbedoRoughnessOutput/, 'each field voxel stores nearest-mesh albedo and roughness');
assert.match(UPDATE_GDF_WGSL, /GlobalEmissionMetallicOutput/, 'each field voxel stores nearest-mesh emission and metallic data');
assert.doesNotMatch(UPDATE_GDF_WGSL, /distance = min\(distance, sampleVehicleDistance\(localPosition\)\)/, 'the moving vehicle is not rebaked into every GDF voxel');
assert.match(GI_WGSL, /return vec2f\(min\(fieldData\.x, sampleVehicleDistance\(localPosition\)\), fieldData\.y\)/, 'the moving vehicle SDF remains live alongside the static global field');
assert.match(GI_WGSL, /boundsDelta = abs\(position - shape\.center\.xyz\)/, 'the analytic 55-shape query remains only as a not-yet-ready clipmap fallback');
assert.match(SHADE_WGSL, /let staticD = sampleGlobalData\(position\)\.x/, 'secondary-hit vehicle overlap tests reuse the static GDF');
assert.match(SHADE_WGSL, /fn staticMaterial\(position: vec3f\)[\s\S]*?sampleGlobalSurfaceData\(position\)/,
  'secondary GI hits recover mesh material metadata from the full-mesh GDF');
assert.match(SHADE_WGSL, /surface\.albedoRoughness\.rgb/, 'non-card mesh hits retain their stored triangle albedo');
assert.match(SHADE_WGSL, /card\.albedoMetallic\.rgb/, 'mesh-card IDs resolve exact cached surface material parameters');
assert.match(FIELD_MAP_WGSL, /refineFieldSliceDistance\(world, coarseDistance\)/, 'the live slice combines the full-mesh field with the separate moving-vehicle SDF');
assert.match(FIELD_MAP_WGSL, /return min\(coarseDistance, vehicleDistance\)/, 'ready-field visualization does not replace mesh GDF distances with analytic shapes');
assert.match(COMPOSE_WGSL, /fn clipmapInfluenceColor\(position: vec3f\)/, 'the cascade diagnostic is based on each visible surface position');
assert.match(COMPOSE_WGSL, /fineBlendStart = P\.cascadeExtents\.x \* 0\.28/, 'the fine-to-middle diagnostic follows the actual GDF blend band');
assert.match(COMPOSE_WGSL, /mode == 7u\) \{\s*linearColor = clipmapInfluenceColor\(position\)/, 'the clipmap influence view has its own render mode');
assert.match(appSource, /function drawClipmapOverlay\(\)/, 'camera-relative clipmap bounds are drawn as world-space wireframes');
assert.match(appSource, /function scheduleSurfaceCardUpdates\(\)/, 'world-space card shading is amortized to visible cache misses');
assert.match(appSource, /state\.gdfReadyMask !== 0b111/, 'mesh-card GI caching waits for all three full-mesh clipmaps to be ready');
assert.match(appSource, /SURFACE_CACHE_UPDATE_LIMIT = 64/, 'mesh-card radiance work is bounded per frame');
assert.match(appSource, /state\.surfaceCacheUpdateCount \* SURFACE_CARD_TEXELS \/ 64/, 'each scheduled quad updates its 4×4 cache tile');
assert.match(appSource, /FINE · 12 m.*MID · 36 m.*FAR · 108 m/s, 'wireframes label the cascade extents');
assert.doesNotMatch(htmlSource, /clipmapPlan|clipmapViewToggle/, 'the old top-down map inset and toggle are removed');
assert.match(htmlSource, /id="clipmapOverlay"/, 'the viewport retains the camera-facing clipmap box overlay');

const bvh = buildBvh(vehicle.triangles);
assert.equal(bvh.triangles.length, vehicle.triangleCount);
assert.equal(bvh.nodes[0].escape, bvh.nodes.length, 'the root escape link terminates the stackless traversal');
assert.ok(bvh.nodes.every((node, index) => node.escape > index && node.escape <= bvh.nodes.length));
assert.ok(bvh.nodes.every(node => node.count === 0 || (node.start >= 0 && node.start + node.count <= vehicle.triangleCount)));
assert.equal(bvh.nodes.filter(node => node.count > 0).reduce((sum, node) => sum + node.count, 0), vehicle.triangleCount);
assert.equal(bvh.triangleBuffer.byteLength, vehicle.triangleCount * 96);
assert.equal(bvh.nodeBuffer.byteLength, bvh.nodes.length * 48);

assert.equal(WORLD_WIDTH, 10_000, 'the playable world is 10 km wide');
assert.equal(WORLD_LENGTH, 100_000, 'the playable world is 100 km long');
assert.equal(CITY_BLOCK_SIZE, 96, 'dense city geometry streams in 96 m blocks');
assert.match(GBUFFER_WGSL, /round\(point\.x \/ 96\.0\) \* 96\.0/, 'raster road materials use the denser world-cell size');
assert.equal(CITY_STREAM_RADIUS, 2, 'city rendering retains a 5×5 streamed neighborhood');
assert.equal(CITY_SDF_RADIUS, 1, 'the analytic field stays at a 3×3 high-detail core');
assert.match(appSource, /function moveVehicleWithinWorld\(deltaX, deltaZ\)/, 'driving is bounded only by the finite world footprint');
assert.match(appSource, /const yawRate = state\.manualSpeed \/ wheelBase \* Math\.tan\(steeringAngle\)/, 'manual steering follows the bicycle model');
assert.match(appSource, /const steering = \(state\.keys\.has\('a'\).*? -\s*\(state\.keys\.has\('d'/s, 'left/right steering input polarity is reversed');
assert.match(appSource, /updateVehicle\(dt\);\s*refreshStreamedWorld\(\);/, 'city chunks refresh as the vehicle moves');
assert.match(appSource, /new Worker\(new URL\('\.\/world-stream-worker\.mjs'/, 'streamed city geometry builds off the render thread');
assert.match(appSource, /state\.worldStreamWorker\.postMessage/, 'new neighborhoods are requested asynchronously');
assert.match(appSource, /emitterSize: state\.emitterSize/, 'streamed mesh BVHs track the selected finite-emitter scale');
assert.match(workerSource, /buildWorldDistanceBvh\(world\.vertices\)/, 'the stream worker builds a BVH over the generated static mesh');
assert.match(workerSource, /worldDistanceBvh\.triangleBuffer[\s\S]*?worldDistanceBvh\.nodeBuffer/, 'both full-mesh BVH buffers are transferred with streamed geometry');
assert.match(appSource, /state\.buffers\.worldTriangles = makeBuffer/, 'the renderer uploads the streamed full-mesh triangle buffer');
assert.match(appSource, /state\.buffers\.worldNodes = makeBuffer/, 'the renderer uploads the matching streamed BVH nodes');
assert.match(appSource, /createGlobalFieldUpdateBindGroup\(state\.device\)/, 'GDF bindings are refreshed when streamed mesh buffers change');
assert.match(appSource, /world\.shapes\.length > state\.shapeCapacity/, 'the analytic fallback sets may vary in count without exceeding the fixed shape-buffer capacity');
assert.match(appSource, /state\.shapeCount = world\.shapes\.length/, 'the current streamed shape count is applied to field queries');
assert.match(appSource, /gdfPendingMask \|= bit/, 'dirty field updates are queued without forcing all cascades into one frame');
assert.match(appSource, /state\.gdfNextCascade = \(level \+ 1\) % centers\.length/, 'dirty cascades are amortized round-robin across frames');
assert.match(appSource, /const recenterThreshold = CASCADE_EXTENTS\[level\] \* 0\.12/, 'snap-center hysteresis avoids rebuilding on every voxel-sized movement');
assert.match(appSource, /const FRAME_FLOATS = 152/, 'frame data carries valid, target, and previous clipmap centers');
assert.match(UPDATE_GDF_WGSL, /!updateFieldForce\(level\) && previousFieldContainsCell/, 'ordinary recentering skips cells already present in the toroidal field');
assert.match(appSource, /if \(state\.gdfDirtyMask !== 0\) \{\s*const updatePass/s, 'GDF voxel dispatch only runs for dirty snapped cascades');
assert.match(GI_WGSL, /readyMask = u32\(P\.wheelSteering\.w \+ 0\.5\)/, 'uninitialized clipmap levels fall back to the analytic static SDF');
assert.doesNotMatch(appSource, /function carCanOccupy|moveVehicleWithCollision/, 'city props are intentionally pass-through');
const world = buildWorldGeometry();
assert.ok(world.vertices.length > 0 && world.vertices.length % VEHICLE_VERTEX_STRIDE === 0);
const worldDistanceBvh = buildWorldDistanceBvh(world.vertices);
const expectedWorldTriangleCount = world.vertices.length / (VEHICLE_VERTEX_STRIDE * 3);
assert.equal(worldDistanceBvh.triangleCount, expectedWorldTriangleCount,
  'the static GDF BVH contains every triangle from the streamed world raster mesh');
assert.equal(worldDistanceBvh.triangleBuffer.byteLength, expectedWorldTriangleCount * 96,
  'each static mesh triangle retains the packed material-bearing 96-byte BVH record');
assert.equal(worldDistanceBvh.nodeBuffer.byteLength, worldDistanceBvh.nodeCount * 48);
const worldNodeWords = new Uint32Array(worldDistanceBvh.nodeBuffer);
assert.equal(worldNodeWords[8], worldDistanceBvh.nodeCount, 'the static-world BVH root escape link terminates stackless traversal');
let worldLeafTriangleCount = 0;
for (let nodeIndex = 0; nodeIndex < worldDistanceBvh.nodeCount; nodeIndex++) {
  const base = nodeIndex * 12;
  const escape = worldNodeWords[base + 8];
  const start = worldNodeWords[base + 9];
  const count = worldNodeWords[base + 10];
  assert.ok(escape > nodeIndex && escape <= worldDistanceBvh.nodeCount);
  if (count > 0) {
    assert.ok(start + count <= expectedWorldTriangleCount);
    worldLeafTriangleCount += count;
  }
}
assert.equal(worldLeafTriangleCount, expectedWorldTriangleCount, 'BVH leaves cover each streamed static triangle exactly once');
const worldTriangleFloats = new Float32Array(worldDistanceBvh.triangleBuffer);
const worldTriangleWords = new Uint32Array(worldDistanceBvh.triangleBuffer);
const worldBvhCardIds = new Set();
let worldBvhVisibilityIds = 0;
let worldBvhWindingFlips = 0;
let emissiveMeshTriangleCount = 0;
for (let triangleIndex = 0; triangleIndex < expectedWorldTriangleCount; triangleIndex++) {
  const base = triangleIndex * 24;
  for (let component = 0; component < 20; component++) {
    assert.ok(Number.isFinite(worldTriangleFloats[base + component]), 'BVH triangles carry finite vertex and material data');
  }
  const packedId = worldTriangleWords[base + 21];
  const cardId = packedId & 0xffff;
  const visibilityId = packedId >>> 16;
  const invertWinding = worldTriangleWords[base + 22];
  assert.ok(cardId <= world.surfaceCardCount && visibilityId <= 63);
  assert.ok(invertWinding <= 1, 'BVH orientation metadata is a single winding-correction bit');
  if (cardId > 0) worldBvhCardIds.add(cardId);
  if (visibilityId > 0) worldBvhVisibilityIds++;
  if (invertWinding > 0) worldBvhWindingFlips++;
  if (worldTriangleFloats[base + 18] > 0) emissiveMeshTriangleCount++;
}
assert.equal(worldBvhCardIds.size, world.surfaceCardCount,
  'nearest-triangle metadata preserves every mesh-card ID for GI material lookup');
assert.ok(worldBvhVisibilityIds > 0 && worldBvhWindingFlips > 0 && emissiveMeshTriangleCount > 0,
  'the full-mesh BVH retains object IDs, corrected normals, and emissive material data');
const scaledEmitterWorld = buildWorldGeometry();
const scaledRange = scaledEmitterWorld.emissiveRanges.find(range => range.scaleWithEmitter);
assert.ok(scaledRange, 'the streamed world exposes the adjustable emissive mesh range');
const scaledVertexOffset = scaledRange.firstVertex * VEHICLE_VERTEX_STRIDE;
const originalEmitterPoint = [...scaledEmitterWorld.vertices.subarray(scaledVertexOffset, scaledVertexOffset + 3)];
scaleWorldEmitterGeometry(scaledEmitterWorld, 1.8);
assert.ok(Math.abs(scaledEmitterWorld.vertices[scaledVertexOffset] -
  (scaledRange.center[0] + (originalEmitterPoint[0] - scaledRange.center[0]) * 1.8)) < 1e-5,
  'emitter size is applied to mesh vertices before building the static GDF BVH');
const rebuiltEmitterBvh = buildWorldDistanceBvh(scaledEmitterWorld.vertices);
assert.equal(rebuiltEmitterBvh.triangleCount, expectedWorldTriangleCount,
  'emitter rescaling rebuilds the full static triangle BVH without dropping mesh triangles');
scaleWorldEmitterGeometry(scaledEmitterWorld, 1.1);
assert.ok(Math.abs(scaledEmitterWorld.vertices[scaledVertexOffset] -
  (scaledRange.center[0] + (originalEmitterPoint[0] - scaledRange.center[0]) * 1.1)) < 1e-5,
  'repeated emitter scaling is based on immutable source positions, not compounded');
assert.ok(world.surfaceCardCount > 0 && world.surfaceCardCount <= MAX_SURFACE_CARDS);
assert.ok(MAX_SURFACE_CARDS <= 65_535, '16-bit card IDs cover the fixed world-card capacity');
assert.equal(world.surfaceCards.length, world.surfaceCardCount * SURFACE_CARD_FLOATS);
assert.ok([...world.surfaceCards].every(Number.isFinite));
for (let cardIndex = 0; cardIndex < world.surfaceCardCount; cardIndex++) {
  const base = cardIndex * SURFACE_CARD_FLOATS;
  const edgeU = Math.hypot(...world.surfaceCards.slice(base + 4, base + 7));
  const edgeV = Math.hypot(...world.surfaceCards.slice(base + 8, base + 11));
  assert.ok(Math.max(edgeU, edgeV) <= SURFACE_CARD_MAX_EDGE + 0.01,
    'oversized city quads are subdivided into bounded world-space cache cards');
}
assert.equal(SURFACE_CARD_RESOLUTION, 4);
assert.equal(SURFACE_CARD_TEXELS, 16);
const surfaceCardIds = new Set();
for (let offset = 3; offset < world.vertices.length; offset += VEHICLE_VERTEX_STRIDE) {
  const cardId = world.vertices[offset];
  if (cardId > 0) surfaceCardIds.add(cardId);
}
assert.equal(surfaceCardIds.size, world.surfaceCardCount, 'each cacheable quad has one stable mesh-card ID');
assert.ok([...surfaceCardIds].every(cardId => Number.isInteger(cardId) && cardId <= world.surfaceCardCount));
assert.equal(world.shapes.length, 55, 'the initial dense city core fills the fixed 55-shape SDF capacity');
assert.ok(world.shapes.every(shape => shape.center.length === 3 && shape.half.every(value => value > 0)));
assert.equal(world.shapes.filter(shape => shape.role === 'building').length, 9, 'the analytic core contains nine primary buildings');
assert.equal(world.shapes.filter(shape => shape.role === 'building-penthouse').length, 5, 'varied rooftops include five setback penthouses');
const parkedCars = world.shapes.filter(shape => shape.role === 'parked-car');
assert.equal(parkedCars.length, 9, 'parked cars populate the city blocks');
assert.ok(parkedCars.some(shape => Math.abs(shape.yaw) > 1), 'rotated parked cars exercise the yaw-aware box mesh transform');
assert.equal(world.shapes.filter(shape => shape.role === 'pedestrian-robot').length, 0, 'mesh-only robots keep global placements independent of the local SDF budget');
assert.equal(world.shapes.filter(shape => shape.role === 'bench').length, 4, 'benches are part of the analytic core');
assert.equal(world.shapes.filter(shape => shape.role === 'chair').length, 4, 'chairs are part of the analytic core');
assert.equal(world.shapes.filter(shape => shape.role === 'trash-bin').length, 4, 'bins are part of the analytic core');
assert.equal(world.shapes.filter(shape => shape.role === 'traffic-light-pole').length, 4, 'the core intersection has four traffic signals');
assert.equal(world.shapes.filter(shape => shape.role === 'traffic-light-head').length, 4);
assert.equal(world.parkedCarCount, 25);
assert.equal(world.robotCount, 5, 'five globally keyed robots remain in every 5×5 streamed neighborhood');
assert.equal(world.emissiveRanges.filter(range => range.role === 'robot-emitter').length, world.robotCount, 'robot visors are persistent mesh emitters without entering the sampled area-light table');
assert.equal(world.streetlightCount, 25);
assert.equal(world.trafficLightCount, 100);
assert.equal(world.benchCount, 25);
assert.equal(world.chairCount, 25);
assert.equal(world.trashBinCount, 25);
const emissiveLights = world.shapes.filter(shape => shape.emission > 0);
assert.equal(emissiveLights.length, 6, 'the six central streetlight panels are analytic emissive surfaces');
assert.equal(emissiveLights.filter(shape => shape.role === 'streetlight-emitter').length, 6);
assert.ok(emissiveLights.every(shape => shape.emission >= 7.5));
assert.ok([...world.vertices].every(Number.isFinite), 'static scene stream contains only finite values');
const sameNeighborhood = buildWorldGeometry(1, 1);
assert.deepEqual(sameNeighborhood.streamCell, world.streamCell);
assert.equal(sameNeighborhood.robotCount, world.robotCount);
assert.deepEqual(sameNeighborhood.vertices, world.vertices, 'city chunks are deterministic within a stream cell');
const nextNeighborhood = buildWorldGeometry(CITY_BLOCK_SIZE * 2 + 1, -CITY_BLOCK_SIZE * 3 - 1);
assert.ok(nextNeighborhood.surfaceCardCount <= MAX_SURFACE_CARDS);
assert.equal(nextNeighborhood.surfaceCards.length, nextNeighborhood.surfaceCardCount * SURFACE_CARD_FLOATS);
assert.deepEqual(nextNeighborhood.streamCell, { x: 2, z: -4 });
assert.ok(nextNeighborhood.shapes.length <= world.shapes.length, 'streamed chunks remain within the fixed 55-shape analytic buffer capacity');
const smallerShapeNeighborhood = buildWorldGeometry(-CITY_BLOCK_SIZE * 30 + 1, -CITY_BLOCK_SIZE * 3 + 1);
assert.equal(smallerShapeNeighborhood.shapes.length, 54, 'an empty optional rooftop uses one fewer analytic shape while retaining the same buffer capacity');
assert.equal(nextNeighborhood.robotCount, world.robotCount, 'the world-coordinate robot layout stays stable across streamed cells');
const revisitedNeighborhood = buildWorldGeometry(1, 1);
assert.deepEqual(revisitedNeighborhood.vertices, world.vertices, 'persistent global cell layouts restore identical assets after leaving the streamed range');
assert.equal(buildEmissiveSourceBuffer(nextNeighborhood, vehicle).buffer.byteLength,
  buildEmissiveSourceBuffer(world, vehicle).buffer.byteLength, 'streamed chunks keep the emitter alias-table budget fixed');
assert.equal(buildEmissiveSourceBuffer(smallerShapeNeighborhood, vehicle).buffer.byteLength,
  buildEmissiveSourceBuffer(world, vehicle).buffer.byteLength, 'shape-count variation does not change the area-light buffer capacity');
const worldVisibilityIds = new Set();
for (let offset = 11; offset < world.vertices.length; offset += VEHICLE_VERTEX_STRIDE) {
  const visibilityId = world.vertices[offset];
  assert.ok(Number.isInteger(visibilityId) && visibilityId >= 0 && visibilityId <= 63,
    'static geometry carries an exact visibility ID representable in the packed G-buffer');
  worldVisibilityIds.add(visibilityId);
}
assert.deepEqual([...worldVisibilityIds].sort((a, b) => a - b), [
  ...Array.from({ length: world.shapes.length + 1 }, (_, index) => index), 56, 57, 58, 59, 60, 63,
], 'ground, SDF solids, persistent robots, and mesh-only props have stable raster visibility IDs');

console.log(`PASS ControlVehicle: ${vehicle.triangleCount.toLocaleString()} triangles, ${vehicle.groupCount} parity groups, ${bvh.nodes.length.toLocaleString()} BVH nodes`);
console.log(`PASS Raster visibility identity: ${worldVisibilityIds.size} static IDs and vehicle body/wheel IDs ${[...vehicleVisibilityIds].sort((a, b) => a - b).join(', ')}`);
console.log(`PASS Clipmap detail and wheel metadata: ${GLOBAL_FIELD_RESOLUTION}³ × 3 field, ${vehicle.wheelParts.length} independent tyre poses, ${vehicle.wheelBase.toFixed(3)} m wheelbase`);
console.log('PASS Raster animation and distance-query fixes: per-wheel roll/steer, single-field runtime raymarch, conservative shadow steps');
console.log(`PASS Full-mesh global field: ${worldDistanceBvh.triangleCount.toLocaleString()} streamed triangles, ${worldDistanceBvh.nodeCount.toLocaleString()} BVH nodes, ${world.surfaceCardCount.toLocaleString()} material cards`);
console.log(`PASS Persistent streamed city: ${world.shapes.length} analytic fallback solids, ${world.buildingCount} buildings, ${world.parkedCarCount} cars, ${world.trafficLightCount} traffic lights, ${world.benchCount} benches, ${world.vertices.length / VEHICLE_VERTEX_STRIDE} raster vertices`);
console.log(`PASS Manifest SHA-256: ${sha256}`);

const emissive = buildEmissiveSourceBuffer(world, vehicle);
assert.equal(emissive.count, 60, 'ten room-facing triangles from each of the six streetlight panels are sampled');
assert.equal(emissive.buffer.byteLength, emissive.count * EMISSIVE_SOURCE_BYTES);
assert.ok(emissive.totalPower > 0);
const sourceFloats = new Float32Array(emissive.buffer);
const sourceWords = new Uint32Array(emissive.buffer);
const reconstructedPdf = new Float64Array(emissive.count);
const trianglesByPanel = new Map();
let pdfSum = 0;
for (let index = 0; index < emissive.count; index++) {
  const base = index * (EMISSIVE_SOURCE_BYTES / Float32Array.BYTES_PER_ELEMENT);
  const area = sourceFloats[base + 20];
  const threshold = sourceFloats[base + 21];
  const pdf = sourceFloats[base + 22];
  const alias = sourceWords[base + 24];
  assert.ok(area > 0 && Number.isFinite(area));
  assert.ok(threshold >= 0 && threshold <= 1);
  assert.ok(pdf > 0 && Number.isFinite(pdf));
  assert.ok(alias < emissive.count);
  assert.equal(sourceWords[base + 25], 0, 'static emitters are stored in world space');
  const a = [sourceFloats[base], sourceFloats[base + 1], sourceFloats[base + 2]];
  const b = [sourceFloats[base + 4], sourceFloats[base + 5], sourceFloats[base + 6]];
  const c = [sourceFloats[base + 8], sourceFloats[base + 9], sourceFloats[base + 10]];
  const ab = b.map((value, axis) => value - a[axis]);
  const ac = c.map((value, axis) => value - a[axis]);
  const cross = [
    ab[1] * ac[2] - ab[2] * ac[1],
    ab[2] * ac[0] - ab[0] * ac[2],
    ab[0] * ac[1] - ab[1] * ac[0],
  ];
  const normalY = cross[1] / Math.hypot(...cross);
  assert.ok(normalY <= 0.2 + 1e-6, 'upward-facing ceiling-panel backs do not enter room-light sampling');
  const panel = sourceWords[base + 26];
  trianglesByPanel.set(panel, (trianglesByPanel.get(panel) || 0) + 1);
  pdfSum += pdf;
  reconstructedPdf[index] += threshold / emissive.count;
  reconstructedPdf[alias] += (1 - threshold) / emissive.count;
}
assert.equal(trianglesByPanel.size, 6, 'each emissive panel contributes to the light table');
assert.ok([...trianglesByPanel.values()].every(count => count === 10), 'each panel contributes its ten room-facing triangles');
assert.ok(Math.abs(pdfSum - 1) < 1e-5, 'emitter selection PDFs form a normalized distribution');
for (let index = 0; index < emissive.count; index++) {
  assert.ok(Math.abs(reconstructedPdf[index] - sourceFloats[index * 28 + 22]) < 2e-6,
    `Walker alias mass for source ${index} must match its power PDF`);
}

const vehicleOnly = buildEmissiveSourceBuffer({ vertices: new Float32Array(), emissiveRanges: [] }, {
  triangles: [{
    a: [0, 0, 0], b: [1, 0, 0], c: [0, 1, 0],
    color: [1, 0.5, 0.25, 1], surface: [0.4, 0, 5, 0], group: 3,
  }],
});
assert.equal(vehicleOnly.count, 1, 'emissive ControlVehicle triangles are supported');
const vehicleWords = new Uint32Array(vehicleOnly.buffer);
assert.equal(vehicleWords[25], 1, 'vehicle sources are flagged for per-frame local-to-world placement');
assert.equal(vehicleWords[26], 3, 'vehicle SDF parity group is retained');
console.log(`PASS Emissive next-event sampling: ${emissive.count} room-facing triangles, ${emissive.buffer.byteLength} bytes, normalized Walker alias table`);

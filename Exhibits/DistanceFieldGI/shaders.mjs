const FRAME_TYPES = /* wgsl */ `
struct FrameUniforms {
    viewProjection: mat4x4f,
    cameraPosition: vec4f,
    vehicle: vec4f,
    center0: vec4f,
    center1: vec4f,
    center2: vec4f,
    cascadeExtents: vec4f,
    vehicleBoundsMin: vec4f,
    vehicleBoundsMax: vec4f,
    viewport: vec4f,
    modeQuality: vec4f,
    sun: vec4f,
    sky: vec4f,
    gi: vec4f,
    environment: vec4f,
    lightControls: vec4f,
    emissiveParams: vec4f,
    areaLightPosition: vec4f,
    areaLightColorRadius: vec4f,
    previousViewProjection: mat4x4f,
    previousVehicle: vec4f,
    wheelFrontLeft: vec4f,
    wheelFrontRight: vec4f,
    wheelRearLeft: vec4f,
    wheelRearRight: vec4f,
    wheelSteering: vec4f,
    targetCenter0: vec4f,
    targetCenter1: vec4f,
    targetCenter2: vec4f,
    previousFieldCenter0: vec4f,
    previousFieldCenter1: vec4f,
    previousFieldCenter2: vec4f,
};
struct AssetParams {
    minimum: vec4f,
    maximum: vec4f,
    dimensions: vec4u,
};
struct StaticShape {
    center: vec4f,
    halfExtent: vec4f,
    albedo: vec4f,
    material: vec4f,
};
const VEHICLE_DYNAMIC_GI_RADIUS: f32 = 24.0;
@group(0) @binding(0) var<uniform> P: FrameUniforms;
fn surfaceVisibilityId(surfaceCode: f32) -> u32 {
    let dynamicSurface = surfaceCode > 1.5;
    let classBase = select(1.0, 2.0, dynamicSurface);
    return u32(round(max(surfaceCode - classBase, 0.0) * 256.0));
}
fn sameSurfaceVisibility(a: f32, b: f32) -> bool {
    return (a > 1.5) == (b > 1.5) && surfaceVisibilityId(a) == surfaceVisibilityId(b);
}
fn vehicleMoved() -> bool {
    let carDelta = P.vehicle.xz - P.previousVehicle.xz;
    let headingAgreement = dot(vec2f(cos(P.vehicle.w), sin(P.vehicle.w)),
                               vec2f(cos(P.previousVehicle.w), sin(P.previousVehicle.w)));
    return dot(carDelta, carDelta) > 1e-5 || headingAgreement < 0.99999;
}
fn wrapFieldCell(value: i32, resolution: i32) -> i32 {
    let wrapped = value % resolution;
    return (wrapped + resolution) % resolution;
}
fn fieldCenter(level: u32) -> vec3f {
    if (level == 0u) { return P.center0.xyz; }
    if (level == 1u) { return P.center1.xyz; }
    return P.center2.xyz;
}
fn updateFieldCenter(level: u32) -> vec3f {
    if (level == 0u) { return P.targetCenter0.xyz; }
    if (level == 1u) { return P.targetCenter1.xyz; }
    return P.targetCenter2.xyz;
}
fn previousFieldCenter(level: u32) -> vec3f {
    if (level == 0u) { return P.previousFieldCenter0.xyz; }
    if (level == 1u) { return P.previousFieldCenter1.xyz; }
    return P.previousFieldCenter2.xyz;
}
fn updateFieldForce(level: u32) -> bool {
    if (level == 0u) { return P.targetCenter0.w > 0.5; }
    if (level == 1u) { return P.targetCenter1.w > 0.5; }
    return P.targetCenter2.w > 0.5;
}
fn fieldExtent(level: u32) -> f32 {
    if (level == 0u) { return P.cascadeExtents.x; }
    if (level == 1u) { return P.cascadeExtents.y; }
    return P.cascadeExtents.z;
}
fn fieldLevelFor(position: vec3f) -> u32 {
    let r0 = max(max(abs(position.x - P.center0.x), abs(position.y - P.center0.y)), abs(position.z - P.center0.z));
    let r1 = max(max(abs(position.x - P.center1.x), abs(position.y - P.center1.y)), abs(position.z - P.center1.z));
    if (r0 < P.cascadeExtents.x * 0.48) { return 0u; }
    if (r1 < P.cascadeExtents.y * 0.48) { return 1u; }
    return 2u;
}
fn voxelSize(level: u32) -> f32 {
    return fieldExtent(level) / P.cascadeExtents.w;
}
fn worldToVehicle(position: vec3f) -> vec3f {
    let delta = position - P.vehicle.xyz;
    let c = cos(P.vehicle.w);
    let s = sin(P.vehicle.w);
    return vec3f(c * delta.x - s * delta.z, delta.y, s * delta.x + c * delta.z);
}
fn sdBox(point: vec3f, center: vec3f, halfExtent: vec3f) -> f32 {
    let q = abs(point - center) - halfExtent;
    return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}
fn sdShape(point: vec3f, shape: StaticShape) -> f32 {
    let delta = point - shape.center.xyz;
    let c = cos(shape.center.w);
    let s = sin(shape.center.w);
    let local = vec3f(c * delta.x - s * delta.z, delta.y, s * delta.x + c * delta.z);
    return sdBox(local, vec3f(0.0), shape.halfExtent.xyz);
}
fn floorAlbedo(point: vec3f) -> vec3f {
    let distanceToXStreet = abs(point.x - round(point.x / 96.0) * 96.0);
    let distanceToZStreet = abs(point.z - round(point.z / 96.0) * 96.0);
    let distanceToStreet = min(distanceToXStreet, distanceToZStreet);
    var color = vec3f(0.19, 0.23, 0.24);
    if (distanceToStreet < 7.2) {
        color = vec3f(0.075, 0.09, 0.095);
        let alongZDash = fract(point.z / 8.0) < 0.52;
        let alongXDash = fract(point.x / 8.0) < 0.52;
        let xCenterLine = distanceToXStreet < 0.085 && alongZDash;
        let zCenterLine = distanceToZStreet < 0.085 && alongXDash;
        let xLaneEdge = abs(distanceToXStreet - 3.35) < 0.075;
        let zLaneEdge = abs(distanceToZStreet - 3.35) < 0.075;
        if (xCenterLine || zCenterLine) { color = vec3f(0.91, 0.65, 0.24); }
        if (xLaneEdge || zLaneEdge) { color = vec3f(0.68, 0.73, 0.68); }
        let zebraZ = abs(distanceToZStreet - 9.0) < 2.4 && fract(point.z / 1.05) < 0.48 && distanceToXStreet < 7.0;
        let zebraX = abs(distanceToXStreet - 9.0) < 2.4 && fract(point.x / 1.05) < 0.48 && distanceToZStreet < 7.0;
        if (zebraZ || zebraX) { color = vec3f(0.82, 0.85, 0.78); }
    } else if (distanceToStreet < 11.5) {
        color = vec3f(0.31, 0.33, 0.31);
    }
    // Keep low-frequency surface variation only, so the city grid does not
    // acquire a high-frequency checker that aliases into false stripes in GI.
    let broadVariation = 0.985 + 0.015 * sin(point.x * 0.013 + point.z * 0.004) * sin(point.z * 0.011 - point.x * 0.006);
    return color * broadVariation;
}
`;

export const BAKE_WGSL = /* wgsl */ `
struct BakeParams {
    minimum: vec4f,
    maximum: vec4f,
    dimensions: vec4u,
    counts: vec4u,
};
struct Triangle {
    a: vec4f,
    b: vec4f,
    c: vec4f,
    color: vec4f,
    surface: vec4f,
    metadata: vec4u,
};
struct BvhNode {
    minimum: vec4f,
    maximum: vec4f,
    metadata: vec4u,
};
@group(0) @binding(0) var<uniform> B: BakeParams;
@group(0) @binding(1) var<storage, read> Triangles: array<Triangle>;
@group(0) @binding(2) var<storage, read> Nodes: array<BvhNode>;
@group(0) @binding(3) var DistanceOutput: texture_storage_3d<r32float, write>;
@group(0) @binding(4) var MaterialOutput: texture_storage_3d<rgba8unorm, write>;
@group(0) @binding(5) var SurfaceOutput: texture_storage_3d<rgba8unorm, write>;
fn pointAabbDistanceSquared(point: vec3f, minimum: vec3f, maximum: vec3f) -> f32 {
    let outside = max(max(minimum - point, point - maximum), vec3f(0.0));
    return dot(outside, outside);
}
fn rayIntersectsAabb(origin: vec3f, direction: vec3f, minimum: vec3f, maximum: vec3f) -> bool {
    var nearDistance = -1e20;
    var farDistance = 1e20;
    for (var axis = 0u; axis < 3u; axis++) {
        let inverseDirection = 1.0 / direction[axis];
        let a = (minimum[axis] - origin[axis]) * inverseDirection;
        let b = (maximum[axis] - origin[axis]) * inverseDirection;
        nearDistance = max(nearDistance, min(a, b));
        farDistance = min(farDistance, max(a, b));
    }
    return farDistance >= max(nearDistance, 0.0);
}
fn pointTriangleDistanceSquared(point: vec3f, a: vec3f, b: vec3f, c: vec3f) -> f32 {
    let ab = b - a;
    let ac = c - a;
    let ap = point - a;
    let d1 = dot(ab, ap);
    let d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) { return dot(ap, ap); }
    let bp = point - b;
    let d3 = dot(ab, bp);
    let d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) { return dot(bp, bp); }
    let vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        let v = d1 / max(d1 - d3, 1e-20);
        let delta = point - (a + v * ab);
        return dot(delta, delta);
    }
    let cp = point - c;
    let d5 = dot(ab, cp);
    let d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) { return dot(cp, cp); }
    let vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        let w = d2 / max(d2 - d6, 1e-20);
        let delta = point - (a + w * ac);
        return dot(delta, delta);
    }
    let va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        let w = (d4 - d3) / max((d4 - d3) + (d5 - d6), 1e-20);
        let delta = point - (b + w * (c - b));
        return dot(delta, delta);
    }
    let denominator = 1.0 / max(va + vb + vc, 1e-20);
    let v = vb * denominator;
    let w = vc * denominator;
    let delta = point - (a + ab * v + ac * w);
    return dot(delta, delta);
}
fn rayTriangleDistance(origin: vec3f, direction: vec3f, triangle: Triangle) -> f32 {
    let edge1 = triangle.b.xyz - triangle.a.xyz;
    let edge2 = triangle.c.xyz - triangle.a.xyz;
    let p = cross(direction, edge2);
    let determinant = dot(edge1, p);
    if (abs(determinant) < 1e-8) { return -1.0; }
    let inverseDeterminant = 1.0 / determinant;
    let t = origin - triangle.a.xyz;
    let u = dot(t, p) * inverseDeterminant;
    if (u < 0.0 || u > 1.0) { return -1.0; }
    let q = cross(t, edge1);
    let v = dot(direction, q) * inverseDeterminant;
    if (v < 0.0 || u + v > 1.0) { return -1.0; }
    let distance = dot(edge2, q) * inverseDeterminant;
    return select(-1.0, distance, distance > 1e-5);
}
@compute @workgroup_size(4, 4, 4)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (any(id >= B.dimensions.xyz)) { return; }
    let dimensions = vec3f(B.dimensions.xyz);
    let point = B.minimum.xyz + (vec3f(id) + vec3f(0.5)) * (B.maximum.xyz - B.minimum.xyz) / dimensions;
    var nearestSquared = 1e30;
    var nearestTriangle = 0u;
    var nodeIndex = 0u;
    loop {
        if (nodeIndex >= B.counts.y) { break; }
        let node = Nodes[nodeIndex];
        if (pointAabbDistanceSquared(point, node.minimum.xyz, node.maximum.xyz) > nearestSquared) {
            nodeIndex = node.metadata.x;
            continue;
        }
        if (node.metadata.z == 0u) {
            nodeIndex += 1u;
            continue;
        }
        for (var item = 0u; item < node.metadata.z; item++) {
            let triangleIndex = node.metadata.y + item;
            let triangle = Triangles[triangleIndex];
            let distanceSquared = pointTriangleDistanceSquared(point, triangle.a.xyz, triangle.b.xyz, triangle.c.xyz);
            if (distanceSquared < nearestSquared) {
                nearestSquared = distanceSquared;
                nearestTriangle = triangleIndex;
            }
        }
        nodeIndex = node.metadata.x;
    }
    var crossings: array<u32, 16>;
    for (var group = 0u; group < 16u; group++) { crossings[group] = 0u; }
    let rayOrigin = point + vec3f(0.00013, 0.000037, 0.000071);
    let rayDirection = normalize(vec3f(1.0, 0.000173, 0.000091));
    nodeIndex = 0u;
    loop {
        if (nodeIndex >= B.counts.y) { break; }
        let node = Nodes[nodeIndex];
        if (!rayIntersectsAabb(rayOrigin, rayDirection, node.minimum.xyz, node.maximum.xyz)) {
            nodeIndex = node.metadata.x;
            continue;
        }
        if (node.metadata.z == 0u) {
            nodeIndex += 1u;
            continue;
        }
        for (var item = 0u; item < node.metadata.z; item++) {
            let triangle = Triangles[node.metadata.y + item];
            let hitDistance = rayTriangleDistance(rayOrigin, rayDirection, triangle);
            if (hitDistance > 0.0 && triangle.metadata.x < 16u) {
                crossings[triangle.metadata.x] += 1u;
            }
        }
        nodeIndex = node.metadata.x;
    }
    var inside = false;
    for (var group = 0u; group < min(B.counts.z, 16u); group++) {
        if ((crossings[group] & 1u) != 0u) { inside = true; }
    }
    let unsignedDistance = sqrt(max(nearestSquared, 0.0));
    let signedDistance = select(unsignedDistance, -unsignedDistance, inside);
    let nearest = Triangles[nearestTriangle];
    textureStore(DistanceOutput, vec3i(id), vec4f(signedDistance, 0.0, 0.0, 0.0));
    textureStore(MaterialOutput, vec3i(id), vec4f(nearest.color.rgb, clamp(nearest.color.a / 8.0, 0.0, 1.0)));
    textureStore(SurfaceOutput, vec3i(id), vec4f(nearest.surface.x, nearest.surface.y, 0.0, 1.0));
}
`;

const VEHICLE_SAMPLE_WGSL = /* wgsl */ `
@group(0) @binding(2) var VehicleDistance: texture_3d<f32>;
@group(0) @binding(3) var<uniform> Asset: AssetParams;
@group(0) @binding(4) var<storage, read> Shapes: array<StaticShape>;
fn sampleVehicleDistance(localPosition: vec3f) -> f32 {
    if (any(localPosition < Asset.minimum.xyz) || any(localPosition > Asset.maximum.xyz)) { return 1e4; }
    let dimensions = vec3f(Asset.dimensions.xyz);
    let coordinate = (localPosition - Asset.minimum.xyz) / (Asset.maximum.xyz - Asset.minimum.xyz) * dimensions - vec3f(0.5);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let limit = vec3i(Asset.dimensions.xyz) - vec3i(1);
    let lo = clamp(base, vec3i(0), limit);
    let hi = clamp(base + vec3i(1), vec3i(0), limit);
    let x00 = mix(textureLoad(VehicleDistance, vec3i(lo.x, lo.y, lo.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, lo.y, lo.z), 0).r, fraction.x);
    let x10 = mix(textureLoad(VehicleDistance, vec3i(lo.x, hi.y, lo.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, hi.y, lo.z), 0).r, fraction.x);
    let x01 = mix(textureLoad(VehicleDistance, vec3i(lo.x, lo.y, hi.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, lo.y, hi.z), 0).r, fraction.x);
    let x11 = mix(textureLoad(VehicleDistance, vec3i(lo.x, hi.y, hi.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, hi.y, hi.z), 0).r, fraction.x);
    return mix(mix(x00, x10, fraction.y), mix(x01, x11, fraction.y), fraction.z);
}
fn staticDistance(position: vec3f) -> f32 {
    var distance = position.y;
    let count = u32(P.gi.w + 0.5);
    for (var index = 0u; index < count; index++) {
        let shape = Shapes[index];
        let boundsDelta = abs(position - shape.center.xyz) -
            vec3f(shape.material.z, shape.halfExtent.y, shape.material.w);
        let outside = max(boundsDelta, vec3f(0.0));
        let nearestPossible = max(distance, 0.0);
        if (dot(outside, outside) <= nearestPossible * nearestPossible) {
            distance = min(distance, sdShape(position, shape));
        }
    }
    return distance;
}
`;

export const UPDATE_GDF_WGSL = /* wgsl */ `${FRAME_TYPES}
struct WorldTriangle {
    a: vec4f,
    b: vec4f,
    c: vec4f,
    color: vec4f,
    surface: vec4f,
    metadata: vec4u,
};
struct WorldBvhNode {
    minimum: vec4f,
    maximum: vec4f,
    metadata: vec4u,
};
struct WorldGdfParams { counts: vec4u };
@group(0) @binding(1) var GlobalDistanceOutput: texture_storage_3d<r32float, write>;
@group(0) @binding(11) var<storage, read> WorldTriangles: array<WorldTriangle>;
@group(0) @binding(12) var<storage, read> WorldNodes: array<WorldBvhNode>;
@group(0) @binding(13) var<uniform> WorldGdf: WorldGdfParams;
@group(0) @binding(14) var GlobalSurfaceIdOutput: texture_storage_3d<r32uint, write>;
@group(0) @binding(15) var GlobalAlbedoRoughnessOutput: texture_storage_3d<rgba16float, write>;
@group(0) @binding(16) var GlobalEmissionMetallicOutput: texture_storage_3d<rgba16float, write>;
fn pointAabbDistanceSquared(point: vec3f, minimum: vec3f, maximum: vec3f) -> f32 {
    let outside = max(max(minimum - point, point - maximum), vec3f(0.0));
    return dot(outside, outside);
}
fn pointTriangleDistanceSquared(point: vec3f, a: vec3f, b: vec3f, c: vec3f) -> f32 {
    let ab = b - a;
    let ac = c - a;
    let ap = point - a;
    let d1 = dot(ab, ap);
    let d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) { return dot(ap, ap); }
    let bp = point - b;
    let d3 = dot(ab, bp);
    let d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) { return dot(bp, bp); }
    let vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        let v = d1 / max(d1 - d3, 1e-20);
        let delta = point - (a + v * ab);
        return dot(delta, delta);
    }
    let cp = point - c;
    let d5 = dot(ab, cp);
    let d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) { return dot(cp, cp); }
    let vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        let w = d2 / max(d2 - d6, 1e-20);
        let delta = point - (a + w * ac);
        return dot(delta, delta);
    }
    let va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        let w = (d4 - d3) / max((d4 - d3) + (d5 - d6), 1e-20);
        let delta = point - (b + w * (c - b));
        return dot(delta, delta);
    }
    let denominator = 1.0 / max(va + vb + vc, 1e-20);
    let v = vb * denominator;
    let w = vc * denominator;
    let delta = point - (a + ab * v + ac * w);
    return dot(delta, delta);
}
@compute @workgroup_size(4, 4, 4)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let resolution = u32(P.cascadeExtents.w + 0.5);
    if (id.x >= resolution || id.y >= resolution || id.z >= resolution) { return; }
    let dirtyMask = u32(P.wheelSteering.z + 0.5);
    if (dirtyMask == 0u || dirtyMask > 4u) { return; }
    var level = 0u;
    if (dirtyMask == 2u) { level = 1u; }
    if (dirtyMask == 4u) { level = 2u; }
    let extent = fieldExtent(level);
    let cellSize = extent / f32(resolution);
    let center = updateFieldCenter(level);
    let previousCenter = previousFieldCenter(level);
    let centerShift = vec3i(round((center - previousCenter) / cellSize));
    let previousCell = vec3i(i32(id.x), i32(id.y), i32(id.z)) + centerShift;
    let previousFieldContainsCell = all(previousCell >= vec3i(0)) &&
        all(previousCell < vec3i(i32(resolution)));
    if (!updateFieldForce(level) && previousFieldContainsCell) { return; }
    let cell = vec3f(f32(id.x), f32(id.y), f32(id.z)) + vec3f(0.5);
    let worldPosition = center + (cell / f32(resolution) - vec3f(0.5)) * extent;
    var nearestSquared = extent * extent;
    var nearestTriangleIndex = 0u;
    var found = false;
    var nodeIndex = 0u;
    loop {
        if (nodeIndex >= WorldGdf.counts.y) { break; }
        let node = WorldNodes[nodeIndex];
        if (pointAabbDistanceSquared(worldPosition, node.minimum.xyz, node.maximum.xyz) > nearestSquared) {
            nodeIndex = node.metadata.x;
            continue;
        }
        if (node.metadata.z == 0u) {
            nodeIndex += 1u;
            continue;
        }
        for (var item = 0u; item < node.metadata.z; item++) {
            let triangleIndex = node.metadata.y + item;
            let triangle = WorldTriangles[triangleIndex];
            let distanceSquared = pointTriangleDistanceSquared(
                worldPosition, triangle.a.xyz, triangle.b.xyz, triangle.c.xyz,
            );
            if (distanceSquared < nearestSquared) {
                nearestSquared = distanceSquared;
                nearestTriangleIndex = triangleIndex;
                found = true;
            }
        }
        nodeIndex = node.metadata.x;
    }
    var distance = extent;
    var packedSurfaceId = 0u;
    var albedoRoughness = vec4f(0.0, 0.0, 0.0, 1.0);
    var emissionMetallic = vec4f(0.0);
    if (found) {
        let nearest = WorldTriangles[nearestTriangleIndex];
        var faceNormal = cross(nearest.b.xyz - nearest.a.xyz, nearest.c.xyz - nearest.a.xyz);
        if (nearest.metadata.z != 0u) { faceNormal = -faceNormal; }
        let normal = faceNormal / max(length(faceNormal), 1e-12);
        let side = select(-1.0, 1.0, dot(worldPosition - nearest.a.xyz, normal) >= 0.0);
        distance = sqrt(max(nearestSquared, 0.0)) * side;
        packedSurfaceId = nearest.metadata.y;
        albedoRoughness = vec4f(nearest.color.rgb, clamp(nearest.surface.x, 0.025, 1.0));
        emissionMetallic = vec4f(
            nearest.color.rgb * max(nearest.surface.z, 0.0),
            clamp(nearest.surface.y, 0.0, 1.0),
        );
    }
    let centerCell = vec3i(round(center / cellSize));
    let worldCell = centerCell + vec3i(i32(id.x), i32(id.y), i32(id.z)) - vec3i(i32(resolution / 2u));
    let outputCoordinate = vec3i(
        wrapFieldCell(worldCell.x, i32(resolution)),
        wrapFieldCell(worldCell.y, i32(resolution)),
        wrapFieldCell(worldCell.z, i32(resolution)) + i32(level * resolution),
    );
    textureStore(GlobalDistanceOutput, outputCoordinate, vec4f(clamp(distance, -extent, extent), 0.0, 0.0, 0.0));
    textureStore(GlobalSurfaceIdOutput, outputCoordinate, vec4u(packedSurfaceId, 0u, 0u, 0u));
    textureStore(GlobalAlbedoRoughnessOutput, outputCoordinate, albedoRoughness);
    textureStore(GlobalEmissionMetallicOutput, outputCoordinate, emissionMetallic);
}
`;

const GLOBAL_FIELD_SAMPLING = /* wgsl */ `
@group(0) @binding(5) var GlobalDistance: texture_3d<f32>;
@group(0) @binding(6) var VehicleDistance: texture_3d<f32>;
@group(0) @binding(7) var VehicleMaterial: texture_3d<f32>;
@group(0) @binding(8) var VehicleSurface: texture_3d<f32>;
@group(0) @binding(9) var<uniform> Asset: AssetParams;
@group(0) @binding(10) var<storage, read> Shapes: array<StaticShape>;
@group(0) @binding(18) var GlobalSurfaceId: texture_3d<u32>;
@group(0) @binding(19) var GlobalAlbedoRoughness: texture_3d<f32>;
@group(0) @binding(20) var GlobalEmissionMetallic: texture_3d<f32>;
struct GlobalSurfaceData {
    packedId: u32,
    albedoRoughness: vec4f,
    emissionMetallic: vec4f,
};
fn globalStorageCoordinate(coordinate: vec3i, level: u32) -> vec3i {
    let resolution = i32(P.cascadeExtents.w + 0.5);
    let clamped = clamp(coordinate, vec3i(0), vec3i(resolution - 1));
    let centerCell = vec3i(round(fieldCenter(level) / voxelSize(level)));
    let worldCell = centerCell + clamped - vec3i(resolution / 2);
    return vec3i(
        wrapFieldCell(worldCell.x, resolution),
        wrapFieldCell(worldCell.y, resolution),
        wrapFieldCell(worldCell.z, resolution) + i32(level) * resolution,
    );
}
fn loadGlobalCell(coordinate: vec3i, level: u32) -> f32 {
    return textureLoad(GlobalDistance, globalStorageCoordinate(coordinate, level), 0).r;
}
fn sampleGlobalSurfaceData(position: vec3f) -> GlobalSurfaceData {
    let level = fieldLevelFor(position);
    let extent = fieldExtent(level);
    let relative = (position - fieldCenter(level)) / extent + vec3f(0.5);
    if (any(relative < vec3f(0.0)) || any(relative > vec3f(1.0))) {
        return GlobalSurfaceData(0u, vec4f(0.0, 0.0, 0.0, 1.0), vec4f(0.0));
    }
    let coordinate = relative * P.cascadeExtents.w - vec3f(0.5);
    let storageCoordinate = globalStorageCoordinate(vec3i(round(coordinate)), level);
    return GlobalSurfaceData(
        textureLoad(GlobalSurfaceId, storageCoordinate, 0).r,
        textureLoad(GlobalAlbedoRoughness, storageCoordinate, 0),
        textureLoad(GlobalEmissionMetallic, storageCoordinate, 0),
    );
}
fn sampleGlobalLevel(position: vec3f, level: u32) -> f32 {
    let extent = fieldExtent(level);
    let center = fieldCenter(level);
    let relative = (position - center) / extent + vec3f(0.5);
    if (any(relative < vec3f(0.0)) || any(relative > vec3f(1.0))) { return 1e4; }
    let coordinate = relative * P.cascadeExtents.w - vec3f(0.5);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let a = mix(loadGlobalCell(base, level), loadGlobalCell(base + vec3i(1, 0, 0), level), fraction.x);
    let b = mix(loadGlobalCell(base + vec3i(0, 1, 0), level), loadGlobalCell(base + vec3i(1, 1, 0), level), fraction.x);
    let c = mix(loadGlobalCell(base + vec3i(0, 0, 1), level), loadGlobalCell(base + vec3i(1, 0, 1), level), fraction.x);
    let d = mix(loadGlobalCell(base + vec3i(0, 1, 1), level), loadGlobalCell(base + vec3i(1, 1, 1), level), fraction.x);
    return mix(mix(a, b, fraction.y), mix(c, d, fraction.y), fraction.z);
}
fn sampleClipmapLevel(position: vec3f, level: u32) -> f32 {
    let readyMask = u32(P.wheelSteering.w + 0.5);
    if ((readyMask & (1u << level)) == 0u) { return staticDistance(position); }
    return sampleGlobalLevel(position, level);
}
fn sampleGlobalData(position: vec3f) -> vec2f {
    let r0 = max(max(abs(position.x - P.center0.x), abs(position.y - P.center0.y)), abs(position.z - P.center0.z));
    let r1 = max(max(abs(position.x - P.center1.x), abs(position.y - P.center1.y)), abs(position.z - P.center1.z));
    let extent0 = P.cascadeExtents.x;
    let extent1 = P.cascadeExtents.y;
    let voxel0 = voxelSize(0u);
    let voxel1 = voxelSize(1u);
    let voxel2 = voxelSize(2u);
    let blend0Start = extent0 * 0.28;
    let blend0End = extent0 * 0.46;
    if (r0 <= blend0Start) { return vec2f(sampleClipmapLevel(position, 0u), voxel0); }
    if (r0 < blend0End) {
        let blend = smoothstep(blend0Start, blend0End, r0);
        return vec2f(mix(sampleClipmapLevel(position, 0u), sampleClipmapLevel(position, 1u), blend), mix(voxel0, voxel1, blend));
    }
    let blend1Start = extent1 * 0.32;
    let blend1End = extent1 * 0.46;
    if (r1 <= blend1Start) { return vec2f(sampleClipmapLevel(position, 1u), voxel1); }
    if (r1 < blend1End) {
        let blend = smoothstep(blend1Start, blend1End, r1);
        return vec2f(mix(sampleClipmapLevel(position, 1u), sampleClipmapLevel(position, 2u), blend), mix(voxel1, voxel2, blend));
    }
    return vec2f(sampleClipmapLevel(position, 2u), voxel2);
}
fn sampleGlobalVoxelSize(position: vec3f) -> f32 { return sampleGlobalData(position).y; }
fn assetCoordinate(localPosition: vec3f) -> vec3f {
    return (localPosition - Asset.minimum.xyz) / (Asset.maximum.xyz - Asset.minimum.xyz) * vec3f(Asset.dimensions.xyz) - vec3f(0.5);
}
fn sampleVehicleDistance(localPosition: vec3f) -> f32 {
    if (any(localPosition < Asset.minimum.xyz) || any(localPosition > Asset.maximum.xyz)) { return 1e4; }
    let coordinate = assetCoordinate(localPosition);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let limit = vec3i(Asset.dimensions.xyz) - vec3i(1);
    let lo = clamp(base, vec3i(0), limit);
    let hi = clamp(base + vec3i(1), vec3i(0), limit);
    let x00 = mix(textureLoad(VehicleDistance, vec3i(lo.x, lo.y, lo.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, lo.y, lo.z), 0).r, fraction.x);
    let x10 = mix(textureLoad(VehicleDistance, vec3i(lo.x, hi.y, lo.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, hi.y, lo.z), 0).r, fraction.x);
    let x01 = mix(textureLoad(VehicleDistance, vec3i(lo.x, lo.y, hi.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, lo.y, hi.z), 0).r, fraction.x);
    let x11 = mix(textureLoad(VehicleDistance, vec3i(lo.x, hi.y, hi.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, hi.y, hi.z), 0).r, fraction.x);
    return mix(mix(x00, x10, fraction.y), mix(x01, x11, fraction.y), fraction.z);
}
fn sampleVehicleMaterial(localPosition: vec3f) -> vec4f {
    if (any(localPosition < Asset.minimum.xyz) || any(localPosition > Asset.maximum.xyz)) { return vec4f(0.6, 0.6, 0.6, 0.0); }
    let coordinate = assetCoordinate(localPosition);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let limit = vec3i(Asset.dimensions.xyz) - vec3i(1);
    let lo = clamp(base, vec3i(0), limit);
    let hi = clamp(base + vec3i(1), vec3i(0), limit);
    let x00 = mix(textureLoad(VehicleMaterial, vec3i(lo.x, lo.y, lo.z), 0), textureLoad(VehicleMaterial, vec3i(hi.x, lo.y, lo.z), 0), fraction.x);
    let x10 = mix(textureLoad(VehicleMaterial, vec3i(lo.x, hi.y, lo.z), 0), textureLoad(VehicleMaterial, vec3i(hi.x, hi.y, lo.z), 0), fraction.x);
    let x01 = mix(textureLoad(VehicleMaterial, vec3i(lo.x, lo.y, hi.z), 0), textureLoad(VehicleMaterial, vec3i(hi.x, lo.y, hi.z), 0), fraction.x);
    let x11 = mix(textureLoad(VehicleMaterial, vec3i(lo.x, hi.y, hi.z), 0), textureLoad(VehicleMaterial, vec3i(hi.x, hi.y, hi.z), 0), fraction.x);
    return mix(mix(x00, x10, fraction.y), mix(x01, x11, fraction.y), fraction.z);
}
fn sampleVehicleSurface(localPosition: vec3f) -> vec4f {
    if (any(localPosition < Asset.minimum.xyz) || any(localPosition > Asset.maximum.xyz)) { return vec4f(0.5, 0.0, 0.0, 1.0); }
    let coordinate = assetCoordinate(localPosition);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let limit = vec3i(Asset.dimensions.xyz) - vec3i(1);
    let lo = clamp(base, vec3i(0), limit);
    let hi = clamp(base + vec3i(1), vec3i(0), limit);
    let x00 = mix(textureLoad(VehicleSurface, vec3i(lo.x, lo.y, lo.z), 0), textureLoad(VehicleSurface, vec3i(hi.x, lo.y, lo.z), 0), fraction.x);
    let x10 = mix(textureLoad(VehicleSurface, vec3i(lo.x, hi.y, lo.z), 0), textureLoad(VehicleSurface, vec3i(hi.x, hi.y, lo.z), 0), fraction.x);
    let x01 = mix(textureLoad(VehicleSurface, vec3i(lo.x, lo.y, hi.z), 0), textureLoad(VehicleSurface, vec3i(hi.x, lo.y, hi.z), 0), fraction.x);
    let x11 = mix(textureLoad(VehicleSurface, vec3i(lo.x, hi.y, hi.z), 0), textureLoad(VehicleSurface, vec3i(hi.x, hi.y, hi.z), 0), fraction.x);
    return mix(mix(x00, x10, fraction.y), mix(x01, x11, fraction.y), fraction.z);
}
fn staticDistance(position: vec3f) -> f32 {
    var distance = position.y;
    let count = u32(P.gi.w + 0.5);
    for (var index = 0u; index < count; index++) {
        let shape = Shapes[index];
        let boundsDelta = abs(position - shape.center.xyz) -
            vec3f(shape.material.z, shape.halfExtent.y, shape.material.w);
        let outside = max(boundsDelta, vec3f(0.0));
        let nearestPossible = max(distance, 0.0);
        if (dot(outside, outside) <= nearestPossible * nearestPossible) {
            distance = min(distance, sdShape(position, shape));
        }
    }
    return distance;
}
fn sampleGlobalDistanceAndVoxel(position: vec3f) -> vec2f {
    // Static geometry is already present in the ready clipmap. Do not walk the
    // source-shape list at every ray step; only the moving vehicle needs a live
    // SDF query in addition to the static global field.
    let fieldData = sampleGlobalData(position);
    let localPosition = worldToVehicle(position);
    if (all(localPosition >= Asset.minimum.xyz) && all(localPosition <= Asset.maximum.xyz)) {
        return vec2f(min(fieldData.x, sampleVehicleDistance(localPosition)), fieldData.y);
    }
    return fieldData;
}
fn sampleGlobalDistance(position: vec3f) -> f32 {
    return sampleGlobalDistanceAndVoxel(position).x;
}
`;

export const GBUFFER_WGSL = /* wgsl */ `${FRAME_TYPES}
struct VertexInput {
    @location(0) position: vec4f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) surface: vec4f,
};
struct VertexOutput {
    @builtin(position) clipPosition: vec4f,
    @location(0) worldPosition: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec4f,
    @location(3) surface: vec4f,
    @location(4) @interpolate(flat) surfaceCardId: u32,
};
fn rotateWheelZ(value: vec3f, angle: f32) -> vec3f {
    let c = cos(angle);
    let s = sin(angle);
    return vec3f(c * value.x - s * value.y, s * value.x + c * value.y, value.z);
}
fn rotateWheelY(value: vec3f, angle: f32) -> vec3f {
    let c = cos(angle);
    let s = sin(angle);
    return vec3f(c * value.x + s * value.z, value.y, -s * value.x + c * value.z);
}
@vertex fn vertexMain(input: VertexInput) -> VertexOutput {
    var world = input.position.xyz;
    var normal = input.normal;
    if (input.surface.w > 0.5) {
        var localPosition = input.position.xyz;
        let wheelPart = u32(round(input.surface.w));
        if (wheelPart == 2u) {
            let center = P.wheelFrontLeft.xyz;
            localPosition = rotateWheelY(rotateWheelZ(input.position.xyz - center, P.wheelFrontLeft.w), P.wheelSteering.x) + center;
            normal = rotateWheelY(rotateWheelZ(normal, P.wheelFrontLeft.w), P.wheelSteering.x);
        } else if (wheelPart == 3u) {
            let center = P.wheelFrontRight.xyz;
            localPosition = rotateWheelY(rotateWheelZ(input.position.xyz - center, P.wheelFrontRight.w), P.wheelSteering.y) + center;
            normal = rotateWheelY(rotateWheelZ(normal, P.wheelFrontRight.w), P.wheelSteering.y);
        } else if (wheelPart == 4u) {
            let center = P.wheelRearLeft.xyz;
            localPosition = rotateWheelZ(input.position.xyz - center, P.wheelRearLeft.w) + center;
            normal = rotateWheelZ(normal, P.wheelRearLeft.w);
        } else if (wheelPart == 5u) {
            let center = P.wheelRearRight.xyz;
            localPosition = rotateWheelZ(input.position.xyz - center, P.wheelRearRight.w) + center;
            normal = rotateWheelZ(normal, P.wheelRearRight.w);
        }
        let c = cos(P.vehicle.w);
        let s = sin(P.vehicle.w);
        world = vec3f(P.vehicle.x + c * localPosition.x + s * localPosition.z,
                      P.vehicle.y + localPosition.y,
                      P.vehicle.z - s * localPosition.x + c * localPosition.z);
        normal = vec3f(c * normal.x + s * normal.z,
                       normal.y,
                       -s * normal.x + c * normal.z);
    }
    var output: VertexOutput;
    output.clipPosition = P.viewProjection * vec4f(world, 1.0);
    output.worldPosition = world;
    output.worldNormal = normalize(normal);
    output.color = input.color;
    output.surface = input.surface;
    output.surfaceCardId = u32(round(max(input.position.w, 0.0)));
    return output;
}
struct GBufferOutput {
    @location(0) position: vec4f,
    @location(1) normalRoughness: vec4f,
    @location(2) albedoMetallic: vec4f,
    @location(3) emission: vec4f,
};
@fragment fn fragmentMain(input: VertexOutput) -> GBufferOutput {
    var albedo = input.color.rgb;
    if (input.worldPosition.y < 0.02 && input.worldNormal.y > 0.8 && input.surface.w < 0.5) {
        albedo = floorAlbedo(input.worldPosition);
    }
    var output: GBufferOutput;
    let dynamicSurface = input.surface.w > 0.5;
    let objectId = u32(round(clamp(input.color.w, 0.0, 63.0)));
    let classBase = select(1.0, 2.0, dynamicSurface);
    let visibilityCode = classBase + f32(objectId) / 256.0;
    output.position = vec4f(input.worldPosition, visibilityCode);
    output.normalRoughness = vec4f(normalize(input.worldNormal), clamp(input.surface.x, 0.025, 1.0));
    output.albedoMetallic = vec4f(albedo, clamp(input.surface.y, 0.0, 1.0));
    output.emission = vec4f(albedo * max(input.surface.z, 0.0), 0.0);
    return output;
}
@fragment fn cardIdFragmentMain(input: VertexOutput) -> @location(0) u32 {
    return input.surfaceCardId;
}
`;

const SURFACE_CACHE_REUSE_WGSL = /* wgsl */ `
@group(0) @binding(13) var CacheHistoryPosition: texture_2d<f32>;
@group(0) @binding(14) var CacheHistoryNormal: texture_2d<f32>;
fn surfaceCacheHasHistory(position: vec4f, normal: vec3f) -> bool {
    if (P.emissiveParams.y < 0.5 || position.w < 0.5) { return false; }
    let previousClip = P.previousViewProjection * vec4f(position.xyz, 1.0);
    if (previousClip.w <= 1e-4) { return false; }
    let previousUv = previousClip.xy / previousClip.w * vec2f(0.5, -0.5) + vec2f(0.5);
    if (any(previousUv <= vec2f(0.0)) || any(previousUv >= vec2f(1.0))) { return false; }
    let dimensions = textureDimensions(CacheHistoryPosition);
    let previousPixel = clamp(vec2i(previousUv * vec2f(dimensions)), vec2i(0), vec2i(dimensions) - vec2i(1));
    let oldPosition = textureLoad(CacheHistoryPosition, previousPixel, 0);
    if (oldPosition.w < 0.5 || !sameSurfaceVisibility(oldPosition.w, position.w)) { return false; }
    let tolerance = select(0.20, 0.075, position.w > 1.5);
    if (distance(oldPosition.xyz, position.xyz) > tolerance) { return false; }
    let oldNormal = normalize(textureLoad(CacheHistoryNormal, previousPixel, 0).xyz);
    return dot(oldNormal, normal) >= 0.86;
}
fn surfaceCacheShouldUpdate(pixel: vec2i, position: vec4f, normal: vec3f) -> bool {
    if (P.emissiveParams.z < 0.5 || position.w > 1.5 || !surfaceCacheHasHistory(position, normal)) { return true; }
    let movingVehicleInfluence = vehicleMoved() && distance(position.xz, P.vehicle.xz) < VEHICLE_DYNAMIC_GI_RADIUS;
    // Keep nearby vehicle-affected GI fully reactive while amortizing stable,
    // camera-independent world lighting farther from the moving car.
    if (movingVehicleInfluence) { return true; }
    // Two rotating checkerboard phases amortize lighting on stable surfaces.
    let pixelPhase = (u32(pixel.x) + u32(pixel.y)) & 1u;
    let framePhase = u32(P.modeQuality.w + 0.5) & 1u;
    return pixelPhase == framePhase;
}
`;

const SHADE_FUNCTIONS = /* wgsl */ `
struct EmissiveTriangle {
    a: vec4f,
    b: vec4f,
    c: vec4f,
    radiance: vec4f,
    sourceCenter: vec4f,
    sampling: vec4f,
    metadata: vec4u,
};
@group(0) @binding(11) var<storage, read> EmissiveSources: array<EmissiveTriangle>;
struct SurfaceMaterial {
    albedo: vec3f,
    roughness: f32,
    metallic: f32,
    emission: vec3f,
};
struct RayHit {
    position: vec3f,
    normal: vec3f,
    distance: f32,
    level: u32,
    found: u32,
};
fn maxComponent(value: vec3f) -> f32 { return max(value.x, max(value.y, value.z)); }
fn fieldNormal(position: vec3f) -> vec3f {
    let epsilon = max(sampleGlobalVoxelSize(position) * 0.55, 0.035);
    let x = sampleGlobalDistance(position + vec3f(epsilon, 0.0, 0.0)) - sampleGlobalDistance(position - vec3f(epsilon, 0.0, 0.0));
    let y = sampleGlobalDistance(position + vec3f(0.0, epsilon, 0.0)) - sampleGlobalDistance(position - vec3f(0.0, epsilon, 0.0));
    let z = sampleGlobalDistance(position + vec3f(0.0, 0.0, epsilon)) - sampleGlobalDistance(position - vec3f(0.0, 0.0, epsilon));
    return normalize(vec3f(x, y, z) + vec3f(1e-6));
}
fn traceField(origin: vec3f, direction: vec3f, maximumDistance: f32, maximumSteps: u32) -> RayHit {
    var result = RayHit(origin, vec3f(0.0, 1.0, 0.0), maximumDistance, 2u, 0u);
    let originField = sampleGlobalDistanceAndVoxel(origin);
    var travel = max(0.035, originField.y * 0.35);
    for (var iteration = 0u; iteration < maximumSteps; iteration++) {
        if (travel >= maximumDistance) { break; }
        let position = origin + direction * travel;
        let fieldData = sampleGlobalDistanceAndVoxel(position);
        let sampleVoxel = fieldData.y;
        let distance = fieldData.x;
        if (distance > 1e3) { break; }
        let level = fieldLevelFor(position);
        let threshold = max(0.025, sampleVoxel * 0.34);
        if (distance <= threshold) {
            result.position = position;
            result.normal = fieldNormal(position);
            result.distance = travel;
            result.level = level;
            result.found = 1u;
            return result;
        }
        travel += max(distance * 0.78, sampleVoxel * 0.28);
    }
    return result;
}
fn analyticStaticMaterial(position: vec3f) -> SurfaceMaterial {
    var result = SurfaceMaterial(floorAlbedo(position), 0.88, 0.02, vec3f(0.0));
    var nearest = abs(position.y);
    let count = u32(P.gi.w + 0.5);
    for (var index = 0u; index < count; index++) {
        let shape = Shapes[index];
        let boundsDelta = abs(position - shape.center.xyz) -
            vec3f(shape.material.z, shape.halfExtent.y, shape.material.w);
        let outside = max(boundsDelta, vec3f(0.0));
        if (dot(outside, outside) > nearest * nearest) { continue; }
        let distance = sdShape(position, shape);
        if (abs(distance) < nearest) {
            nearest = abs(distance);
            result = SurfaceMaterial(shape.albedo.xyz, shape.material.x, shape.albedo.w,
                                     shape.albedo.xyz * shape.material.y * P.lightControls.x);
        }
    }
    return result;
}
fn staticMaterial(position: vec3f) -> SurfaceMaterial {
    let level = fieldLevelFor(position);
    let readyMask = u32(P.wheelSteering.w + 0.5);
    if ((readyMask & (1u << level)) == 0u) { return analyticStaticMaterial(position); }
    let surface = sampleGlobalSurfaceData(position);
    let visibilityId = (surface.packedId >> 16u) & 0xffu;
    if (visibilityId == 0u && abs(position.y) < 0.12) {
        return SurfaceMaterial(floorAlbedo(position), 0.88, 0.02, vec3f(0.0));
    }
    let cardId = surface.packedId & 0xffffu;
    if (cardId > 0u && cardId <= arrayLength(&SurfaceCards)) {
        let card = SurfaceCards[cardId - 1u];
        return SurfaceMaterial(
            card.albedoMetallic.rgb,
            clamp(card.normalRoughness.w, 0.025, 1.0),
            clamp(card.albedoMetallic.w, 0.0, 1.0),
            card.emission.rgb * P.lightControls.x,
        );
    }
    return SurfaceMaterial(
        surface.albedoRoughness.rgb,
        clamp(surface.albedoRoughness.a, 0.025, 1.0),
        clamp(surface.emissionMetallic.a, 0.0, 1.0),
        surface.emissionMetallic.rgb * P.lightControls.x,
    );
}
fn materialAt(position: vec3f) -> SurfaceMaterial {
    let local = worldToVehicle(position);
    let insideAssetBounds = all(local >= Asset.minimum.xyz) && all(local <= Asset.maximum.xyz);
    if (insideAssetBounds) {
        let vehicleDistance = sampleVehicleDistance(local);
        let staticD = sampleGlobalData(position).x;
        if (abs(vehicleDistance) <= abs(staticD) + 0.11) {
            let sampled = sampleVehicleMaterial(local);
            let surface = sampleVehicleSurface(local);
            return SurfaceMaterial(sampled.rgb, surface.r, surface.g, sampled.rgb * (sampled.a * 8.0) * P.lightControls.x);
        }
    }
    return staticMaterial(position);
}
fn skyRadiance(direction: vec3f) -> vec3f {
    let horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    let gradient = mix(vec3f(0.018, 0.027, 0.044), vec3f(0.30, 0.43, 0.56), pow(horizon, 1.2));
    let sunLobe = pow(max(dot(direction, normalize(P.sun.xyz)), 0.0), 180.0) * vec3f(1.0, 0.68, 0.35) * 1.5;
    return (gradient + sunLobe) * P.sky.w;
}
fn traceVisibility(origin: vec3f, direction: vec3f, maximumDistance: f32, maximumSteps: u32) -> f32 {
    // Each area/sun sample gets its own visibility ray. Averaging these actual
    // source samples creates the penumbra; the emitter itself is never blurred.
    let contactBias = max(voxelSize(0u) * 0.08, 0.012);
    let minimumStep = max(voxelSize(0u) * 0.01, 0.001);
    let maximumStep = 1.5;
    var travel = max(0.055, voxelSize(0u) * 0.40);
    for (var iteration = 0u; iteration < maximumSteps; iteration++) {
        if (travel >= maximumDistance) { break; }
        let distance = sampleGlobalDistance(origin + direction * travel);
        if (distance > 1e3) { break; }
        if (distance <= contactBias) { return 0.0; }
        travel += clamp(distance * 0.78, minimumStep, maximumStep);
    }
    return 1.0;
}
fn ambientOcclusion(position: vec3f, normal: vec3f) -> f32 {
    // Do not query sub-voxel radii: those samples turn clipmap quantization into
    // visible contour rings. Bias by the active blended voxel footprint and use a
    // smooth cavity response with a bounded maximum darkening.
    let voxel = sampleGlobalVoxelSize(position);
    let bias = max(voxel * 0.38, 0.025);
    let distances = array<f32, 6>(0.16, 0.28, 0.43, 0.62, 0.86, 1.15);
    let weights = array<f32, 6>(0.28, 0.24, 0.19, 0.13, 0.10, 0.06);
    var occlusion = 0.0;
    for (var index = 0u; index < 6u; index++) {
        let radius = distances[index];
        if (radius < voxel * 0.85) { continue; }
        let measured = sampleGlobalDistance(position + normal * radius);
        let cavity = clamp((radius - measured - bias) / radius, 0.0, 1.0);
        occlusion += smoothstep(0.0, 1.0, cavity) * weights[index];
    }
    let strength = clamp(P.gi.z, 0.0, 2.0) * 0.22;
    return clamp(1.0 - occlusion * strength, 0.56, 1.0);
}
fn brdfTimesCosine(normal: vec3f, viewDirection: vec3f, lightDirection: vec3f, material: SurfaceMaterial, specularScale: f32) -> vec3f {
    let cosine = max(dot(normal, lightDirection), 0.0);
    if (cosine <= 0.0) { return vec3f(0.0); }
    let halfVector = normalize(lightDirection + viewDirection);
    let roughness = clamp(material.roughness, 0.035, 1.0);
    let alpha = roughness * roughness;
    let alphaSquared = alpha * alpha;
    let ndoth = max(dot(normal, halfVector), 0.0);
    let ndotv = max(dot(normal, viewDirection), 1e-3);
    let denominator = ndoth * ndoth * (alphaSquared - 1.0) + 1.0;
    let distribution = alphaSquared / max(3.14159265 * denominator * denominator, 1e-4);
    let f0 = mix(vec3f(0.04), material.albedo, clamp(material.metallic, 0.0, 1.0));
    let fresnel = f0 + (vec3f(1.0) - f0) * pow(1.0 - max(dot(lightDirection, halfVector), 0.0), 5.0);
    let specular = fresnel * distribution * specularScale / (ndotv + 0.02);
    let diffuse = material.albedo * (1.0 - material.metallic) / 3.14159265;
    return (diffuse + specular) * cosine;
}
fn vehicleLocalToWorld(point: vec3f) -> vec3f {
    let c = cos(P.vehicle.w);
    let s = sin(P.vehicle.w);
    return vec3f(P.vehicle.x + c * point.x + s * point.z,
                 P.vehicle.y + point.y,
                 P.vehicle.z - s * point.x + c * point.z);
}
fn emissiveDirect(
    position: vec3f,
    normal: vec3f,
    viewDirection: vec3f,
    material: SurfaceMaterial,
    seed: f32,
    sampleIndex: u32,
    requestedSamples: u32,
) -> vec3f {
    let sourceCount = u32(P.emissiveParams.x + 0.5);
    if (sourceCount == 0u) { return vec3f(0.0); }
    let sampleCount = clamp(requestedSamples, 1u, 16u);
    let sourceScale = clamp(P.lightControls.y, 0.2, 2.5);
    let originBias = max(0.055, voxelSize(0u) * 0.40);
    let rayOrigin = position + normal * originBias;
    var result = vec3f(0.0);

    // Walker-alias selection samples from every emissive triangle in O(1); the
    // fixed-size loop is over Monte Carlo samples, never over the emitter list.
    for (var sample = 0u; sample < 16u; sample++) {
        if (sample >= sampleCount) { break; }
        let stratum = f32(sample) + hash21(vec2f(seed * 17.13 + f32(sampleIndex), f32(sample + 1u) * 31.7));
        let aliasCoordinate = stratum / f32(sampleCount) * f32(sourceCount);
        let column = min(u32(aliasCoordinate), sourceCount - 1u);
        let columnData = EmissiveSources[column];
        let sourceIndex = select(columnData.metadata.x, column, fract(aliasCoordinate) < columnData.sampling.y);
        let source = EmissiveSources[sourceIndex];
        let u = hash21(vec2f(seed * 29.7 + f32(sampleIndex) * 0.31, f32(sample + 3u) * 11.17));
        let v = hash21(vec2f(seed * 43.1 + f32(sampleIndex) * 0.73, f32(sample + 7u) * 19.19));
        let root = sqrt(u);
        let baryA = 1.0 - root;
        let baryB = root * (1.0 - v);
        let baryC = root * v;

        var a = source.a.xyz;
        var b = source.b.xyz;
        var c = source.c.xyz;
        var sourceCenter = source.sourceCenter.xyz;
        if (source.metadata.y == 1u) {
            a = vehicleLocalToWorld(a);
            b = vehicleLocalToWorld(b);
            c = vehicleLocalToWorld(c);
            sourceCenter = vehicleLocalToWorld(sourceCenter);
        }
        a = sourceCenter + (a - sourceCenter) * sourceScale;
        b = sourceCenter + (b - sourceCenter) * sourceScale;
        c = sourceCenter + (c - sourceCenter) * sourceScale;
        let lightPoint = a * baryA + b * baryB + c * baryC;
        let toLight = lightPoint - position;
        let distanceSquared = max(dot(toLight, toLight), 1e-4);
        let distance = sqrt(distanceSquared);
        let lightDirection = toLight / distance;
        let cosineAtSource = max(dot(normalize(cross(b - a, c - a)), -lightDirection), 0.0);
        if (cosineAtSource <= 0.0 || dot(normal, lightDirection) <= 0.0) { continue; }

        let shadowVector = lightPoint - rayOrigin;
        let shadowDistance = max(length(shadowVector) - max(0.045, voxelSize(0u) * 0.20), 0.0);
        let visibility = traceVisibility(rayOrigin, normalize(shadowVector), shadowDistance, 32u);
        let area = source.sampling.x * sourceScale * sourceScale;
        let sourcePdf = max(source.sampling.z, 1e-8);
        let geometry = cosineAtSource * area / max(distanceSquared * sourcePdf, 1e-5);
        let brdf = brdfTimesCosine(normal, viewDirection, lightDirection, material, 0.18);
        result += brdf * source.radiance.rgb * P.lightControls.x * geometry * visibility;
    }
    return result / f32(sampleCount);
}
fn pointAreaLightDirect(
    position: vec3f,
    normal: vec3f,
    viewDirection: vec3f,
    material: SurfaceMaterial,
    seed: f32,
    sampleIndex: u32,
    requestedSamples: u32,
) -> vec3f {
    let intensity = max(P.areaLightPosition.w, 0.0);
    if (intensity <= 1e-4) { return vec3f(0.0); }
    let sampleCount = clamp(requestedSamples, 1u, 4u);
    let radius = clamp(P.areaLightColorRadius.w, 0.0, 2.5);
    let originBias = max(0.055, voxelSize(0u) * 0.40);
    let rayOrigin = position + normal * originBias;
    var result = vec3f(0.0);
    // A sphere-shaped point-light source is distinct from emissive geometry.
    // Source radius changes the sampled emitter extent, so the blocker casts a
    // finite-source penumbra instead of a hard point-light shadow.
    for (var sample = 0u; sample < 4u; sample++) {
        if (sample >= sampleCount) { break; }
        let u = (f32(sample) + hash21(vec2f(seed * 19.73 + f32(sampleIndex), f32(sample + 1u) * 37.1))) / f32(sampleCount);
        let v = hash21(vec2f(seed * 41.17 + f32(sampleIndex) * 0.37, f32(sample + 5u) * 13.9));
        let y = 1.0 - 2.0 * u;
        let radial = sqrt(max(1.0 - y * y, 0.0));
        let phi = 6.28318530718 * v;
        let sourceNormal = vec3f(radial * cos(phi), y, radial * sin(phi));
        let lightPoint = P.areaLightPosition.xyz + sourceNormal * radius;
        let toLight = lightPoint - position;
        let distanceSquared = max(dot(toLight, toLight), 1e-4);
        let distance = sqrt(distanceSquared);
        let lightDirection = toLight / distance;
        if (dot(normal, lightDirection) <= 0.0) { continue; }
        let sourceCosine = select(1.0, max(dot(sourceNormal, -lightDirection), 0.0), radius > 1e-3);
        if (sourceCosine <= 0.0) { continue; }
        let visibility = traceVisibility(rayOrigin, normalize(lightPoint - rayOrigin),
            max(distance - max(0.045, voxelSize(0u) * 0.20), 0.0), 32u);
        let geometry = intensity * sourceCosine / distanceSquared;
        result += brdfTimesCosine(normal, viewDirection, lightDirection, material, 0.18) *
            P.areaLightColorRadius.rgb * geometry * visibility;
    }
    return result / f32(sampleCount);
}
fn sunDirect(
    position: vec3f,
    normal: vec3f,
    viewDirection: vec3f,
    material: SurfaceMaterial,
    seed: f32,
    sampleIndex: u32,
    requestedSamples: u32,
) -> vec3f {
    let sampleCount = clamp(requestedSamples, 1u, 4u);
    let sunDirection = normalize(P.sun.xyz);
    let angularRadius = clamp(P.lightControls.z, 0.05, 3.0) * 0.01745329252;
    let cosineLimit = cos(angularRadius);
    let helper = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(sunDirection.y) > 0.98);
    let tangent = normalize(cross(helper, sunDirection));
    let bitangent = cross(sunDirection, tangent);
    let originBias = max(0.055, voxelSize(0u) * 0.40);
    let rayOrigin = position + normal * originBias;
    var result = vec3f(0.0);

    // Sampling points across the solar disk makes the blocker—not the light
    // surface—produce the penumbra through the fraction of visible rays.
    for (var sample = 0u; sample < 4u; sample++) {
        if (sample >= sampleCount) { break; }
        let u = (f32(sample) + hash21(vec2f(seed * 13.7 + f32(sampleIndex), f32(sample + 2u) * 23.1))) / f32(sampleCount);
        let v = hash21(vec2f(seed * 37.1 + f32(sampleIndex) * 0.47, f32(sample + 5u) * 17.7));
        let cosTheta = mix(1.0, cosineLimit, u);
        let sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        let phi = 6.28318530718 * v;
        let lightDirection = normalize(sunDirection * cosTheta +
            (tangent * cos(phi) + bitangent * sin(phi)) * sinTheta);
        let visibility = traceVisibility(rayOrigin, lightDirection, 46.0, 32u);
        result += brdfTimesCosine(normal, viewDirection, lightDirection, material, 0.22) * P.sun.w * visibility;
    }
    return result / f32(sampleCount);
}
fn directAtSurface(
    position: vec3f,
    normal: vec3f,
    viewDirection: vec3f,
    material: SurfaceMaterial,
    seed: f32,
    sampleIndex: u32,
    requestedSamples: u32,
    emissiveSamples: u32,
) -> vec3f {
    return sunDirect(position, normal, viewDirection, material, seed, sampleIndex, requestedSamples) +
           pointAreaLightDirect(position, normal, viewDirection, material, seed, sampleIndex, requestedSamples) +
           emissiveDirect(position, normal, viewDirection, material, seed, sampleIndex, emissiveSamples);
}
fn cosineHemisphere(normal: vec3f, u: f32, v: f32) -> vec3f {
    let phi = 6.28318530718 * v;
    let radial = sqrt(u);
    let localX = radial * cos(phi);
    let localY = radial * sin(phi);
    let localZ = sqrt(max(1.0 - u, 0.0));
    let helper = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(normal.y) > 0.98);
    let tangent = normalize(cross(helper, normal));
    let bitangent = cross(normal, tangent);
    return normalize(tangent * localX + bitangent * localY + normal * localZ);
}
fn hash21(point: vec2f) -> f32 {
    let value = dot(point, vec2f(127.1, 311.7));
    return fract(sin(value) * 43758.5453123);
}
fn stableSurfaceSeed(position: vec3f) -> f32 {
    let worldCell = floor(position * 48.0);
    let seedPosition = vec2f(worldCell.x + worldCell.y * 19.19, worldCell.z + worldCell.y * 73.73);
    return hash21(seedPosition + vec2f(0.37, 0.73));
}
fn tracePath(position: vec3f, normal: vec3f, seed: f32, sampleIndex: u32) -> vec3f {
    var radiance = vec3f(0.0);
    var throughput = vec3f(1.0);
    var origin = position + normal * max(0.045, sampleGlobalVoxelSize(position) * 0.35);
    var bounceNormal = normal;
    let bounceLimit = clamp(u32(P.modeQuality.z + 0.5), 1u, 3u);
    var u = fract(seed + f32(sampleIndex) * 0.754877666);
    var v = fract(seed * 0.618033989 + f32(sampleIndex) * 0.569840296);
    var direction = cosineHemisphere(bounceNormal, u, v);
    for (var bounce = 0u; bounce < 3u; bounce++) {
        if (bounce >= bounceLimit) { break; }
        let hit = traceField(origin, direction, 92.0, 38u);
        if (hit.found == 0u) {
            radiance += throughput * skyRadiance(direction);
            break;
        }
        let material = materialAt(hit.position);
        // Emissive hits are accounted for by next-event sampling at the previous
        // diffuse vertex. Do not add a second, high-variance path-hit estimate.
        if (maxComponent(material.emission) > 1e-4) { break; }
        let hitSeed = fract(seed + stableSurfaceSeed(hit.position) * 0.618033989);
        radiance += throughput * directAtSurface(
            hit.position, hit.normal, normalize(-direction), material,
            hitSeed, sampleIndex * 7u + bounce, 1u, 1u,
        );
        if (bounce + 1u >= bounceLimit) { break; }
        throughput *= material.albedo * 0.82;
        if (maxComponent(throughput) < 0.025) { break; }
        bounceNormal = hit.normal;
        u = fract(seed * 0.379 + f32(bounce + sampleIndex + 1u) * 0.754877666);
        v = fract(seed * 0.719 + f32(bounce + sampleIndex + 1u) * 0.569840296);
        direction = cosineHemisphere(bounceNormal, u, v);
        origin = hit.position + bounceNormal * max(0.045, sampleGlobalVoxelSize(hit.position) * 0.36);
    }
    return radiance;
}
fn diffuseGi(position: vec3f, normal: vec3f, albedo: vec3f) -> vec3f {
    let sampleCount = clamp(u32(P.modeQuality.y + 0.5), 1u, 6u);
    // Anchor the sample sequence to quantized world position so camera motion does
    // not reshuffle GI noise across otherwise static walls and surfaces.
    // Rotate the per-surface sequence each frame so temporal accumulation gets
    // new path samples instead of repeatedly averaging an identical pattern.
    let seed = fract(stableSurfaceSeed(position) + P.modeQuality.w * 0.618033989);
    var sum = vec3f(0.0);
    for (var sampleIndex = 0u; sampleIndex < 6u; sampleIndex++) {
        if (sampleIndex >= sampleCount) { break; }
        sum += tracePath(position, normal, seed, sampleIndex);
    }
    return albedo * (sum / f32(sampleCount)) * P.gi.x;
}
fn fresnelSchlick(cosine: f32, f0: vec3f) -> vec3f {
    return f0 + (vec3f(1.0) - f0) * pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);
}
fn aces(color: vec3f) -> vec3f {
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
}
fn fieldDebugColor(distance: f32) -> vec3f {
    if (distance < 0.0) {
        return mix(vec3f(0.88, 0.12, 0.27), vec3f(1.0, 0.50, 0.14), clamp(-distance * 0.24, 0.0, 1.0));
    }
    let proximity = exp(-distance * 0.34);
    return mix(vec3f(0.018, 0.035, 0.065), vec3f(0.08, 0.72, 0.68), proximity);
}
`;

const SURFACE_CACHE_TYPES = /* wgsl */ `
struct SurfaceCard {
    origin: vec4f,
    axisU: vec4f,
    axisV: vec4f,
    normalRoughness: vec4f,
    albedoMetallic: vec4f,
    emission: vec4f,
};
struct SurfaceCacheEntry {
    direct: vec4f,
    indirect: vec4f,
};
`;

const SURFACE_CACHE_READ = /* wgsl */ `
${SURFACE_CACHE_TYPES}
@group(0) @binding(15) var SurfaceCardId: texture_2d<u32>;
@group(0) @binding(16) var<storage, read> SurfaceCards: array<SurfaceCard>;
@group(0) @binding(17) var<storage, read> SurfaceRadiance: array<SurfaceCacheEntry>;
fn surfaceCacheEntryValid(entry: SurfaceCacheEntry) -> bool {
    let generation = u32(P.gi.y + 0.5);
    return u32(round(entry.direct.w)) == generation && u32(round(entry.indirect.w)) == generation;
}
fn sampleSurfaceCard(cardId: u32, worldPosition: vec3f) -> SurfaceCacheEntry {
    let empty = SurfaceCacheEntry(vec4f(0.0), vec4f(0.0));
    if (cardId == 0u || cardId > arrayLength(&SurfaceCards)) { return empty; }
    let card = SurfaceCards[cardId - 1u];
    let relative = worldPosition - card.origin.xyz;
    let uv = clamp(vec2f(
        dot(relative, card.axisU.xyz) * card.axisU.w,
        dot(relative, card.axisV.xyz) * card.axisV.w,
    ), vec2f(0.0), vec2f(1.0));
    let coordinate = clamp(uv * 4.0 - vec2f(0.5), vec2f(0.0), vec2f(3.0));
    let lower = vec2u(floor(coordinate));
    let upper = min(lower + vec2u(1u), vec2u(3u));
    let fraction = fract(coordinate);
    let base = (cardId - 1u) * 16u;
    let a = SurfaceRadiance[base + lower.y * 4u + lower.x];
    let b = SurfaceRadiance[base + lower.y * 4u + upper.x];
    let c = SurfaceRadiance[base + upper.y * 4u + lower.x];
    let d = SurfaceRadiance[base + upper.y * 4u + upper.x];
    if (!surfaceCacheEntryValid(a) || !surfaceCacheEntryValid(b) ||
        !surfaceCacheEntryValid(c) || !surfaceCacheEntryValid(d)) { return empty; }
    let direct = mix(mix(a.direct, b.direct, fraction.x), mix(c.direct, d.direct, fraction.x), fraction.y);
    let indirect = mix(mix(a.indirect, b.indirect, fraction.x), mix(c.indirect, d.indirect, fraction.x), fraction.y);
    return SurfaceCacheEntry(direct, indirect);
}
`;

export const SURFACE_CACHE_WGSL = /* wgsl */ `${FRAME_TYPES}
${GLOBAL_FIELD_SAMPLING}
${SHADE_FUNCTIONS}
${SURFACE_CACHE_TYPES}
@group(0) @binding(12) var<storage, read> SurfaceCards: array<SurfaceCard>;
@group(0) @binding(13) var<storage, read_write> SurfaceRadiance: array<SurfaceCacheEntry>;
@group(0) @binding(14) var<storage, read> SurfaceCardUpdateList: array<u32>;
@compute @workgroup_size(64, 1, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let updateCount = u32(P.emissiveParams.w + 0.5);
    let cacheTexel = id.x;
    if (cacheTexel >= updateCount * 16u) { return; }
    let cardIndex = SurfaceCardUpdateList[cacheTexel / 16u];
    if (cardIndex >= arrayLength(&SurfaceCards)) { return; }
    let card = SurfaceCards[cardIndex];
    let localIndex = cacheTexel % 16u;
    let u = (f32(localIndex % 4u) + 0.5) * 0.25;
    let v = (f32(localIndex / 4u) + 0.5) * 0.25;
    let position = card.origin.xyz + card.axisU.xyz * u + card.axisV.xyz * v;
    let normal = normalize(card.normalRoughness.xyz);
    let albedo = card.albedoMetallic.rgb;
    let material = SurfaceMaterial(
        albedo,
        clamp(card.normalRoughness.w, 0.025, 1.0),
        clamp(card.albedoMetallic.w, 0.0, 1.0),
        card.emission.rgb * P.lightControls.x,
    );
    let seed = fract(stableSurfaceSeed(position) + P.modeQuality.w * 0.618033989);
    let direct = material.emission + emissiveDirect(
        position, normal, normal, material, seed, localIndex,
        u32(P.lightControls.w + 0.5),
    );
    let indirect = diffuseGi(position, normal, albedo);
    let generation = f32(u32(P.gi.y + 0.5));
    let radianceIndex = cardIndex * 16u + localIndex;
    SurfaceRadiance[radianceIndex].direct = vec4f(max(direct, vec3f(0.0)), generation);
    SurfaceRadiance[radianceIndex].indirect = vec4f(max(indirect, vec3f(0.0)), generation);
}
`;

export const SHADE_WGSL = /* wgsl */ `${FRAME_TYPES}
@group(0) @binding(1) var GPosition: texture_2d<f32>;
@group(0) @binding(2) var GNormalRoughness: texture_2d<f32>;
@group(0) @binding(3) var GAlbedoMetallic: texture_2d<f32>;
@group(0) @binding(4) var GEmission: texture_2d<f32>;
${GLOBAL_FIELD_SAMPLING}
@group(0) @binding(12) var OutputImage: texture_storage_2d<rgba16float, write>;
${SURFACE_CACHE_REUSE_WGSL}
${SHADE_FUNCTIONS}
${SURFACE_CACHE_READ}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let width = u32(P.viewport.x + 0.5);
    let height = u32(P.viewport.y + 0.5);
    if (id.x >= width || id.y >= height) { return; }
    let pixel = vec2i(id.xy);
    let positionSample = textureLoad(GPosition, pixel, 0);
    if (positionSample.w < 0.5) {
        textureStore(OutputImage, pixel, vec4f(0.0, 0.0, 0.0, 1.0));
        return;
    }
    let normalData = textureLoad(GNormalRoughness, pixel, 0);
    let normal = normalize(normalData.xyz);
    let position = positionSample.xyz;
    let materialData = textureLoad(GAlbedoMetallic, pixel, 0);
    let albedo = materialData.rgb;
    let emission = textureLoad(GEmission, pixel, 0).rgb * P.lightControls.x;
    let material = SurfaceMaterial(albedo, clamp(normalData.w, 0.025, 1.0),
                                  clamp(materialData.w, 0.0, 1.0), emission);
    let viewDirection = normalize(P.cameraPosition.xyz - position);
    let frameSeed = fract(stableSurfaceSeed(position) + P.modeQuality.w * 0.618033989);
    let cardId = textureLoad(SurfaceCardId, pixel, 0).r;
    if (cardId > 0u && distance(position.xz, P.vehicle.xz) > VEHICLE_DYNAMIC_GI_RADIUS) {
        let cached = sampleSurfaceCard(cardId, position);
        if (surfaceCacheEntryValid(cached)) {
            let sun = sunDirect(position, normal, viewDirection, material, frameSeed, 0u, 2u);
            let point = pointAreaLightDirect(position, normal, viewDirection, material, frameSeed, 0u, 2u);
            textureStore(OutputImage, pixel, vec4f(max(cached.direct.rgb + sun + point, vec3f(0.0)), 1.0));
            return;
        }
    }
    if (!surfaceCacheShouldUpdate(pixel, positionSample, normal)) {
        textureStore(OutputImage, pixel, vec4f(0.0, 0.0, 0.0, 0.0));
        return;
    }
    let direct = emission + directAtSurface(position, normal, viewDirection, material, frameSeed, 0u, 2u, u32(P.lightControls.w + 0.5));
    textureStore(OutputImage, pixel, vec4f(max(direct, vec3f(0.0)), 1.0));
}
`;

export const GI_WGSL = /* wgsl */ `${FRAME_TYPES}
@group(0) @binding(1) var GPosition: texture_2d<f32>;
@group(0) @binding(2) var GNormalRoughness: texture_2d<f32>;
@group(0) @binding(3) var GAlbedoMetallic: texture_2d<f32>;
@group(0) @binding(4) var GEmission: texture_2d<f32>;
${GLOBAL_FIELD_SAMPLING}
@group(0) @binding(12) var OutputImage: texture_storage_2d<rgba16float, write>;
${SURFACE_CACHE_REUSE_WGSL}
${SHADE_FUNCTIONS}
${SURFACE_CACHE_READ}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let width = u32(P.viewport.x + 0.5);
    let height = u32(P.viewport.y + 0.5);
    if (id.x >= width || id.y >= height) { return; }
    let pixel = vec2i(id.xy);
    let positionSample = textureLoad(GPosition, pixel, 0);
    if (positionSample.w < 0.5) {
        textureStore(OutputImage, pixel, vec4f(0.0, 0.0, 0.0, 1.0));
        return;
    }
    let normal = normalize(textureLoad(GNormalRoughness, pixel, 0).xyz);
    let position = positionSample.xyz;
    let cardId = textureLoad(SurfaceCardId, pixel, 0).r;
    if (cardId > 0u && distance(position.xz, P.vehicle.xz) > VEHICLE_DYNAMIC_GI_RADIUS) {
        let cached = sampleSurfaceCard(cardId, position);
        if (surfaceCacheEntryValid(cached)) {
            textureStore(OutputImage, pixel, vec4f(max(cached.indirect.rgb, vec3f(0.0)), 1.0));
            return;
        }
    }
    if (!surfaceCacheShouldUpdate(pixel, positionSample, normal)) {
        textureStore(OutputImage, pixel, vec4f(0.0, 0.0, 0.0, 0.0));
        return;
    }
    let albedo = textureLoad(GAlbedoMetallic, pixel, 0).rgb;
    let indirect = diffuseGi(position, normal, albedo);
    textureStore(OutputImage, pixel, vec4f(max(indirect, vec3f(0.0)), 1.0));
}
`;

export const COMPOSE_WGSL = /* wgsl */ `${FRAME_TYPES}
@group(0) @binding(1) var GPosition: texture_2d<f32>;
@group(0) @binding(2) var GNormalRoughness: texture_2d<f32>;
@group(0) @binding(3) var GAlbedoMetallic: texture_2d<f32>;
${GLOBAL_FIELD_SAMPLING}
@group(0) @binding(11) var DirectImage: texture_2d<f32>;
@group(0) @binding(12) var IndirectImage: texture_2d<f32>;
@group(0) @binding(13) var OutputImage: texture_storage_2d<rgba16float, write>;
fn skyRadiance(direction: vec3f) -> vec3f {
    let horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    let gradient = mix(vec3f(0.018, 0.027, 0.044), vec3f(0.30, 0.43, 0.56), pow(horizon, 1.2));
    let sunLobe = pow(max(dot(direction, normalize(P.sun.xyz)), 0.0), 180.0) * vec3f(1.0, 0.68, 0.35) * 1.5;
    return (gradient + sunLobe) * P.sky.w;
}
fn ambientOcclusion(position: vec3f, normal: vec3f) -> f32 {
    let voxel = sampleGlobalVoxelSize(position);
    let bias = max(voxel * 0.38, 0.025);
    let distances = array<f32, 6>(0.16, 0.28, 0.43, 0.62, 0.86, 1.15);
    let weights = array<f32, 6>(0.28, 0.24, 0.19, 0.13, 0.10, 0.06);
    var occlusion = 0.0;
    for (var index = 0u; index < 6u; index++) {
        let radius = distances[index];
        if (radius < voxel * 0.85) { continue; }
        let measured = sampleGlobalDistance(position + normal * radius);
        let cavity = clamp((radius - measured - bias) / radius, 0.0, 1.0);
        occlusion += smoothstep(0.0, 1.0, cavity) * weights[index];
    }
    let strength = clamp(P.gi.z, 0.0, 2.0) * 0.22;
    return clamp(1.0 - occlusion * strength, 0.56, 1.0);
}
fn fresnelSchlick(cosine: f32, f0: vec3f) -> vec3f {
    return f0 + (vec3f(1.0) - f0) * pow(1.0 - clamp(cosine, 0.0, 1.0), 5.0);
}
fn fieldDebugColor(distance: f32) -> vec3f {
    if (distance < 0.0) {
        return mix(vec3f(0.88, 0.12, 0.27), vec3f(1.0, 0.50, 0.14), clamp(-distance * 0.24, 0.0, 1.0));
    }
    let proximity = exp(-distance * 0.34);
    return mix(vec3f(0.018, 0.035, 0.065), vec3f(0.08, 0.72, 0.68), proximity);
}
fn clipmapInfluenceColor(position: vec3f) -> vec3f {
    let fineDistance = max(max(abs(position.x - P.center0.x), abs(position.y - P.center0.y)), abs(position.z - P.center0.z));
    let middleDistance = max(max(abs(position.x - P.center1.x), abs(position.y - P.center1.y)), abs(position.z - P.center1.z));
    let coarseDistance = max(max(abs(position.x - P.center2.x), abs(position.y - P.center2.y)), abs(position.z - P.center2.z));
    let fineColor = vec3f(0.02, 0.92, 0.42);
    let middleColor = vec3f(0.04, 0.46, 1.0);
    let coarseColor = vec3f(1.0, 0.39, 0.035);
    let fineBlendStart = P.cascadeExtents.x * 0.28;
    let fineBlendEnd = P.cascadeExtents.x * 0.46;
    if (fineDistance <= fineBlendStart) { return fineColor; }
    if (fineDistance < fineBlendEnd) {
        return mix(fineColor, middleColor, smoothstep(fineBlendStart, fineBlendEnd, fineDistance));
    }
    let middleBlendStart = P.cascadeExtents.y * 0.32;
    let middleBlendEnd = P.cascadeExtents.y * 0.46;
    if (middleDistance <= middleBlendStart) { return middleColor; }
    if (middleDistance < middleBlendEnd) {
        return mix(middleColor, coarseColor, smoothstep(middleBlendStart, middleBlendEnd, middleDistance));
    }
    if (coarseDistance > P.cascadeExtents.z * 0.5) { return vec3f(0.055, 0.065, 0.085); }
    return coarseColor;
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let width = u32(P.viewport.x + 0.5);
    let height = u32(P.viewport.y + 0.5);
    if (id.x >= width || id.y >= height) { return; }
    let pixel = vec2i(id.xy);
    let positionSample = textureLoad(GPosition, pixel, 0);
    var linearColor = vec3f(0.0);
    if (positionSample.w < 0.5) {
        let direction = normalize(vec3f((f32(id.x) / f32(width) - 0.5) * 1.7, 0.32, -1.0));
        linearColor = skyRadiance(direction);
    } else {
        let mode = u32(P.modeQuality.x + 0.5);
        if ((mode == 0u && (textureLoad(DirectImage, pixel, 0).a < 0.5 || textureLoad(IndirectImage, pixel, 0).a < 0.5)) ||
            (mode == 2u && textureLoad(DirectImage, pixel, 0).a < 0.5) ||
            (mode == 3u && textureLoad(IndirectImage, pixel, 0).a < 0.5)) {
            textureStore(OutputImage, pixel, vec4f(0.0, 0.0, 0.0, 0.0));
            return;
        }
        let position = positionSample.xyz;
        let normalData = textureLoad(GNormalRoughness, pixel, 0);
        let materialData = textureLoad(GAlbedoMetallic, pixel, 0);
        let normal = normalize(normalData.xyz);
        let albedo = materialData.rgb;
        let roughness = clamp(normalData.w, 0.025, 1.0);
        let metallic = clamp(materialData.w, 0.0, 1.0);
        if (mode == 1u) {
            linearColor = fieldDebugColor(sampleGlobalDistance(position)) * 1.35;
        } else if (mode == 2u) {
            linearColor = textureLoad(DirectImage, pixel, 0).rgb;
        } else if (mode == 3u) {
            linearColor = textureLoad(IndirectImage, pixel, 0).rgb;
        } else if (mode == 4u) {
            linearColor = normal * 0.5 + vec3f(0.5);
        } else if (mode == 5u) {
            linearColor = albedo;
        } else if (mode == 6u) {
            linearColor = vec3f(ambientOcclusion(position + normal * 0.025, normal));
        } else if (mode == 7u) {
            linearColor = clipmapInfluenceColor(position);
        } else {
            let direct = textureLoad(DirectImage, pixel, 0).rgb;
            let indirect = textureLoad(IndirectImage, pixel, 0).rgb;
            let ao = ambientOcclusion(position + normal * 0.025, normal);
            let skyFill = albedo * skyRadiance(normal) * (0.14 * ao);
            let f0 = mix(vec3f(0.04), albedo, metallic);
            let fresnel = fresnelSchlick(max(dot(normal, normalize(P.cameraPosition.xyz - position)), 0.0), f0);
            let specularEnvironment = fresnel * skyRadiance(reflect(-normalize(P.cameraPosition.xyz - position), normal)) *
                                      clamp(1.0 - roughness * 1.2, 0.0, 1.0);
            linearColor = direct + skyFill + indirect + specularEnvironment;
        }
    }
    textureStore(OutputImage, pixel, vec4f(max(linearColor, vec3f(0.0)) * P.environment.w, 1.0));
}
`;

export const TEMPORAL_WGSL = /* wgsl */ `${FRAME_TYPES}
@group(0) @binding(1) var CurrentImage: texture_2d<f32>;
@group(0) @binding(2) var CurrentPosition: texture_2d<f32>;
@group(0) @binding(3) var CurrentNormal: texture_2d<f32>;
@group(0) @binding(4) var HistoryImage: texture_2d<f32>;
@group(0) @binding(5) var HistoryPosition: texture_2d<f32>;
@group(0) @binding(6) var HistoryNormal: texture_2d<f32>;
@group(0) @binding(7) var TemporalOutput: texture_storage_2d<rgba16float, write>;
fn maxComponent(value: vec3f) -> f32 { return max(value.x, max(value.y, value.z)); }
fn previousSurfacePosition(position: vec3f, dynamicSurface: bool) -> vec3f {
    if (!dynamicSurface) { return position; }
    // Recover the car's local point, then transform it by its previous-frame pose.
    // This is the rigid-object world-space motion vector used to generate the
    // screen-space history reprojection without an extra full-resolution target.
    let delta = position - P.vehicle.xyz;
    let currentC = cos(P.vehicle.w);
    let currentS = sin(P.vehicle.w);
    let localPosition = vec3f(currentC * delta.x - currentS * delta.z, delta.y,
                               currentS * delta.x + currentC * delta.z);
    let previousC = cos(P.previousVehicle.w);
    let previousS = sin(P.previousVehicle.w);
    return vec3f(P.previousVehicle.x + previousC * localPosition.x + previousS * localPosition.z,
                 P.previousVehicle.y + localPosition.y,
                 P.previousVehicle.z - previousS * localPosition.x + previousC * localPosition.z);
}
fn previousSurfaceNormal(normal: vec3f, dynamicSurface: bool) -> vec3f {
    if (!dynamicSurface) { return normal; }
    let currentC = cos(P.vehicle.w);
    let currentS = sin(P.vehicle.w);
    let localNormal = vec3f(currentC * normal.x - currentS * normal.z, normal.y,
                             currentS * normal.x + currentC * normal.z);
    let previousC = cos(P.previousVehicle.w);
    let previousS = sin(P.previousVehicle.w);
    return normalize(vec3f(previousC * localNormal.x + previousS * localNormal.z, localNormal.y,
                           -previousS * localNormal.x + previousC * localNormal.z));
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let dimensions = textureDimensions(CurrentImage);
    if (id.x >= dimensions.x || id.y >= dimensions.y) { return; }
    let pixel = vec2i(id.xy);
    let current = textureLoad(CurrentImage, pixel, 0);
    let position = textureLoad(CurrentPosition, pixel, 0);
    let normal = normalize(textureLoad(CurrentNormal, pixel, 0).xyz);
    let dynamicSurface = position.w > 1.5;
    let mode = u32(P.modeQuality.x + 0.5);
    let lightingMode = mode == 0u || mode == 2u || mode == 3u;
    if (P.emissiveParams.z < 0.5 || P.emissiveParams.y < 0.5 || !lightingMode || position.w < 0.5) {
        textureStore(TemporalOutput, pixel, vec4f(current.rgb, 1.0));
        return;
    }

    let previousWorld = previousSurfacePosition(position.xyz, dynamicSurface);
    let worldMotion = previousWorld - position.xyz;
    let objectId = surfaceVisibilityId(position.w);
    let rotatingWheel = dynamicSurface && objectId >= 59u && objectId <= 62u;
    let movingVehicleInfluence = !dynamicSurface && vehicleMoved() &&
        distance(position.xz, P.vehicle.xz) < VEHICLE_DYNAMIC_GI_RADIUS;
    if (movingVehicleInfluence || rotatingWheel || (dynamicSurface && length(worldMotion) > 0.01)) {
        // Nearby vehicle-affected GI is reactive, not accumulated from a stale
        // frame. Stable distant walls still use motion-aware reprojection.
        textureStore(TemporalOutput, pixel, vec4f(current.rgb, 1.0));
        return;
    }
    let expectedPreviousNormal = previousSurfaceNormal(normal, dynamicSurface);
    let previousClip = P.previousViewProjection * vec4f(previousWorld, 1.0);
    if (previousClip.w <= 1e-4) {
        textureStore(TemporalOutput, pixel, vec4f(current.rgb, 1.0));
        return;
    }
    let previousUv = previousClip.xy / previousClip.w * vec2f(0.5, -0.5) + vec2f(0.5);
    let currentUv = (vec2f(pixel) + vec2f(0.5)) / vec2f(dimensions);
    let motionVector = previousUv - currentUv;
    let reprojectedUv = currentUv + motionVector;
    if (any(reprojectedUv <= vec2f(0.0)) || any(reprojectedUv >= vec2f(1.0))) {
        textureStore(TemporalOutput, pixel, vec4f(current.rgb, 1.0));
        return;
    }
    let previousPixel = clamp(vec2i(reprojectedUv * vec2f(dimensions)), vec2i(0), vec2i(dimensions) - vec2i(1));
    let oldPosition = textureLoad(HistoryPosition, previousPixel, 0);
    let oldNormal = normalize(textureLoad(HistoryNormal, previousPixel, 0).xyz);
    let positionTolerance = select(0.20, 0.075, dynamicSurface);
    if (oldPosition.w < 0.5 || !sameSurfaceVisibility(oldPosition.w, position.w) ||
        distance(oldPosition.xyz, previousWorld) > positionTolerance || dot(oldNormal, expectedPreviousNormal) < 0.86) {
        textureStore(TemporalOutput, pixel, vec4f(current.rgb, 1.0));
        return;
    }

    let history = textureLoad(HistoryImage, previousPixel, 0);
    if (history.a < 1.0) {
        textureStore(TemporalOutput, pixel, vec4f(current.rgb, 1.0));
        return;
    }
    if (current.a < 0.5) {
        // This surface intentionally skipped its lighting update this frame.
        textureStore(TemporalOutput, pixel, history);
        return;
    }
    let historyLength = clamp(history.a, 1.0, 16.0);
    let change = length(current.rgb - history.rgb);
    let signal = max(maxComponent(current.rgb), maxComponent(history.rgb));
    let relativeChange = change / max(signal, 0.08);
    let reactiveLighting = change > (0.10 + signal * 0.10) && relativeChange > 0.18;
    var currentWeight = 1.0 / min(historyLength + 1.0, 16.0);
    var nextLength = min(historyLength + 1.0, 16.0);
    if (reactiveLighting) {
        // Even on otherwise stable objects, changing visibility abandons most
        // history so dynamic GI does not linger after a blocker moves.
        currentWeight = 0.78;
        nextLength = 2.0;
    }
    let accumulated = mix(history.rgb, current.rgb, currentWeight);
    textureStore(TemporalOutput, pixel, vec4f(accumulated, nextLength));
}
`;

export const DENOISE_WGSL = /* wgsl */ `
fn denoiseVisibilityId(surfaceCode: f32) -> u32 {
    let classBase = select(1.0, 2.0, surfaceCode > 1.5);
    return u32(round(max(surfaceCode - classBase, 0.0) * 256.0));
}
fn sameDenoiseSurface(a: f32, b: f32) -> bool {
    return (a > 1.5) == (b > 1.5) && denoiseVisibilityId(a) == denoiseVisibilityId(b);
}
@group(0) @binding(0) var Source: texture_2d<f32>;
@group(0) @binding(1) var GPosition: texture_2d<f32>;
@group(0) @binding(2) var GNormal: texture_2d<f32>;
@group(0) @binding(3) var GAlbedo: texture_2d<f32>;
@group(0) @binding(4) var DenoisedOutput: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let dimensions = textureDimensions(Source);
    if (id.x >= dimensions.x || id.y >= dimensions.y) { return; }
    let pixel = vec2i(id.xy);
    let centerColor = textureLoad(Source, pixel, 0);
    let centerPosition = textureLoad(GPosition, pixel, 0);
    let centerNormal = normalize(textureLoad(GNormal, pixel, 0).xyz);
    let centerAlbedo = textureLoad(GAlbedo, pixel, 0).rgb;
    let maximumPixel = vec2i(dimensions) - vec2i(1);
    var colorSum = centerColor.rgb;
    var weightSum = 1.0;

    // A 5x5 guided filter averages low-ray-count GI noise over a larger footprint,
    // while world position, normal and albedo guides preserve silhouettes/material edges.
    for (var y = -2; y <= 2; y++) {
        for (var x = -2; x <= 2; x++) {
            if (x == 0 && y == 0) { continue; }
            let neighborPixel = clamp(pixel + vec2i(x, y), vec2i(0), maximumPixel);
            let neighborPosition = textureLoad(GPosition, neighborPixel, 0);
            var weight = exp(-0.28 * f32(x * x + y * y));
            if (centerPosition.w > 0.5) {
                if (neighborPosition.w <= 0.5 || !sameDenoiseSurface(centerPosition.w, neighborPosition.w)) { continue; }
                let neighborNormal = normalize(textureLoad(GNormal, neighborPixel, 0).xyz);
                let normalAgreement = max(dot(centerNormal, neighborNormal), 0.0);
                let normalWeight = pow(normalAgreement, 16.0);
                let depthDelta = abs(dot(centerNormal, neighborPosition.xyz - centerPosition.xyz));
                let depthWeight = exp(-depthDelta * 14.0);
                let neighborAlbedo = textureLoad(GAlbedo, neighborPixel, 0).rgb;
                let albedoWeight = exp(-length(neighborAlbedo - centerAlbedo) * 10.0);
                weight *= normalWeight * depthWeight * albedoWeight;
            } else if (neighborPosition.w > 0.5) {
                continue;
            }
            colorSum += textureLoad(Source, neighborPixel, 0).rgb * weight;
            weightSum += weight;
        }
    }
    textureStore(DenoisedOutput, pixel, vec4f(colorSum / max(weightSum, 1e-4), centerColor.a));
}
`;

export const PRESENT_WGSL = /* wgsl */ `
struct VertexOutput { @builtin(position) position: vec4f, @location(0) uv: vec2f };
@group(0) @binding(0) var Image: texture_2d<f32>;
@group(0) @binding(1) var ImageSampler: sampler;
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    var output: VertexOutput;
    let position = positions[index];
    output.position = vec4f(position, 0.0, 1.0);
    output.uv = position * vec2f(0.5, -0.5) + vec2f(0.5);
    return output;
}
fn aces(color: vec3f) -> vec3f {
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
}
fn hash21(point: vec2f) -> f32 {
    return fract(sin(dot(point, vec2f(127.1, 311.7))) * 43758.5453123);
}
@fragment fn fragmentMain(input: VertexOutput) -> @location(0) vec4f {
    let hdr = textureSampleLevel(Image, ImageSampler, input.uv, 0.0).rgb;
    let display = pow(aces(max(hdr, vec3f(0.0))), vec3f(1.0 / 2.2));
    let dither = (hash21(floor(input.position.xy) + vec2f(0.37, 0.73)) - 0.5) / 255.0;
    return vec4f(clamp(display + vec3f(dither), vec3f(0.0), vec3f(1.0)), 1.0);
}
`;

export const FIELD_MAP_WGSL = /* wgsl */ `${FRAME_TYPES}
@group(0) @binding(1) var GlobalDistance: texture_3d<f32>;
@group(0) @binding(2) var VehicleDistance: texture_3d<f32>;
@group(0) @binding(3) var<uniform> Asset: AssetParams;
@group(0) @binding(4) var<storage, read> Shapes: array<StaticShape>;
fn loadGlobalCell(coordinate: vec3i) -> f32 {
    let resolution = i32(P.cascadeExtents.w + 0.5);
    let clamped = clamp(coordinate, vec3i(0), vec3i(resolution - 1));
    let centerCell = vec3i(round(P.center0.xyz / voxelSize(0u)));
    let worldCell = centerCell + clamped - vec3i(resolution / 2);
    let storageCell = vec3i(
        wrapFieldCell(worldCell.x, resolution),
        wrapFieldCell(worldCell.y, resolution),
        wrapFieldCell(worldCell.z, resolution),
    );
    return textureLoad(GlobalDistance, storageCell, 0).r;
}
fn sampleGlobalLevel(position: vec3f) -> f32 {
    if ((u32(P.wheelSteering.w + 0.5) & 1u) == 0u) { return analyticFieldDistance(position); }
    let extent = P.cascadeExtents.x;
    let relative = (position - P.center0.xyz) / extent + vec3f(0.5);
    if (any(relative < vec3f(0.0)) || any(relative > vec3f(1.0))) { return 1e4; }
    let coordinate = relative * P.cascadeExtents.w - vec3f(0.5);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let a = mix(loadGlobalCell(base), loadGlobalCell(base + vec3i(1, 0, 0)), fraction.x);
    let b = mix(loadGlobalCell(base + vec3i(0, 1, 0)), loadGlobalCell(base + vec3i(1, 1, 0)), fraction.x);
    let c = mix(loadGlobalCell(base + vec3i(0, 0, 1)), loadGlobalCell(base + vec3i(1, 0, 1)), fraction.x);
    let d = mix(loadGlobalCell(base + vec3i(0, 1, 1)), loadGlobalCell(base + vec3i(1, 1, 1)), fraction.x);
    return mix(mix(a, b, fraction.y), mix(c, d, fraction.y), fraction.z);
}
fn sampleVehicleDistance(localPosition: vec3f) -> f32 {
    if (any(localPosition < Asset.minimum.xyz) || any(localPosition > Asset.maximum.xyz)) { return 1e4; }
    let coordinate = (localPosition - Asset.minimum.xyz) / (Asset.maximum.xyz - Asset.minimum.xyz) * vec3f(Asset.dimensions.xyz) - vec3f(0.5);
    let base = vec3i(floor(coordinate));
    let fraction = fract(coordinate);
    let limit = vec3i(Asset.dimensions.xyz) - vec3i(1);
    let lo = clamp(base, vec3i(0), limit);
    let hi = clamp(base + vec3i(1), vec3i(0), limit);
    let x00 = mix(textureLoad(VehicleDistance, vec3i(lo.x, lo.y, lo.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, lo.y, lo.z), 0).r, fraction.x);
    let x10 = mix(textureLoad(VehicleDistance, vec3i(lo.x, hi.y, lo.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, hi.y, lo.z), 0).r, fraction.x);
    let x01 = mix(textureLoad(VehicleDistance, vec3i(lo.x, lo.y, hi.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, lo.y, hi.z), 0).r, fraction.x);
    let x11 = mix(textureLoad(VehicleDistance, vec3i(lo.x, hi.y, hi.z), 0).r, textureLoad(VehicleDistance, vec3i(hi.x, hi.y, hi.z), 0).r, fraction.x);
    return mix(mix(x00, x10, fraction.y), mix(x01, x11, fraction.y), fraction.z);
}
struct FieldVertex { @builtin(position) position: vec4f };
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> FieldVertex {
    var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    var output: FieldVertex;
    output.position = vec4f(positions[index], 0.0, 1.0);
    return output;
}
fn analyticFieldDistance(position: vec3f) -> f32 {
    var distance = position.y;
    let count = u32(P.gi.w + 0.5);
    for (var index = 0u; index < count; index++) {
        distance = min(distance, sdShape(position, Shapes[index]));
    }
    return distance;
}
fn refineFieldSliceDistance(position: vec3f, coarseDistance: f32) -> f32 {
    let voxel = P.cascadeExtents.x / P.cascadeExtents.w;
    if (abs(coarseDistance) > max(voxel * 1.5, 0.035)) { return coarseDistance; }
    let localPosition = worldToVehicle(position);
    var vehicleDistance = 1e4;
    if (all(localPosition >= Asset.minimum.xyz) && all(localPosition <= Asset.maximum.xyz)) {
        vehicleDistance = sampleVehicleDistance(localPosition);
    }
    return min(coarseDistance, vehicleDistance);
}
fn fieldPalette(distance: f32) -> vec3f {
    if (distance < -0.01) {
        return mix(vec3f(0.95, 0.12, 0.22), vec3f(1.0, 0.63, 0.12), clamp(-distance * 0.3, 0.0, 1.0));
    }
    let surfaceBand = (P.cascadeExtents.x / P.cascadeExtents.w) * 1.35;
    if (distance < surfaceBand) { return vec3f(0.96, 0.82, 0.43); }
    let falloff = exp(-distance * 0.28);
    return mix(vec3f(0.015, 0.035, 0.065), vec3f(0.09, 0.67, 0.68), falloff);
}
@fragment fn fragmentMain(@builtin(position) pixel: vec4f) -> @location(0) vec4f {
    let size = vec2f(P.environment.y, P.environment.z);
    let uv = pixel.xy / max(size, vec2f(1.0));
    let extent = P.cascadeExtents.x;
    let center = P.center0.xyz;
    let world = vec3f(center.x + (uv.x - 0.5) * extent, P.environment.x, center.z + (uv.y - 0.5) * extent);
    let coarseDistance = sampleGlobalLevel(world);
    let distance = refineFieldSliceDistance(world, coarseDistance);
    var color = fieldPalette(distance);
    let voxel = P.cascadeExtents.x / P.cascadeExtents.w;
    let localVehicle = worldToVehicle(world);
    if (all(localVehicle >= Asset.minimum.xyz) && all(localVehicle <= Asset.maximum.xyz)) {
        let objectDistance = sampleVehicleDistance(localVehicle);
        if (abs(objectDistance) < voxel * 0.42) { color = vec3f(1.0, 0.92, 0.63); }
    }
    let border = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    if (border < 0.009) { color = vec3f(0.55, 0.82, 0.76); }
    if ((abs(uv.x - 0.5) < 0.004 && abs(uv.y - 0.5) < 0.04) ||
        (abs(uv.y - 0.5) < 0.004 && abs(uv.x - 0.5) < 0.04)) { color = vec3f(0.91, 0.96, 0.88); }
    return vec4f(color, 1.0);
}
`;

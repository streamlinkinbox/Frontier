import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import polygonClipping from 'polygon-clipping';
import {
  bridgeHeightAt,
  edgeLength,
  getDegrees,
  pointAtDistance,
  pointOnEdge,
  sampleEdge,
  tangentOnEdge,
} from './road-network.js';

const PALETTE = {
  asphalt: 0x383c42,
  asphaltSide: 0x2d3137,
  pavement: 0x97999a,
  curb: 0xb7b4ad,
  whitePaint: 0xd6d4ce,
  yellowPaint: 0xd2ae70,
  bridge: 0x656a70,
  bridgeDark: 0x41464c,
  bridgeLight: 0x85888a,
  concrete: 0x767a7c,
  concreteDark: 0x55595b,
  point: 0xb8bec4,
  pointSelected: 0xe2b678,
  handle: 0xf2c985,
};

const mat = (color, options = {}) => new THREE.MeshStandardMaterial({
  color,
  roughness: 0.86,
  metalness: 0.02,
  side: THREE.DoubleSide,
  ...options,
});

function disposeGroup(group) {
  for (const child of [...group.children]) {
    child.traverse((object) => {
      if (object.geometry) object.geometry.dispose();
      if (object.material?.userData?.ownedMaterial) object.material.dispose();
    });
  }
  group.clear();
}

function cleanRing(ring) {
  const points = ring.map(([x, z]) => ({ x: Number(x), z: Number(z) }));
  if (points.length > 2 && Math.hypot(points[0].x - points.at(-1).x, points[0].z - points.at(-1).z) < 1e-7) points.pop();
  return points.filter((point, index) => index === 0 || Math.hypot(point.x - points[index - 1].x, point.z - points[index - 1].z) > 1e-7);
}

function unit(dx, dz) {
  const length = Math.hypot(dx, dz) || 1;
  return { x: dx / length, z: dz / length };
}

function offsetPath(points, offset) {
  return points.map((point, index) => {
    const previous = points[Math.max(0, index - 1)];
    const next = points[Math.min(points.length - 1, index + 1)];
    const before = unit(point.x - previous.x, point.z - previous.z);
    const after = unit(next.x - point.x, next.z - point.z);
    const n1 = { x: -before.z, z: before.x };
    const n2 = { x: -after.z, z: after.x };
    const miter = unit(n1.x + n2.x, n1.z + n2.z);
    const denominator = miter.x * n2.x + miter.z * n2.z;
    const scale = Math.min(Math.abs(offset) * 3.5, Math.abs(offset / (Math.abs(denominator) < 0.18 ? 0.18 : denominator)));
    const sign = offset < 0 ? -1 : 1;
    return { x: point.x + miter.x * scale * sign, z: point.z + miter.z * scale * sign };
  });
}

function corridorRing(points, width) {
  const half = width * 0.5;
  const left = offsetPath(points, half);
  const right = offsetPath(points, -half).reverse();
  return [...left, ...right].map(({ x, z }) => [x, z]);
}

function bandRing(points, innerOffset, outerOffset) {
  const outer = offsetPath(points, outerOffset);
  const inner = offsetPath(points, innerOffset).reverse();
  return [...outer, ...inner].map(({ x, z }) => [x, z]);
}

function toClipPolygon(ring) {
  if (ring.length < 3) return null;
  const cleaned = ring.map(([x, z]) => [x, z]);
  cleaned.push([...cleaned[0]]);
  return [cleaned];
}

function shapeFromPolygon(polygon) {
  if (!polygon?.[0]?.length) return null;
  const outer = cleanRing(polygon[0]);
  if (outer.length < 3) return null;
  const shape = new THREE.Shape();
  shape.moveTo(outer[0].x, -outer[0].z);
  for (let i = 1; i < outer.length; i += 1) shape.lineTo(outer[i].x, -outer[i].z);
  shape.closePath();
  for (const holeRing of polygon.slice(1)) {
    const hole = cleanRing(holeRing);
    if (hole.length < 3) continue;
    const path = new THREE.Path();
    path.moveTo(hole[0].x, -hole[0].z);
    for (let i = 1; i < hole.length; i += 1) path.lineTo(hole[i].x, -hole[i].z);
    path.closePath();
    shape.holes.push(path);
  }
  return shape;
}

function addArea(group, multipolygon, y, material, name) {
  if (!Array.isArray(multipolygon)) return;
  let count = 0;
  for (const polygon of multipolygon) {
    const shape = shapeFromPolygon(polygon);
    if (!shape) continue;
    const geometry = new THREE.ShapeGeometry(shape, 1);
    geometry.rotateX(-Math.PI / 2);
    geometry.translate(0, y, 0);
    geometry.computeVertexNormals();
    const mesh = new THREE.Mesh(geometry, material);
    mesh.name = name;
    mesh.receiveShadow = true;
    mesh.castShadow = false;
    group.add(mesh);
    count += 1;
  }
  return count;
}

function mergePolygons(polygons) {
  const valid = polygons.filter(Boolean);
  if (!valid.length) return [];
  try {
    return polygonClipping.union(...valid);
  } catch (error) {
    console.warn('Road surface union failed; drawing individual road polygons instead.', error);
    return valid.map((polygon) => polygon);
  }
}

function subtractPolygons(subject, clip) {
  if (!subject?.length) return [];
  if (!clip?.length) return subject;
  try {
    return polygonClipping.difference(subject, clip);
  } catch (error) {
    console.warn('Pavement cleanup failed; retaining the untrimmed pavement surface.', error);
    return subject;
  }
}

function profileAtBoundaryPoint(point, profiles) {
  let nearest = null;
  for (const profile of profiles) {
    const samples = profile.samples;
    for (let i = 0; i < samples.length - 1; i += 1) {
      const a = samples[i];
      const b = samples[i + 1];
      const dx = b.x - a.x;
      const dz = b.z - a.z;
      const denominator = dx * dx + dz * dz || 1;
      const t = Math.max(0, Math.min(1, ((point.x - a.x) * dx + (point.z - a.z) * dz) / denominator));
      const x = a.x + dx * t;
      const z = a.z + dz * t;
      const distance = Math.hypot(point.x - x, point.z - z);
      if (!nearest || distance < nearest.distance) nearest = { distance, profile };
    }
  }
  return nearest?.profile ?? { width: 0.22, height: 0.18 };
}

function boundaryCurbGeometry(multipolygon, bottomY, profiles) {
  const positions = [];
  const indices = [];
  for (const polygon of multipolygon ?? []) {
    for (const ring of polygon) {
      const points = cleanRing(ring);
      if (points.length < 3) continue;
      const base = positions.length / 3;
      for (let i = 0; i < points.length; i += 1) {
        const point = points[i];
        const previous = points[(i - 1 + points.length) % points.length];
        const next = points[(i + 1) % points.length];
        const before = unit(point.x - previous.x, point.z - previous.z);
        const after = unit(next.x - point.x, next.z - point.z);
        const n1 = { x: -before.z, z: before.x };
        const n2 = { x: -after.z, z: after.x };
        const miter = unit(n1.x + n2.x, n1.z + n2.z);
        const denominator = miter.x * n2.x + miter.z * n2.z;
        const profile = profileAtBoundaryPoint(point, profiles);
        const offset = Math.min(profile.width * 1.8, profile.width * 0.5 / Math.max(0.22, Math.abs(denominator)));
        const ix = point.x - miter.x * offset;
        const iz = point.z - miter.z * offset;
        const ox = point.x + miter.x * offset;
        const oz = point.z + miter.z * offset;
        const topY = bottomY + profile.height;
        positions.push(ix, topY, iz, ox, topY, oz, ix, bottomY, iz, ox, bottomY, oz);
      }
      for (let i = 0; i < points.length; i += 1) {
        const a = base + i * 4;
        const b = base + ((i + 1) % points.length) * 4;
        indices.push(a, a + 1, b, a + 1, b + 1, b);
        indices.push(a, b, a + 2, b, b + 2, a + 2);
        indices.push(a + 1, a + 3, b + 1, a + 3, b + 3, b + 1);
        indices.push(a + 2, b + 2, a + 3, b + 2, b + 3, a + 3);
      }
    }
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function ribbonGeometry(samples, innerOffset, outerOffset, yOffset = 0) {
  const positions = [];
  const indices = [];
  for (let i = 0; i < samples.length; i += 1) {
    const current = samples[i];
    const previous = samples[Math.max(0, i - 1)];
    const next = samples[Math.min(samples.length - 1, i + 1)];
    const direction = unit(next.x - previous.x, next.z - previous.z);
    const normal = { x: -direction.z, z: direction.x };
    const inner = { x: current.x + normal.x * innerOffset, z: current.z + normal.z * innerOffset };
    const outer = { x: current.x + normal.x * outerOffset, z: current.z + normal.z * outerOffset };
    positions.push(inner.x, current.y + yOffset, inner.z, outer.x, current.y + yOffset, outer.z);
  }
  for (let i = 0; i < samples.length - 1; i += 1) {
    const a = i * 2;
    const b = a + 2;
    indices.push(a, b, a + 1, a + 1, b, b + 1);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function roadWidth(edge) {
  return Math.max(2.5, Number(edge.laneCount ?? 2) * Number(edge.laneWidth ?? 3.5) + Number(edge.shoulder ?? 0.3) * 2);
}

function surfacePolygons(network, edges) {
  const roadPolygons = [];
  const pavePolygons = [];
  for (const edge of edges) {
    const length = edgeLength(edge, network.nodes, 24);
    const samples = sampleEdge(edge, network.nodes, Math.max(24, Math.ceil(length * 1.6))).map(({ x, z }) => ({ x, z }));
    if (samples.length < 2) continue;
    roadPolygons.push(toClipPolygon(corridorRing(samples, roadWidth(edge))));
    if (edge.pavementEnabled && Number(edge.pavementWidth) > 0) {
      const half = roadWidth(edge) * 0.5;
      const curb = Number(edge.curbWidth ?? 0.22);
      const walk = Number(edge.pavementWidth ?? 0);
      if (walk > 0) {
        pavePolygons.push(toClipPolygon(bandRing(samples, half + curb, half + curb + walk)));
        pavePolygons.push(toClipPolygon(bandRing(samples, -half - curb - walk, -half - curb)));
      }
    }
  }
  const roadUnion = mergePolygons(roadPolygons);
  const pavementUnion = mergePolygons(pavePolygons);
  return { roadUnion, pavementUnion, pavementOnly: subtractPolygons(pavementUnion, roadUnion) };
}

function makeBoundaryCurbs(group, roadUnion, edges, network, material) {
  if (!roadUnion.length || !edges.length) return;
  const profiles = edges.map((edge) => ({
    width: Number(edge.curbWidth ?? 0.22),
    height: Number(edge.curbHeight ?? 0.18),
    samples: sampleEdge(edge, network.nodes, Math.max(24, Math.ceil(edgeLength(edge, network.nodes, 24) * 1.4))),
  }));
  if (profiles.every((profile) => profile.height <= 0 || profile.width <= 0)) return;
  const geometry = boundaryCurbGeometry(roadUnion, 0.13, profiles);
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = 'Continuous curb network';
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  group.add(mesh);
}

function lineTube(group, points, radius, material, closed = false, segments = null) {
  if (points.length < 2) return null;
  const vectors = points.map((point) => new THREE.Vector3(point.x, point.y, point.z));
  const curve = new THREE.CatmullRomCurve3(vectors, closed, 'centripetal');
  const tubularSegments = segments ?? Math.max(12, points.length * 2);
  const geometry = new THREE.TubeGeometry(curve, tubularSegments, radius, 6, closed);
  const mesh = new THREE.Mesh(geometry, material);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  group.add(mesh);
  return mesh;
}

function makeLinearCurb(group, samples, lateral, width, height, material) {
  const positions = [];
  const indices = [];
  for (let i = 0; i < samples.length; i += 1) {
    const current = samples[i];
    const previous = samples[Math.max(0, i - 1)];
    const next = samples[Math.min(samples.length - 1, i + 1)];
    const direction = unit(next.x - previous.x, next.z - previous.z);
    const normal = { x: -direction.z, z: direction.x };
    const center = { x: current.x + normal.x * lateral, z: current.z + normal.z * lateral };
    const half = width * 0.5;
    positions.push(
      center.x - normal.x * half, current.y + 0.34, center.z - normal.z * half,
      center.x + normal.x * half, current.y + 0.34, center.z + normal.z * half,
      center.x - normal.x * half, current.y + 0.34 - height, center.z - normal.z * half,
      center.x + normal.x * half, current.y + 0.34 - height, center.z + normal.z * half,
    );
  }
  for (let i = 0; i < samples.length - 1; i += 1) {
    const a = i * 4;
    const b = a + 4;
    indices.push(a, a + 1, b, a + 1, b + 1, b);
    indices.push(a, b, a + 2, b, b + 2, a + 2);
    indices.push(a + 1, a + 3, b + 1, a + 3, b + 3, b + 1);
    indices.push(a + 2, b + 2, a + 3, b + 2, b + 3, a + 3);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  const mesh = new THREE.Mesh(geometry, material);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  group.add(mesh);
}

function addStripeGeometry(group, network, edge, samples, stripeMaterials) {
  if (!edge.markings || Number(edge.laneCount) < 2 || samples.length < 2) return;
  const length = samples.at(-1).distance;
  const degrees = getDegrees(network);
  const startTrim = (degrees[edge.from] ?? 1) > 1 ? roadWidth(edge) * 0.63 : 0.8;
  const endTrim = (degrees[edge.to] ?? 1) > 1 ? roadWidth(edge) * 0.63 : 0.8;
  const laneWidth = Number(edge.laneWidth ?? 3.5);
  const laneCount = Math.max(1, Math.round(Number(edge.laneCount ?? 2)));
  const width = Math.max(0.055, Number(edge.lineWidth ?? 0.075));
  const offsets = [];
  for (let lane = 1; lane < laneCount; lane += 1) {
    const offset = -((laneCount * laneWidth) / 2) + lane * laneWidth;
    const center = laneCount % 2 === 0 && lane === laneCount / 2;
    if (center && laneCount >= 4) {
      offsets.push({ offset: offset - 0.09, center: true });
      offsets.push({ offset: offset + 0.09, center: true });
    } else offsets.push({ offset, center });
  }
  const buckets = { center: { positions: [], indices: [] }, lane: { positions: [], indices: [] } };
  for (const stripe of offsets) {
    const bucket = stripe.center ? buckets.center : buckets.lane;
    for (let distance = startTrim + 0.5; distance < length - endTrim; distance += 5.1) {
      const dashEnd = Math.min(distance + 2.65, length - endTrim);
      if (dashEnd - distance < 0.25) continue;
      const a = pointAtDistance(samples, distance);
      const b = pointAtDistance(samples, dashEnd);
      const direction = unit(b.x - a.x, b.z - a.z);
      const normal = { x: -direction.z, z: direction.x };
      const aCenter = { x: a.x + normal.x * stripe.offset, z: a.z + normal.z * stripe.offset };
      const bCenter = { x: b.x + normal.x * stripe.offset, z: b.z + normal.z * stripe.offset };
      const offsetY = 0.225;
      const startIndex = bucket.positions.length / 3;
      bucket.positions.push(
        aCenter.x - normal.x * width * 0.5, a.y + offsetY, aCenter.z - normal.z * width * 0.5,
        aCenter.x + normal.x * width * 0.5, a.y + offsetY, aCenter.z + normal.z * width * 0.5,
        bCenter.x - normal.x * width * 0.5, b.y + offsetY, bCenter.z - normal.z * width * 0.5,
        bCenter.x + normal.x * width * 0.5, b.y + offsetY, bCenter.z + normal.z * width * 0.5,
      );
      bucket.indices.push(startIndex, startIndex + 2, startIndex + 1, startIndex + 1, startIndex + 2, startIndex + 3);
    }
  }
  for (const [key, bucket] of Object.entries(buckets)) {
    if (!bucket.positions.length) continue;
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(bucket.positions, 3));
    geometry.setIndex(bucket.indices);
    geometry.computeVertexNormals();
    const mesh = new THREE.Mesh(geometry, key === 'center' ? stripeMaterials.yellow : stripeMaterials.white);
    mesh.name = `${edge.name} ${key === 'center' ? 'center line' : 'lane dividers'}`;
    mesh.receiveShadow = false;
    group.add(mesh);
  }
}

function addBox(group, dimensions, position, material, rotationY = 0, castShadow = true) {
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(...dimensions), material);
  mesh.position.set(position.x, position.y, position.z);
  mesh.rotation.y = rotationY;
  mesh.castShadow = castShadow;
  mesh.receiveShadow = true;
  group.add(mesh);
  return mesh;
}

function addCylinder(group, topRadius, bottomRadius, height, position, material, segments = 12) {
  const safeHeight = Math.max(0.12, height);
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(topRadius, bottomRadius, safeHeight, segments, 1), material);
  mesh.position.set(position.x, position.y, position.z);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  group.add(mesh);
  return mesh;
}

function addBeam(group, start, end, radius, material, radialSegments = 8) {
  const a = new THREE.Vector3(start.x, start.y, start.z);
  const b = new THREE.Vector3(end.x, end.y, end.z);
  const direction = new THREE.Vector3().subVectors(b, a);
  const length = direction.length();
  if (length < 0.05) return null;
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, length, radialSegments, 1), material);
  mesh.position.copy(a).add(b).multiplyScalar(0.5);
  mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction.normalize());
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  group.add(mesh);
  return mesh;
}

function bridgeDeckGeometry(samples, halfWidth, thickness) {
  const positions = [];
  const indices = [];
  for (let i = 0; i < samples.length; i += 1) {
    const current = samples[i];
    const previous = samples[Math.max(0, i - 1)];
    const next = samples[Math.min(samples.length - 1, i + 1)];
    const direction = unit(next.x - previous.x, next.z - previous.z);
    const normal = { x: -direction.z, z: direction.x };
    const lx = current.x + normal.x * halfWidth;
    const lz = current.z + normal.z * halfWidth;
    const rx = current.x - normal.x * halfWidth;
    const rz = current.z - normal.z * halfWidth;
    const top = current.y + 0.12;
    const bottom = top - thickness;
    positions.push(lx, top, lz, rx, top, rz, lx, bottom, lz, rx, bottom, rz);
  }
  for (let i = 0; i < samples.length - 1; i += 1) {
    const a = i * 4;
    const b = a + 4;
    // Top, soffit, and longitudinal side faces.
    indices.push(a, b, a + 1, a + 1, b, b + 1);
    indices.push(a + 2, a + 3, b + 2, a + 3, b + 3, b + 2);
    indices.push(a, a + 2, b, a + 2, b + 2, b);
    indices.push(a + 1, b + 1, a + 3, a + 3, b + 1, b + 3);
  }
  const start = 0;
  const end = (samples.length - 1) * 4;
  indices.push(start, start + 1, start + 2, start + 1, start + 3, start + 2);
  indices.push(end, end + 2, end + 1, end + 1, end + 2, end + 3);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function prismAlongPath(samples, lateralOffset, width, topOffset, bottomOffset) {
  const positions = [];
  const indices = [];
  for (let i = 0; i < samples.length; i += 1) {
    const point = samples[i];
    const previous = samples[Math.max(0, i - 1)];
    const next = samples[Math.min(samples.length - 1, i + 1)];
    const direction = unit(next.x - previous.x, next.z - previous.z);
    const normal = { x: -direction.z, z: direction.x };
    const centerX = point.x + normal.x * lateralOffset;
    const centerZ = point.z + normal.z * lateralOffset;
    const leftX = centerX + normal.x * width * 0.5;
    const leftZ = centerZ + normal.z * width * 0.5;
    const rightX = centerX - normal.x * width * 0.5;
    const rightZ = centerZ - normal.z * width * 0.5;
    positions.push(
      leftX, point.y + topOffset, leftZ, rightX, point.y + topOffset, rightZ,
      leftX, point.y + bottomOffset, leftZ, rightX, point.y + bottomOffset, rightZ,
    );
  }
  for (let i = 0; i < samples.length - 1; i += 1) {
    const a = i * 4;
    const b = a + 4;
    indices.push(a, b, a + 1, a + 1, b, b + 1);
    indices.push(a + 2, a + 3, b + 2, a + 3, b + 3, b + 2);
    indices.push(a, a + 2, b, a + 2, b + 2, b);
    indices.push(a + 1, b + 1, a + 3, a + 3, b + 1, b + 3);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function meshWithGeometry(group, geometry, material, name, castShadow = true) {
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = name;
  mesh.castShadow = castShadow;
  mesh.receiveShadow = true;
  group.add(mesh);
  return mesh;
}

function bridgeSupport(group, edge, samples, distance, deckWidth, deckThickness, supportDepth, materials) {
  const point = pointAtDistance(samples, distance);
  const direction = tangentOnEdge(edge, group.userData.nodes ?? {}, point.t);
  const yaw = -Math.atan2(direction.z, direction.x);
  const top = Math.max(0.8, point.y - deckThickness - supportDepth - 0.05);
  const footingHeight = 0.42;
  const groundY = -0.02;
  const height = top - groundY;
  if (height < 1.5) return;
  const type = edge.bridge?.supportType ?? 'twin-column';
  const footingWidth = Math.max(1.7, deckWidth * 0.2);
  addBox(group, [1.7, footingHeight, footingWidth], { x: point.x, y: groundY + footingHeight * 0.5, z: point.z }, materials.concreteDark, yaw);
  const columnTop = top - 0.35;
  const columnHeight = Math.max(0.7, columnTop - (groundY + footingHeight));
  const centerY = groundY + footingHeight + columnHeight * 0.5;
  const columnMaterial = materials.concrete;
  const capY = top - 0.18;
  const capDepth = Math.min(deckWidth * 0.9, deckWidth - 0.35);
  const sideOffset = Math.max(1.35, deckWidth * 0.28);

  if (type === 'hammerhead') {
    addBox(group, [1.55, columnHeight, 1.55], { x: point.x, y: centerY, z: point.z }, columnMaterial, yaw);
    addBox(group, [1.25, 0.48, capDepth], { x: point.x, y: capY, z: point.z }, materials.concreteDark, yaw);
  } else if (type === 'portal') {
    for (const sign of [-1, 1]) {
      const normalOffset = sign * (deckWidth * 0.38);
      const normal = { x: -direction.z, z: direction.x };
      const x = point.x + normal.x * normalOffset;
      const z = point.z + normal.z * normalOffset;
      addBox(group, [0.88, columnHeight, 0.88], { x, y: centerY, z }, columnMaterial, yaw);
    }
    addBox(group, [1.0, 0.45, deckWidth * 0.88], { x: point.x, y: capY, z: point.z }, materials.concreteDark, yaw);
  } else if (type === 'inverted-y') {
    const stemHeight = columnHeight * 0.58;
    const stemTopY = groundY + footingHeight + stemHeight;
    addBox(group, [1.05, stemHeight, 1.05], { x: point.x, y: groundY + footingHeight + stemHeight * 0.5, z: point.z }, columnMaterial, yaw);
    const normal = { x: -direction.z, z: direction.x };
    for (const sign of [-1, 1]) {
      const end = { x: point.x + normal.x * sideOffset, y: top - 0.42, z: point.z + normal.z * sideOffset };
      addBeam(group,
        { x: point.x, y: stemTopY, z: point.z },
        end,
        0.28,
        materials.concreteDark,
      );
    }
    addBox(group, [1.15, 0.38, capDepth], { x: point.x, y: capY, z: point.z }, materials.concreteDark, yaw);
  } else if (type === 'arch') {
    const normal = { x: -direction.z, z: direction.x };
    const left = { x: point.x - normal.x * deckWidth * 0.4, z: point.z - normal.z * deckWidth * 0.4 };
    const right = { x: point.x + normal.x * deckWidth * 0.4, z: point.z + normal.z * deckWidth * 0.4 };
    const archPoints = [];
    for (let i = 0; i <= 24; i += 1) {
      const t = i / 24;
      const x = left.x + (right.x - left.x) * t;
      const z = left.z + (right.z - left.z) * t;
      const y = groundY + 0.55 + Math.sin(Math.PI * t) * Math.max(1.1, Math.min(3.2, height * 0.34));
      archPoints.push({ x, y, z });
    }
    lineTube(group, archPoints, 0.42, materials.concreteDark, false, 36);
    for (const t of [0.2, 0.5, 0.8]) {
      const x = left.x + (right.x - left.x) * t;
      const z = left.z + (right.z - left.z) * t;
      const y = groundY + 0.55 + Math.sin(Math.PI * t) * Math.max(1.1, Math.min(3.2, height * 0.34));
      addBeam(group, { x, y, z }, { x, y: top - 0.45, z }, 0.13, materials.concrete);
    }
  } else {
    const normal = { x: -direction.z, z: direction.x };
    for (const sign of [-1, 1]) {
      const x = point.x + normal.x * sign * sideOffset;
      const z = point.z + normal.z * sign * sideOffset;
      addCylinder(group, 0.37, 0.58, columnHeight, { x, y: centerY, z }, columnMaterial, 14);
    }
    addBox(group, [1.0, 0.44, capDepth], { x: point.x, y: capY, z: point.z }, materials.concreteDark, yaw);
  }
}

function bridgeRail(group, samples, lateralOffset, material) {
  const railPoints = [];
  for (let i = 0; i < samples.length; i += 1) {
    const sample = samples[i];
    const previous = samples[Math.max(0, i - 1)];
    const next = samples[Math.min(samples.length - 1, i + 1)];
    const direction = unit(next.x - previous.x, next.z - previous.z);
    const normal = { x: -direction.z, z: direction.x };
    railPoints.push({ x: sample.x + normal.x * lateralOffset, y: sample.y + 1.2, z: sample.z + normal.z * lateralOffset });
  }
  lineTube(group, railPoints, 0.085, material, false, Math.max(36, samples.length));
  const total = samples.at(-1)?.distance ?? 0;
  for (let d = 0.8; d < total - 0.5; d += 3.6) {
    const sample = pointAtDistance(samples, d);
    const before = pointAtDistance(samples, Math.max(0, d - 0.2));
    const after = pointAtDistance(samples, Math.min(total, d + 0.2));
    const tangent = unit(after.x - before.x, after.z - before.z);
    const normal = { x: -tangent.z, z: tangent.x };
    addBox(group, [0.12, 1.12, 0.12], {
      x: sample.x + normal.x * lateralOffset,
      y: sample.y + 0.62,
      z: sample.z + normal.z * lateralOffset,
    }, material, -Math.atan2(tangent.z, tangent.x));
  }
}

function buildBridgeStructure(structureGroup, edge, network, materials, profileMaterials) {
  const length = edgeLength(edge, network.nodes, 64);
  const samples = sampleEdge(edge, network.nodes, Math.max(72, Math.ceil(length * 2.2)));
  const roadW = roadWidth(edge);
  const walk = edge.pavementEnabled ? Number(edge.pavementWidth ?? 0) : 0;
  const curbW = Number(edge.curbWidth ?? 0.22);
  const deckWidth = roadW + 2 * (walk + curbW + 0.28);
  const bridge = edge.bridge;
  const type = bridge.deckType ?? 'box-girder';
  const deckThickness = type === 'box-girder' ? 0.92 : 0.52;

  const deckGeo = bridgeDeckGeometry(samples, deckWidth * 0.5, deckThickness);
  meshWithGeometry(structureGroup, deckGeo, materials.bridge, `${edge.name} deck soffit`);

  if (type === 'box-girder' || type === 't-girder') {
    const count = type === 'box-girder' ? 2 : 3;
    const beamWidth = type === 'box-girder' ? 0.72 : 0.56;
    const beamDepth = type === 'box-girder' ? 1.05 : 1.35;
    for (let i = 0; i < count; i += 1) {
      const lateral = count === 1 ? 0 : -deckWidth * 0.31 + (deckWidth * 0.62 * i) / (count - 1);
      const geometry = prismAlongPath(samples, lateral, beamWidth, -deckThickness + 0.05, -deckThickness - beamDepth);
      meshWithGeometry(structureGroup, geometry, materials.bridgeDark, `${edge.name} longitudinal girder`);
    }
  } else if (type === 'truss') {
    const total = samples.at(-1).distance;
    for (const sign of [-1, 1]) {
      const lateral = sign * deckWidth * 0.39;
      const step = 4.0;
      let previous = null;
      let previousBottom = null;
      let index = 0;
      for (let d = 0; d <= total + 0.01; d += step) {
        const p = pointAtDistance(samples, Math.min(d, total));
        const tangent = tangentOnEdge(edge, network.nodes, p.t);
        const normal = { x: -tangent.z, z: tangent.x };
        const x = p.x + normal.x * lateral;
        const z = p.z + normal.z * lateral;
        const top = { x, y: p.y - 0.35, z };
        const bottom = { x, y: p.y - 1.85, z };
        if (previous) {
          addBeam(structureGroup, previous.top, top, 0.09, materials.bridgeLight);
          addBeam(structureGroup, previous.bottom, bottom, 0.1, materials.bridgeDark);
          addBeam(structureGroup, previous.top, index % 2 ? bottom : previousBottom, 0.075, materials.bridgeLight);
          addBeam(structureGroup, previousBottom, bottom, 0.065, materials.bridgeLight);
          addBeam(structureGroup, top, bottom, 0.065, materials.bridgeLight);
        }
        previous = { top, bottom };
        previousBottom = bottom;
        index += 1;
      }
    }
  } else if (type === 'arch-rib') {
    const total = samples.at(-1).distance;
    for (const sign of [-1, 1]) {
      const lateral = sign * deckWidth * 0.34;
      const arch = [];
      for (let i = 0; i <= 48; i += 1) {
        const d = (total * i) / 48;
        const p = pointAtDistance(samples, d);
        const tangent = tangentOnEdge(edge, network.nodes, p.t);
        const normal = { x: -tangent.z, z: tangent.x };
        const sag = Math.sin(Math.PI * i / 48) * Math.min(3.6, Number(bridge.height ?? 7) * 0.48);
        arch.push({ x: p.x + normal.x * lateral, y: p.y - 0.4 - sag, z: p.z + normal.z * lateral });
      }
      lineTube(structureGroup, arch, 0.28, materials.bridgeDark, false, 72);
      for (let i = 3; i < 48; i += 4) {
        const p = pointAtDistance(samples, (total * i) / 48);
        const tangent = tangentOnEdge(edge, network.nodes, p.t);
        const normal = { x: -tangent.z, z: tangent.x };
        const archPoint = arch[i];
        addBeam(structureGroup, archPoint, { x: p.x + normal.x * lateral, y: p.y - 0.3, z: p.z + normal.z * lateral }, 0.075, materials.bridgeLight);
      }
    }
  }

  // Supports are positioned from measured spline distance rather than point index,
  // so changing tessellation never shifts the pier rhythm.
  const spacing = Math.max(6, Number(bridge.supportSpacing ?? 12));
  const rampLength = length * Number(bridge.rampFraction ?? 0.2);
  const supportStart = Math.max(rampLength + 2, spacing * 0.55);
  const supportEnd = length - supportStart;
  structureGroup.userData.nodes = network.nodes;
  const supportDepth = type === 'box-girder' ? 1.05 : type === 't-girder' ? 1.35 : type === 'truss' || type === 'arch-rib' ? 1.85 : 0;
  for (let distance = supportStart; distance < supportEnd; distance += spacing) {
    const point = pointAtDistance(samples, distance);
    if (point.y < 2.0) continue;
    bridgeSupport(structureGroup, edge, samples, distance, deckWidth, deckThickness, supportDepth, profileMaterials);
  }

  if (bridge.parapet !== false) {
    const railOffset = deckWidth * 0.5 - 0.16;
    bridgeRail(structureGroup, samples, railOffset, materials.bridgeLight);
    bridgeRail(structureGroup, samples, -railOffset, materials.bridgeLight);
  }
  return { samples, roadW, deckWidth, deckThickness };
}

function bridgeRoadGeometry(edge, network, layers, materials) {
  const length = edgeLength(edge, network.nodes, 64);
  const samples = sampleEdge(edge, network.nodes, Math.max(72, Math.ceil(length * 2.1)));
  const halfRoad = roadWidth(edge) * 0.5;
  const curbW = Number(edge.curbWidth ?? 0.22);
  const walk = edge.pavementEnabled ? Number(edge.pavementWidth ?? 0) : 0;
  const deckWidth = roadWidth(edge) + 2 * (curbW + walk + 0.28);
  const bridge = edge.bridge;

  const roadMesh = meshWithGeometry(layers.bridgeRoad, ribbonGeometry(samples, -halfRoad, halfRoad, 0.2), materials.asphalt, `${edge.name} asphalt`);
  roadMesh.userData.bridgeSurface = true;
  if (walk > 0 && edge.pavementEnabled) {
    meshWithGeometry(layers.pavement, ribbonGeometry(samples, halfRoad + curbW, halfRoad + curbW + walk, 0.195), materials.pavement, `${edge.name} pavement · left`, false);
    meshWithGeometry(layers.pavement, ribbonGeometry(samples, -halfRoad - curbW - walk, -halfRoad - curbW, 0.195), materials.pavement, `${edge.name} pavement · right`, false);
  }
  if (edge.curbHeight > 0 && edge.curbWidth > 0) {
    makeLinearCurb(layers.curbs, samples, halfRoad + curbW * 0.5, curbW, Number(edge.curbHeight), materials.curb);
    makeLinearCurb(layers.curbs, samples, -halfRoad - curbW * 0.5, curbW, Number(edge.curbHeight), materials.curb);
  }
  return samples;
}

function addNodeMarker(group, node, selected, id) {
  const radius = selected ? 0.56 : 0.36;
  const geometry = new THREE.SphereGeometry(radius, 18, 12);
  const material = selected ? new THREE.MeshStandardMaterial({ color: PALETTE.pointSelected, roughness: 0.42, metalness: 0.06 })
    : new THREE.MeshStandardMaterial({ color: PALETTE.point, roughness: 0.48, metalness: 0.02 });
  material.userData.ownedMaterial = true;
  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.set(node.x, 0.43, node.z);
  mesh.castShadow = true;
  mesh.userData.roadworks = { type: 'node', id };
  group.add(mesh);
  return mesh;
}

function addHandleMarker(group, edgeId, kind, point) {
  const geometry = new THREE.SphereGeometry(0.29, 16, 10);
  const material = new THREE.MeshStandardMaterial({ color: PALETTE.handle, roughness: 0.38, metalness: 0.03 });
  material.userData.ownedMaterial = true;
  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.set(point.x, 0.47, point.z);
  mesh.castShadow = true;
  mesh.userData.roadworks = { type: 'handle', edgeId, handle: kind };
  group.add(mesh);
}

function lineGeometry(points) {
  const geometry = new THREE.BufferGeometry();
  geometry.setFromPoints(points.map((point) => new THREE.Vector3(point.x, point.y, point.z)));
  return geometry;
}

export class RoadScene {
  constructor(container) {
    this.container = container;
    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.65));
    this.renderer.setClearColor(0x242831, 1);
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.06;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.domElement.className = 'scene-canvas';
    this.container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x242831);
    this.camera = new THREE.PerspectiveCamera(42, 1, 0.1, 700);
    this.camera.position.set(42, 47, 50);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.target.set(0, 1, 0);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.075;
    this.controls.minDistance = 22;
    this.controls.maxDistance = 160;
    this.controls.maxPolarAngle = Math.PI * 0.49;
    this.controls.screenSpacePanning = true;

    this.raycaster = new THREE.Raycaster();
    this.raycaster.params.Line.threshold = 0.4;
    this.groundPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
    this.groundPoint = new THREE.Vector3();
    this.network = null;
    this.selection = null;
    this.drawPreview = null;
    this.pickObjects = [];
    this.isPlanView = false;
    this.dragWasActive = false;

    this.materials = {
      ground: mat(0x252a30, { roughness: 1, metalness: 0 }),
      asphalt: mat(PALETTE.asphalt, { roughness: 0.98, metalness: 0 }),
      pavement: mat(PALETTE.pavement, { roughness: 0.93, metalness: 0 }),
      curb: mat(PALETTE.curb, { roughness: 0.8, metalness: 0 }),
      whitePaint: mat(PALETTE.whitePaint, { roughness: 0.68, metalness: 0 }),
      yellowPaint: mat(PALETTE.yellowPaint, { roughness: 0.72, metalness: 0 }),
      bridge: mat(PALETTE.bridge, { roughness: 0.78, metalness: 0.1 }),
      bridgeDark: mat(PALETTE.bridgeDark, { roughness: 0.82, metalness: 0.1 }),
      bridgeLight: mat(PALETTE.bridgeLight, { roughness: 0.74, metalness: 0.08 }),
      concrete: mat(PALETTE.concrete, { roughness: 0.9, metalness: 0 }),
      concreteDark: mat(PALETTE.concreteDark, { roughness: 0.9, metalness: 0 }),
      node: mat(PALETTE.point, { roughness: 0.45, metalness: 0.02 }),
      helperLine: new THREE.LineBasicMaterial({ color: 0xe4b67c, transparent: true, opacity: 0.92 }),
      tangentLine: new THREE.LineBasicMaterial({ color: 0xd3a96e, transparent: true, opacity: 0.72, dashSize: 0.6, gapSize: 0.34 }),
      previewLine: new THREE.LineDashedMaterial({ color: 0xe4b67c, dashSize: 0.8, gapSize: 0.44, transparent: true, opacity: 0.95 }),
      snap: mat(0xd7ad71, { roughness: 0.38, emissive: 0x241707, emissiveIntensity: 0.35 }),
    };

    this.world = new THREE.Group();
    this.scene.add(this.world);
    this.layers = {
      groundRoad: new THREE.Group(),
      bridgeRoad: new THREE.Group(),
      pavement: new THREE.Group(),
      curbs: new THREE.Group(),
      markings: new THREE.Group(),
      bridgeStructure: new THREE.Group(),
      helpers: new THREE.Group(),
      preview: new THREE.Group(),
    };
    for (const group of Object.values(this.layers)) this.world.add(group);

    this.buildEnvironment();
    this.addLights();
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(this.container);
    this.resize();
    this.animate = this.animate.bind(this);
    this.frameId = requestAnimationFrame(this.animate);
  }

  buildEnvironment() {
    const ground = new THREE.Mesh(new THREE.PlaneGeometry(180, 180), this.materials.ground);
    ground.rotation.x = -Math.PI / 2;
    ground.position.y = -0.24;
    ground.receiveShadow = true;
    this.scene.add(ground);
    this.replaceGrid(1);
    this.axis = new THREE.AxesHelper(3.2);
    this.axis.position.set(-38, -0.18, 34);
    this.scene.add(this.axis);
  }

  addLights() {
    const hemi = new THREE.HemisphereLight(0xe2e5eb, 0x3a3733, 2.15);
    this.scene.add(hemi);
    const key = new THREE.DirectionalLight(0xffedcf, 3.2);
    key.position.set(-28, 55, 36);
    key.castShadow = true;
    key.shadow.mapSize.set(1536, 1536);
    key.shadow.camera.left = -58;
    key.shadow.camera.right = 58;
    key.shadow.camera.top = 58;
    key.shadow.camera.bottom = -58;
    key.shadow.bias = -0.00018;
    key.shadow.normalBias = 0.018;
    this.scene.add(key);
    const rim = new THREE.DirectionalLight(0x9eb8d4, 1.25);
    rim.position.set(26, 25, -34);
    this.scene.add(rim);
  }

  resize() {
    const width = Math.max(1, this.container.clientWidth);
    const height = Math.max(1, this.container.clientHeight);
    this.renderer.setSize(width, height, false);
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
  }

  animate() {
    this.frameId = requestAnimationFrame(this.animate);
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }

  setNetwork(network, selection = this.selection) {
    this.network = network;
    this.selection = selection;
    this.rebuild();
  }

  setSelection(selection) {
    this.selection = selection;
    this.rebuildHelpers();
  }

  setVisibility(settings = {}) {
    this.layers.groundRoad.visible = settings.showRoadSurface !== false;
    this.layers.bridgeRoad.visible = settings.showRoadSurface !== false;
    this.layers.pavement.visible = settings.showPavement !== false;
    this.layers.curbs.visible = settings.showCurbs !== false;
    this.layers.markings.visible = settings.showMarkings !== false;
    this.layers.bridgeStructure.visible = settings.showBridgeStructure !== false;
    this.layers.helpers.visible = settings.showPoints !== false || this.selection?.type === 'node' || this.selection?.type === 'edge';
    const requestedGridSize = Math.max(0.25, Number(settings.gridSize ?? 1));
    if (Math.abs(requestedGridSize - (this.gridSize ?? 1)) > 0.001) this.replaceGrid(requestedGridSize);
    this.grid.visible = settings.grid !== false;
  }

  replaceGrid(gridSize) {
    if (this.grid) {
      this.scene.remove(this.grid);
      this.grid.geometry.dispose();
      const materials = Array.isArray(this.grid.material) ? this.grid.material : [this.grid.material];
      materials.forEach((material) => material.dispose());
    }
    const divisions = Math.max(1, Math.min(720, Math.round(180 / gridSize)));
    this.grid = new THREE.GridHelper(180, divisions, 0x656b72, 0x3c4148);
    this.grid.position.y = -0.205;
    this.grid.material.transparent = true;
    this.grid.material.opacity = 0.34;
    this.grid.material.depthWrite = false;
    this.gridSize = gridSize;
    this.scene.add(this.grid);
  }

  rebuild() {
    if (!this.network) return;
    for (const key of ['groundRoad', 'bridgeRoad', 'pavement', 'curbs', 'markings', 'bridgeStructure']) disposeGroup(this.layers[key]);

    const groundEdges = this.network.edges.filter((edge) => !edge.bridge?.enabled);
    const bridgeEdges = this.network.edges.filter((edge) => edge.bridge?.enabled);
    const surfaces = surfacePolygons(this.network, groundEdges);
    addArea(this.layers.pavement, surfaces.pavementOnly, 0.165, this.materials.pavement, 'Connected pavement mesh');
    addArea(this.layers.groundRoad, surfaces.roadUnion, 0.135, this.materials.asphalt, 'Merged road surface');
    makeBoundaryCurbs(this.layers.curbs, surfaces.roadUnion, groundEdges, this.network, this.materials.curb);
    for (const edge of groundEdges) {
      const length = edgeLength(edge, this.network.nodes, 40);
      const samples = sampleEdge(edge, this.network.nodes, Math.max(48, Math.ceil(length * 1.65)));
      addStripeGeometry(this.layers.markings, this.network, edge, samples, { yellow: this.materials.yellowPaint, white: this.materials.whitePaint });
    }

    for (const edge of bridgeEdges) {
      const structure = buildBridgeStructure(this.layers.bridgeStructure, edge, this.network, this.materials, {
        concrete: this.materials.concrete,
        concreteDark: this.materials.concreteDark,
      });
      const samples = bridgeRoadGeometry(edge, this.network, this.layers, this.materials);
      // Add bridge road paint in white too; the center line uses the muted amber asphalt convention.
      addStripeGeometry(this.layers.markings, this.network, edge, samples, { yellow: this.materials.yellowPaint, white: this.materials.whitePaint });
      if (!structure) continue;
    }

    this.rebuildHelpers();
    this.setVisibility(this.network.settings);
  }

  rebuildHelpers() {
    if (!this.network) return;
    disposeGroup(this.layers.helpers);
    this.pickObjects = [];
    const showPoints = this.network.settings?.showPoints !== false;
    if (showPoints) {
      for (const [id, node] of Object.entries(this.network.nodes)) {
        const marker = addNodeMarker(this.layers.helpers, node, this.selection?.type === 'node' && this.selection.id === id, id);
        this.pickObjects.push(marker);
      }
    }
    if (this.selection?.type === 'edge') {
      const edge = this.network.edges.find((item) => item.id === this.selection.id);
      if (edge) {
        const samples = sampleEdge(edge, this.network.nodes, 56).map((point) => ({ x: point.x, y: point.y + 0.42, z: point.z }));
        const highlight = new THREE.Line(lineGeometry(samples), this.materials.helperLine);
        this.layers.helpers.add(highlight);
        const p0 = this.network.nodes[edge.from];
        const p3 = this.network.nodes[edge.to];
        if (p0 && p3) {
          const c1 = edge.c1 ?? p0;
          const c2 = edge.c2 ?? p3;
          const tangentPoints = [
            { x: p0.x, y: 0.48, z: p0.z },
            { x: c1.x, y: 0.48, z: c1.z },
            { x: p3.x, y: 0.48, z: p3.z },
            { x: c2.x, y: 0.48, z: c2.z },
          ];
          const handles = new THREE.LineSegments(lineGeometry([tangentPoints[0], tangentPoints[1], tangentPoints[2], tangentPoints[3]]), this.materials.tangentLine);
          this.layers.helpers.add(handles);
          addHandleMarker(this.layers.helpers, edge.id, 'c1', c1);
          addHandleMarker(this.layers.helpers, edge.id, 'c2', c2);
          this.pickObjects.push(...this.layers.helpers.children.filter((child) => child.userData.roadworks?.type === 'handle'));
        }
      }
    }
    this.setVisibility(this.network.settings);
  }

  setDrawPreview(preview) {
    this.drawPreview = preview;
    disposeGroup(this.layers.preview);
    if (preview?.points?.length) this.drawPreviewGeometry();
  }

  drawPreviewGeometry() {
    const preview = this.drawPreview;
    if (!preview?.points?.length) return;
    const points = [...preview.points];
    if (preview.cursor) points.push(preview.cursor);
    if (points.length > 1) {
      const linePoints = points.map((point) => ({ x: point.x, y: 0.39, z: point.z }));
      const line = new THREE.Line(lineGeometry(linePoints), this.materials.previewLine);
      line.computeLineDistances();
      this.layers.preview.add(line);
    }
    if (preview.snapPoint) {
      const geometry = new THREE.SphereGeometry(0.5, 18, 12);
      const marker = new THREE.Mesh(geometry, this.materials.snap);
      marker.position.set(preview.snapPoint.x, 0.42, preview.snapPoint.z);
      this.layers.preview.add(marker);
    }
  }

  pick(clientX, clientY) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const ndc = new THREE.Vector2(
      ((clientX - rect.left) / rect.width) * 2 - 1,
      -((clientY - rect.top) / rect.height) * 2 + 1,
    );
    this.raycaster.setFromCamera(ndc, this.camera);
    if (!this.pickObjects.length) return null;
    const hits = this.raycaster.intersectObjects(this.pickObjects, false);
    return hits[0]?.object.userData.roadworks ?? null;
  }

  groundAt(clientX, clientY) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const ndc = new THREE.Vector2(
      ((clientX - rect.left) / rect.width) * 2 - 1,
      -((clientY - rect.top) / rect.height) * 2 + 1,
    );
    this.raycaster.setFromCamera(ndc, this.camera);
    if (this.raycaster.ray.intersectPlane(this.groundPlane, this.groundPoint)) {
      return { x: this.groundPoint.x, z: this.groundPoint.z };
    }
    return null;
  }

  roadHit(clientX, clientY) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    const ndc = new THREE.Vector2(
      ((clientX - rect.left) / rect.width) * 2 - 1,
      -((clientY - rect.top) / rect.height) * 2 + 1,
    );
    this.raycaster.setFromCamera(ndc, this.camera);
    const targets = [...this.layers.groundRoad.children, ...this.layers.bridgeRoad.children];
    const hit = this.raycaster.intersectObjects(targets, true)[0];
    if (!hit) return null;
    return { x: hit.point.x, y: hit.point.y, z: hit.point.z, bridge: hit.object.userData.bridgeSurface === true };
  }

  fit(network = this.network) {
    if (!network) return;
    const nodes = Object.values(network.nodes);
    if (!nodes.length) return;
    const minX = Math.min(...nodes.map((node) => node.x));
    const maxX = Math.max(...nodes.map((node) => node.x));
    const minZ = Math.min(...nodes.map((node) => node.z));
    const maxZ = Math.max(...nodes.map((node) => node.z));
    const center = new THREE.Vector3((minX + maxX) * 0.5, 1.2, (minZ + maxZ) * 0.5);
    const span = Math.max(maxX - minX, maxZ - minZ, 24);
    const distance = Math.max(38, span * 1.48);
    const direction = new THREE.Vector3(0.65, 0.82, 0.72).normalize();
    this.controls.target.copy(center);
    this.camera.position.copy(center).addScaledVector(direction, distance);
    this.camera.near = Math.max(0.1, distance / 300);
    this.camera.far = Math.max(400, distance * 16);
    this.camera.updateProjectionMatrix();
    this.isPlanView = false;
    this.controls.update();
  }

  setPlanView(enabled) {
    this.isPlanView = enabled;
    const target = this.controls.target.clone();
    if (enabled) {
      const distance = Math.max(54, this.camera.position.distanceTo(target));
      this.camera.up.set(0, 0, -1);
      this.camera.position.set(target.x, target.y + distance * 1.1, target.z);
    } else {
      this.camera.up.set(0, 1, 0);
      this.camera.position.copy(target).add(new THREE.Vector3(0.65, 0.82, 0.72).normalize().multiplyScalar(58));
    }
    this.camera.lookAt(target);
    this.controls.update();
  }

  dispose() {
    cancelAnimationFrame(this.frameId);
    this.resizeObserver?.disconnect();
    this.controls.dispose();
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }
}

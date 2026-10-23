import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import {
    BARK_PROFILES,
    BarkProfile,
    BarkProfileId,
    barkColour,
    barkHeight,
    hash01,
    makeClusterSpecs,
} from './procedural';
import './styles.css';

interface BarkMaps
{
    albedo: HTMLCanvasElement;
    normal: HTMLCanvasElement;
    roughness: HTMLCanvasElement;
}

const viewport = document.querySelector<HTMLCanvasElement>('#barkViewport')!;
const albedoPreview = document.querySelector<HTMLCanvasElement>('#albedoPreview')!;
const normalPreview = document.querySelector<HTMLCanvasElement>('#normalPreview')!;
const roughnessPreview = document.querySelector<HTMLCanvasElement>('#roughnessPreview')!;
const profileSelect = document.querySelector<HTMLSelectElement>('#profileSelect')!;
const seedInput = document.querySelector<HTMLInputElement>('#seedInput')!;
const seedValue = document.querySelector<HTMLElement>('#seedValue')!;
const heightInput = document.querySelector<HTMLInputElement>('#heightInput')!;
const radiusInput = document.querySelector<HTMLInputElement>('#radiusInput')!;
const heightValue = document.querySelector<HTMLElement>('#heightValue')!;
const radiusValue = document.querySelector<HTMLElement>('#radiusValue')!;
const clusterValue = document.querySelector<HTMLElement>('#clusterValue')!;
const description = document.querySelector<HTMLElement>('#profileDescription')!;
const status = document.querySelector<HTMLElement>('#status')!;
const regenerateButton = document.querySelector<HTMLButtonElement>('#regenerateButton')!;
const randomiseButton = document.querySelector<HTMLButtonElement>('#randomiseButton')!;
const bakeButton = document.querySelector<HTMLButtonElement>('#bakeButton')!;

for (const profile of BARK_PROFILES)
{
    const option = document.createElement('option');
    option.value = profile.id;
    option.textContent = profile.name;
    profileSelect.append(option);
}
profileSelect.value = 'cork';

const renderer = new THREE.WebGLRenderer({ canvas: viewport, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.12;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x120f0c);
scene.fog = new THREE.Fog(0x120f0c, 7, 18);
const camera = new THREE.PerspectiveCamera(36, 1, 0.03, 40);
camera.position.set(3.15, 2.35, 4.25);
const controls = new OrbitControls(camera, viewport);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.minDistance = 1.45;
controls.maxDistance = 9;
controls.target.set(0, 1.12, 0);

const key = new THREE.DirectionalLight(0xffe5bd, 3.8);
key.position.set(3.5, 5.5, 3.5);
key.castShadow = true;
key.shadow.mapSize.set(2048, 2048);
key.shadow.bias = -0.0003;
key.shadow.normalBias = 0.025;
scene.add(key);
scene.add(key.target);
scene.add(new THREE.HemisphereLight(0xc6d5e8, 0x2a1810, 0.7));

const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(18, 18),
    new THREE.MeshStandardMaterial({ color: 0x211a15, roughness: 1, metalness: 0 }),
);
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);

const trunkGroup = new THREE.Group();
scene.add(trunkGroup);
let barkMaterial: THREE.MeshStandardMaterial | null = null;
let coreMaterial: THREE.MeshStandardMaterial | null = null;
let maps: BarkMaps | null = null;
let mapTextures: THREE.CanvasTexture[] = [];
let currentProfile = BARK_PROFILES[0];
let currentSeed = 17;

function renderMaps(profile: BarkProfile, seed: number, size = 512): BarkMaps
{
    const albedo = document.createElement('canvas');
    const normal = document.createElement('canvas');
    const roughness = document.createElement('canvas');
    for (const canvas of [albedo, normal, roughness])
    {
        canvas.width = size;
        canvas.height = size;
    }
    const albedoPixels = albedo.getContext('2d')!.createImageData(size, size);
    const normalPixels = normal.getContext('2d')!.createImageData(size, size);
    const roughnessPixels = roughness.getContext('2d')!.createImageData(size, size);
    const epsilon = 1 / size;

    for (let y = 0; y < size; y++)
    {
        const v = 1 - y / (size - 1);
        for (let x = 0; x < size; x++)
        {
            const u = x / (size - 1);
            const h = barkHeight(profile, u, v, seed);
            const colour = barkColour(profile, h, u, v, seed);
            const index = (y * size + x) * 4;
            albedoPixels.data[index] = Math.round(Math.pow(colour[0], 1 / 2.2) * 255);
            albedoPixels.data[index + 1] = Math.round(Math.pow(colour[1], 1 / 2.2) * 255);
            albedoPixels.data[index + 2] = Math.round(Math.pow(colour[2], 1 / 2.2) * 255);
            albedoPixels.data[index + 3] = 255;

            const left = barkHeight(profile, u - epsilon, v, seed);
            const right = barkHeight(profile, u + epsilon, v, seed);
            const down = barkHeight(profile, u, v - epsilon, seed);
            const up = barkHeight(profile, u, v + epsilon, seed);
            const dx = (right - left) * 14;
            const dy = (up - down) * 14;
            normalPixels.data[index] = Math.round(THREE.MathUtils.clamp(128 - dx * 96, 0, 255));
            normalPixels.data[index + 1] = Math.round(THREE.MathUtils.clamp(128 - dy * 96, 0, 255));
            normalPixels.data[index + 2] = 255;
            normalPixels.data[index + 3] = 255;

            const rough = 188 - h * 48 + hash01(x, y, seed + 600) * 20;
            roughnessPixels.data[index] = rough;
            roughnessPixels.data[index + 1] = rough;
            roughnessPixels.data[index + 2] = rough;
            roughnessPixels.data[index + 3] = 255;
        }
    }
    albedo.getContext('2d')!.putImageData(albedoPixels, 0, 0);
    normal.getContext('2d')!.putImageData(normalPixels, 0, 0);
    roughness.getContext('2d')!.putImageData(roughnessPixels, 0, 0);
    return { albedo, normal, roughness };
}

function textureFrom(canvas: HTMLCanvasElement, srgb: boolean): THREE.CanvasTexture
{
    const texture = new THREE.CanvasTexture(canvas);
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;
    texture.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());
    texture.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    texture.needsUpdate = true;
    return texture;
}

function makeClusterGeometry(spec: ReturnType<typeof makeClusterSpecs>[number], profile: BarkProfile, radius: number, trunkHeight: number): THREE.BufferGeometry
{
    const columns = profile.columns;
    const rows = profile.rings;
    const positions: number[] = [];
    const uvs: number[] = [];
    const colours: number[] = [];
    const indices: number[] = [];
    const front: number[][] = [];
    const back: number[][] = [];
    const centreY = spec.y + spec.height * 0.5;
    const startY = THREE.MathUtils.clamp(spec.y, 0.025, Math.max(0.03, trunkHeight - 0.04));
    const endY = THREE.MathUtils.clamp(Math.max(startY + 0.02, centreY), startY + 0.02, trunkHeight - 0.012);

    const addVertex = (x: number, y: number, z: number, u: number, v: number, backFace: boolean): number =>
    {
        const index = positions.length / 3;
        positions.push(x, y, z);
        uvs.push(u, v);
        const shade = 0.86 + hash01(index, spec.row, spec.seed + (backFace ? 4 : 0)) * 0.24;
        colours.push(shade, shade, shade);
        return index;
    };

    for (let row = 0; row <= rows; row++)
    {
        const t = row / rows;
        const y = THREE.MathUtils.lerp(startY, endY, t);
        const edge = Math.pow(Math.max(0.08, Math.sin(Math.PI * t)), profile.clusterShape === 'paper' ? 0.22 : 0.42);
        const rowFront: number[] = [];
        const rowBack: number[] = [];
        for (let column = 0; column <= columns; column++)
        {
            const q = column / columns - 0.5;
            const localWidth = spec.width * (0.62 + edge * 0.38);
            const angle = spec.angle + q * localWidth / radius + spec.lean * (t - 0.5);
            const reliefWave = 0.5 + 0.5 * Math.sin((t * profile.rings + q * profile.columns) * Math.PI);
            const relief = spec.relief * (0.48 + reliefWave * 0.52) * (0.8 + hash01(row, column, spec.seed) * 0.35);
            const frontRadius = radius + 0.016 + relief;
            const backRadius = radius - spec.thickness * 0.45;
            rowFront.push(addVertex(Math.cos(angle) * frontRadius, y, Math.sin(angle) * frontRadius, column / columns, t, false));
            rowBack.push(addVertex(Math.cos(angle) * backRadius, y, Math.sin(angle) * backRadius, column / columns, t, true));
        }
        front.push(rowFront);
        back.push(rowBack);
    }

    const quad = (a: number, b: number, c: number, d: number): void =>
    {
        indices.push(a, b, c, a, c, d);
    };
    for (let row = 0; row < rows; row++)
    {
        for (let column = 0; column < columns; column++)
        {
            const a = front[row][column];
            const b = front[row][column + 1];
            const c = front[row + 1][column + 1];
            const d = front[row + 1][column];
            quad(a, d, c, b);
            quad(back[row][column], back[row + 1][column], back[row + 1][column + 1], back[row][column + 1]);
        }
    }
    for (let column = 0; column < columns; column++)
    {
        quad(front[0][column], front[0][column + 1], back[0][column + 1], back[0][column]);
        quad(front[rows][column + 1], front[rows][column], back[rows][column], back[rows][column + 1]);
    }
    for (let row = 0; row < rows; row++)
    {
        quad(front[row + 1][0], front[row][0], back[row][0], back[row + 1][0]);
        quad(front[row][columns], front[row + 1][columns], back[row + 1][columns], back[row][columns]);
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
    geometry.setAttribute('color', new THREE.Float32BufferAttribute(colours, 3));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();
    geometry.computeBoundingSphere();
    return geometry;
}

function clearTrunk(): void
{
    while (trunkGroup.children.length > 0)
    {
        const child = trunkGroup.children.pop()!;
        if (child instanceof THREE.Mesh)
        {
            child.geometry.dispose();
            if (child.material instanceof THREE.Material) child.material.dispose();
        }
    }
    barkMaterial?.dispose();
    coreMaterial?.dispose();
    for (const texture of mapTextures) texture.dispose();
    mapTextures = [];
}

function build(): void
{
    currentProfile = BARK_PROFILES.find((profile) => profile.id === profileSelect.value as BarkProfileId) ?? BARK_PROFILES[0];
    currentSeed = Number.parseInt(seedInput.value, 10) || 1;
    const trunkHeight = Number(heightInput.value);
    const radius = Number(radiusInput.value);
    clearTrunk();
    const generatedMaps = renderMaps(currentProfile, currentSeed);
    maps = generatedMaps;
    const albedoTexture = textureFrom(generatedMaps.albedo, true);
    const normalTexture = textureFrom(generatedMaps.normal, false);
    const roughnessTexture = textureFrom(generatedMaps.roughness, false);
    mapTextures = [albedoTexture, normalTexture, roughnessTexture];

    barkMaterial = new THREE.MeshStandardMaterial({
        map: albedoTexture,
        normalMap: normalTexture,
        roughnessMap: roughnessTexture,
        roughness: 0.9,
        metalness: 0,
        vertexColors: true,
        side: THREE.DoubleSide,
        normalScale: new THREE.Vector2(0.72, 0.72),
    });
    coreMaterial = barkMaterial.clone();
    coreMaterial.vertexColors = false;
    coreMaterial.color.setRGB(0.58, 0.45, 0.32);
    coreMaterial.roughness = 0.96;

    const core = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.985, radius * 1.08, trunkHeight, 40, 12, false), coreMaterial);
    core.position.y = trunkHeight * 0.5;
    core.castShadow = true;
    core.receiveShadow = true;
    trunkGroup.add(core);

    const specs = makeClusterSpecs(currentProfile, currentSeed, trunkHeight);
    for (const spec of specs)
    {
        const geometry = makeClusterGeometry(spec, currentProfile, radius, trunkHeight);
        const cluster = new THREE.Mesh(geometry, barkMaterial);
        cluster.castShadow = true;
        cluster.receiveShadow = true;
        trunkGroup.add(cluster);
    }
    trunkGroup.position.y = 0;
    controls.target.set(0, trunkHeight * 0.48, 0);
    seedValue.textContent = String(currentSeed);
    heightValue.textContent = `${trunkHeight.toFixed(2)} m`;
    radiusValue.textContent = `${radius.toFixed(2)} m`;
    clusterValue.textContent = `${specs.length} independent pieces`;
    description.textContent = currentProfile.description;
    drawPreview(albedoPreview, generatedMaps.albedo);
    drawPreview(normalPreview, generatedMaps.normal);
    drawPreview(roughnessPreview, generatedMaps.roughness);
    status.textContent = `Built ${specs.length} separate ${currentProfile.clusterShape} clusters · seed ${currentSeed}`;
}

function drawPreview(target: HTMLCanvasElement, source: HTMLCanvasElement): void
{
    const context = target.getContext('2d')!;
    context.clearRect(0, 0, target.width, target.height);
    context.drawImage(source, 0, 0, target.width, target.height);
}

function saveCanvas(canvas: HTMLCanvasElement, filename: string): void
{
    canvas.toBlob((blob) =>
    {
        if (!blob) return;
        const link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = filename;
        link.click();
        window.setTimeout(() => URL.revokeObjectURL(link.href), 1000);
    }, 'image/png');
}

function bake(): void
{
    const bakedMaps = maps;
    if (!bakedMaps) return;
    const stem = `bark-${currentProfile.id}-${currentSeed}`;
    saveCanvas(bakedMaps.albedo, `${stem}-albedo.png`);
    window.setTimeout(() => saveCanvas(bakedMaps.normal, `${stem}-normal.png`), 120);
    window.setTimeout(() => saveCanvas(bakedMaps.roughness, `${stem}-roughness.png`), 240);
    status.textContent = `Baked albedo, normal and roughness maps · ${stem}`;
}

function resize(): void
{
    const width = viewport.clientWidth || 640;
    const height = viewport.clientHeight || 620;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
}

profileSelect.addEventListener('change', build);
regenerateButton.addEventListener('click', build);
heightInput.addEventListener('input', build);
radiusInput.addEventListener('input', build);
randomiseButton.addEventListener('click', () =>
{
    seedInput.value = String(1 + Math.floor(Math.random() * 99999));
    build();
});
bakeButton.addEventListener('click', bake);
window.addEventListener('resize', resize);
resize();
build();

function tick(time: number): void
{
    controls.update();
    trunkGroup.rotation.y = Math.sin(time * 0.00018) * 0.018;
    renderer.render(scene, camera);
}
renderer.setAnimationLoop(tick);

/* ============================================================
   FRONTIER · ROAD EDITOR — street-furniture prototypes
   Pure data: every prop is a list of primitive parts, each bound to a
   material key. `scene.js` merges each part into one geometry and draws
   every placement as an InstancedMesh (1 draw call per part, per prop).
   Units: metres, +Y up, prop faces along +Z (rotY aligns it to the road).
   ============================================================ */

const box = (w, h, d, at = [0, 0, 0], rot = [0, 0, 0]) => ({ g: 'box', a: [w, h, d], at, rot });
const cyl = (r1, r2, h, seg = 10, at = [0, 0, 0], rot = [0, 0, 0]) => ({ g: 'cyl', a: [r1, r2, h, seg], at, rot });
const sph = (r, at = [0, 0, 0], sx = 1, sy = 1, sz = 1) => ({ g: 'sph', a: [r, 12, 8], at, s: [sx, sy, sz] });
const cone = (r, h, at = [0, 0, 0], seg = 12) => ({ g: 'cone', a: [r, h, seg], at });
const X = Math.PI / 2;

export const PROP_SPECS = {
  /* ---- delineation ---- */
  'bollard': { label: 'Bollard · cast iron', cat: 'Delineation', parts: [
    { m: 'metal-dark', ...cyl(0.075, 0.085, 0.92, 10, [0, .46, 0]) },
    { m: 'paint-white', ...cyl(0.082, 0.082, 0.09, 10, [0, .78, 0]) },
    { m: 'metal-dark', ...sph(0.085, [0, .95, 0]) }], shadow: 1, footprint: 0.2 },
  'bollard-flexi': { label: 'Bollard · flexible delineator', cat: 'Delineation', parts: [
    { m: 'paint-white', ...cyl(0.035, 0.05, 0.95, 8, [0, .48, 0]) },
    { m: 'paint-yellow', ...cyl(0.048, 0.048, 0.16, 8, [0, .74, 0]) }], footprint: 0.14 },
  'bollard-retractable': { label: 'Bollard · retractable', cat: 'Delineation', parts: [
    { m: 'concrete-sw', ...cyl(0.13, 0.13, 0.12, 12, [0, .06, 0]) },
    { m: 'metal', ...cyl(0.09, 0.09, 0.85, 12, [0, .5, 0]) },
    { m: 'paint-yellow', ...cyl(0.092, 0.092, 0.1, 12, [0, .88, 0]) }], footprint: 0.3 },
  'bollard-pin': { label: 'Rural marker post', cat: 'Delineation', parts: [
    { m: 'paint-white', ...cyl(0.035, 0.045, 0.9, 6, [0, .45, 0]) },
    { m: 'paint-black', ...cyl(0.046, 0.046, 0.12, 6, [0, .78, 0]) }], footprint: 0.1 },
  'post-reflector': { label: 'Reflector post (delineator)', cat: 'Delineation', parts: [
    { m: 'metal', ...box(0.09, 0.75, 0.02, [0, .38, 0]) },
    { m: 'emissive-red', ...box(0.075, 0.14, 0.03, [0, .6, 0.015]) }], footprint: 0.12 },
  'chain-barrier': { label: 'Chain barrier', cat: 'Delineation', parts: [
    { m: 'metal', ...cyl(0.05, 0.055, 0.95, 8, [0, .48, 0]) },
    { m: 'metal-dark', ...sph(0.06, [0, .98, 0]) }], footprint: 0.14 },
  'cone': { label: 'Traffic cone', cat: 'Works', parts: [
    { m: 'paint-black', ...box(0.34, 0.03, 0.34, [0, .015, 0]) },
    { m: 'cones', ...cone(0.17, 0.7, [0, .38, 0]) },
    { m: 'paint-white', ...cyl(0.115, 0.13, 0.1, 12, [0, .42, 0]) }], footprint: 0.4 },
  'barrier-works': { label: 'Barrier · red/white board', cat: 'Works', parts: [
    { m: 'metal-dark', ...box(0.06, 1.0, 0.5, [0, .5, 0]) },
    { m: 'paint-red', ...box(2.0, 0.28, 0.06, [0, .85, 0]) }], footprint: 2.0 },
  'barrier-plastic': { label: 'Water-filled unit', cat: 'Works', parts: [
    { m: 'paint-orange', ...box(1.15, 0.62, 0.42, [0, .31, 0], [0, 0, 0]) },
    { m: 'paint-black', ...box(1.15, 0.1, 0.44, [0, .05, 0]) }], footprint: 1.2 },
  'gate-road': { label: 'Road gate / swing', cat: 'Works', parts: [
    { m: 'metal-dark', ...cyl(0.09, 0.1, 1.1, 8, [0, .55, 0]) },
    { m: 'paint-white', ...box(0.1, 0.1, 2.6, [0, 1.02, 1.3]) }], footprint: 0.2 },

  /* ---- lighting ---- */
  'lamp-cobra': { label: 'Luminaire · cobra arm', cat: 'Lighting', parts: [
    { m: 'concrete-sw', ...cyl(0.19, 0.23, 0.5, 10, [0, .25, 0]) },
    { m: 'metal', ...cyl(0.1, 0.145, 8.4, 10, [0, 4.7, 0]) },
    { m: 'metal', ...box(0.14, 0.16, 1.6, [0, 8.7, .75], [-.18, 0, 0]) },
    { m: 'lamp', ...box(0.42, 0.14, 0.92, [0, 8.62, 1.55]) }], shadow: 1, light: { y: 8.6, i: 1 }, footprint: 0.5 },
  'lamp-modern': { label: 'Luminaire · straight column', cat: 'Lighting', parts: [
    { m: 'concrete-sw', ...cyl(0.16, 0.19, 0.4, 8, [0, .2, 0]) },
    { m: 'metal', ...box(0.18, 7.6, 0.18, [0, 4.2, 0]) },
    { m: 'metal', ...box(0.16, 0.14, 1.15, [0, 7.95, .55], [-.1, 0, 0]) },
    { m: 'lamp', ...box(0.3, 0.1, 1.0, [0, 7.88, 1.05]) }], shadow: 1, light: { y: 7.9, i: 1 }, footprint: 0.4 },
  'lamp-heritage': { label: 'Luminaire · heritage twin', cat: 'Lighting', parts: [
    { m: 'concrete-sw', ...cyl(0.22, 0.28, 0.55, 8, [0, .28, 0]) },
    { m: 'metal-dark', ...cyl(0.11, 0.16, 6.1, 10, [0, 3.6, 0]) },
    { m: 'metal-dark', ...box(0.12, 0.12, 0.7, [0, 6.5, .3], [-.5, 0, 0]) },
    { m: 'lamp', ...cyl(0.3, 0.18, 0.3, 8, [0, 6.72, .58]) },
    { m: 'metal-dark', ...box(0.12, 0.12, 0.7, [0, 6.5, -.3], [.5, 0, 0]) },
    { m: 'lamp', ...cyl(0.3, 0.18, 0.3, 8, [0, 6.72, -.58]) },
    { m: 'metal-dark', ...sph(0.14, [0, 7.1, 0]) }], shadow: 1, light: { y: 6.7, i: 1.1 }, footprint: 0.6 },
  'lamp-twin': { label: 'Motorway mast · twin flood', cat: 'Lighting', parts: [
    { m: 'concrete-sw', ...cyl(0.3, 0.38, 0.6, 8, [0, .3, 0]) },
    { m: 'metal', ...cyl(0.14, 0.2, 13.4, 10, [0, 7.4, 0]) },
    { m: 'metal', ...box(7.0, 0.22, 0.22, [0, 14.0, 0]) },
    { m: 'lamp', ...box(0.9, 0.3, 0.55, [-2.9, 13.82, 0], [0, 0, .22]) },
    { m: 'lamp', ...box(0.9, 0.3, 0.55, [2.9, 13.82, 0], [0, 0, -.22]) }], light: { y: 13.8, i: 2.2 }, footprint: 0.8 },
  'lamp-cycle': { label: 'Footway lighting · low column', cat: 'Lighting', parts: [
    { m: 'metal-dark', ...cyl(0.06, 0.075, 3.1, 8, [0, 1.55, 0]) },
    { m: 'lamp', ...cyl(0.16, 0.1, 0.22, 8, [0, 3.18, 0]) }], light: { y: 3.2, i: .5 }, footprint: 0.2 },
  'lamp-bollard': { label: 'Light bollard', cat: 'Lighting', parts: [
    { m: 'metal-dark', ...cyl(0.08, 0.09, 0.62, 8, [0, .31, 0]) },
    { m: 'lamp', ...cyl(0.07, 0.07, 0.2, 8, [0, .5, 0]) }], light: { y: .5, i: .28 }, footprint: 0.2 },
  'lamp-globe': { label: 'Plaza globe light', cat: 'Lighting', parts: [
    { m: 'metal-dark', ...cyl(0.09, 0.13, 3.4, 8, [0, 1.7, 0]) },
    { m: 'lamp', ...sph(0.34, [0, 3.7, 0]) }], light: { y: 3.7, i: .8 }, footprint: 0.4 },
  'lamp-tunnel': { label: 'Tunnel luminaire', cat: 'Lighting', parts: [
    { m: 'lamp', ...box(1.1, 0.09, 0.34, [0, 0, 0]) },
    { m: 'metal-dark', ...box(1.24, 0.14, 0.44, [0, .06, 0]) }], light: { y: 0, i: .9 }, footprint: 1.2, noGround: 1 },
  'lamp-wall-bracket': { label: 'Alley wall light', cat: 'Lighting', parts: [
    { m: 'metal-dark', ...box(0.12, 0.12, 0.3, [0, 0, .15]) },
    { m: 'lamp', ...box(0.22, 0.16, 0.16, [0, .02, .34]) }], light: { y: 0, i: .35 }, footprint: 0.3, noGround: 1 },
  'polaris': { label: 'Polaris night-sky beacon', cat: 'Lighting', parts: [
    { m: 'emissive-white', ...sph(0.07, [0, 0, 0]) }], footprint: 0.1, sky: 1 },

  /* ---- landscaping ---- */
  'tree-plane': { label: 'Plane / sycamore', cat: 'Greenery', parts: [
    { m: 'bark', ...cyl(0.17, 0.3, 3.4, 7, [0, 1.7, 0]) },
    { m: 'leaf-1', ...sph(1.9, [0, 4.5, 0], 1.15, .95, 1.1) },
    { m: 'leaf-2', ...sph(1.35, [.9, 5.3, -.4], 1, .85, 1) },
    { m: 'leaf-1', ...sph(1.15, [-1.0, 5.0, .5], 1, .9, 1) }], shadow: 1, canopy: 5.6, footprint: 2.2, tint: 'tree' },
  'tree-oak': { label: 'Oak', cat: 'Greenery', parts: [
    { m: 'bark', ...cyl(0.26, 0.44, 2.8, 7, [0, 1.4, 0]) },
    { m: 'leaf-2', ...sph(2.6, [0, 4.2, 0], 1.2, .9, 1.15) },
    { m: 'leaf-1', ...sph(1.6, [1.4, 4.9, .6]) }], shadow: 1, canopy: 6.4, footprint: 3, tint: 'tree' },
  'tree-palm': { label: 'Palm', cat: 'Greenery', parts: [
    { m: 'bark', ...cyl(0.18, 0.28, 6.2, 8, [0, 3.1, 0]) },
    { m: 'leaf-2', ...box(0.35, 0.1, 3.4, [0, 6.3, 1.5], [.35, 0, 0]) },
    { m: 'leaf-2', ...box(0.35, 0.1, 3.4, [0, 6.3, -1.5], [-.35, 0, 0]) },
    { m: 'leaf-2', ...box(3.4, 0.1, 0.35, [1.5, 6.3, 0], [0, 0, .35]) },
    { m: 'leaf-2', ...box(3.4, 0.1, 0.35, [-1.5, 6.3, 0], [0, 0, -.35]) },
    { m: 'leaf-1', ...sph(0.42, [0, 6.35, 0]) }], shadow: 1, canopy: 6.6, footprint: 2, tint: 'tree' },
  'tree-cherry': { label: 'Ornamental cherry', cat: 'Greenery', parts: [
    { m: 'bark', ...cyl(0.1, 0.17, 2.2, 6, [0, 1.1, 0]) },
    { m: 'blossom', ...sph(1.5, [0, 2.9, 0], 1.2, .9, 1.1) },
    { m: 'blossom', ...sph(0.9, [.8, 3.4, .3]) }], shadow: 1, canopy: 4, footprint: 1.8, tint: 'bloom' },
  'tree-olive': { label: 'Olive', cat: 'Greenery', parts: [
    { m: 'bark', ...cyl(0.14, 0.22, 1.6, 6, [0, .8, 0]) },
    { m: 'leaf-2', ...sph(1.15, [0, 2.2, 0], 1.1, 1, 1.1) }], shadow: 1, canopy: 3.2, footprint: 1.6, tint: 'tree' },
  'shrub': { label: 'Median shrub / hedge block', cat: 'Greenery', parts: [
    { m: 'leaf-2', ...box(1.5, .85, 1.1, [0, .42, 0]) }], shadow: 1, footprint: 1.5, tint: 'tree' },
  'flower-bed': { label: 'Seasonal planting', cat: 'Greenery', parts: [
    { m: 'soil', ...box(1.5, .25, .8, [0, .12, 0]) },
    { m: 'bloom', ...box(1.4, .28, .7, [0, .35, 0]) }], footprint: 1.6 },
  'planter': { label: 'Street planter', cat: 'Greenery', parts: [
    { m: 'paver-sw', ...box(1.3, .6, .8, [0, .3, 0]) },
    { m: 'soil', ...box(1.16, .12, .66, [0, .62, 0]) },
    { m: 'leaf-1', ...box(1.1, .5, .6, [0, .85, 0]) }], footprint: 1.4 },
  'grass-patch': { label: 'Swale / rain garden', cat: 'Greenery', parts: [
    { m: 'grass', ...box(3.0, .12, 1.6, [0, .06, 0]) }], footprint: 3 },

  /* ---- seating, litter, bikes ---- */
  'bench': { label: 'Bench · timber + cast', cat: 'Street', parts: [
    { m: 'metal-dark', ...box(0.08, .45, .55, [-.7, .22, 0]) },
    { m: 'metal-dark', ...box(0.08, .45, .55, [.7, .22, 0]) },
    { m: 'timber', ...box(1.8, .08, .48, [0, .47, 0]) },
    { m: 'timber', ...box(1.8, .34, .07, [0, .68, -.22], [.18, 0, 0]) }], shadow: 1, footprint: 1.9 },
  'bin': { label: 'Litter bin', cat: 'Street', parts: [
    { m: 'metal-dark', ...cyl(0.26, 0.22, .78, 12, [0, .39, 0]) },
    { m: 'metal', ...cyl(0.29, 0.29, .1, 12, [0, .82, 0]) }], footprint: .6 },
  'bike-rack': { label: 'Cycle hangar / stands', cat: 'Street', parts: [
    { m: 'metal', ...cyl(0.16, 0.16, 1.0, 10, [0, .5, 0], [0, 0, X]) },
    { m: 'metal', ...cyl(0.16, 0.16, 1.0, 10, [0, .5, .9], [0, 0, X]) },
    { m: 'metal', ...box(1.1, .09, .09, [0, .06, .45]) }], footprint: 1.2 },
  'bike-dockless': { label: 'Parked scooters / bikes', cat: 'Street', parts: [
    { m: 'paint-green', ...box(.2, .9, 1.05, [0, .5, 0], [0, 0, .12]) },
    { m: 'metal-dark', ...box(.1, .1, .5, [0, .95, .1], [0, 0, .3]) }], footprint: .8 },
  'bus-shelter': { label: 'Bus shelter · glass + frame', cat: 'Transit', parts: [
    { m: 'metal', ...box(2.7, .12, 1.4, [0, 2.42, 0]) },
    { m: 'glass', ...box(.06, 2.2, 1.4, [-1.32, 1.25, 0]) },
    { m: 'glass', ...box(2.7, 2.2, .06, [0, 1.25, -.68]) },
    { m: 'glass', ...box(.06, 2.2, 1.4, [1.32, 1.25, 0]) },
    { m: 'metal', ...box(.09, 2.35, .09, [-1.32, 1.2, .68]) },
    { m: 'metal', ...box(.09, 2.35, .09, [1.32, 1.2, .68]) },
    { m: 'timber', ...box(2.4, .08, .45, [0, .5, .35]) },
    { m: 'lamp', ...box(1.6, .06, .12, [0, 2.34, .3]) }], shadow: 1, light: { y: 2.3, i: .4 }, footprint: 2.8 },
  'bus-flag': { label: 'Bus stop flag', cat: 'Transit', parts: [
    { m: 'metal', ...cyl(0.05, 0.06, 3.0, 8, [0, 1.5, 0]) },
    { m: 'paint-yellow', ...box(.52, .68, .05, [0, 2.7, .04]) },
    { m: 'bus-red', ...box(.4, .4, .07, [0, 2.7, .07]) }], footprint: .3 },
  'tram-flag': { label: 'Tram stop totem', cat: 'Transit', parts: [
    { m: 'metal', ...box(.22, 3.2, .22, [0, 1.6, 0]) },
    { m: 'emissive-cyan', ...box(.7, .7, .1, [0, 3.1, .06]) }], footprint: .5 },
  'parking-meter': { label: 'Parking meter / pay & display', cat: 'Street', parts: [
    { m: 'metal', ...cyl(0.05, 0.06, 1.35, 8, [0, .68, 0]) },
    { m: 'metal-dark', ...box(.3, .42, .2, [0, 1.5, 0]) },
    { m: 'emissive-white', ...box(.2, .12, .02, [0, 1.56, .11]) }], footprint: .35 },
  'pay-station': { label: 'Car park pay station', cat: 'Street', parts: [
    { m: 'metal-dark', ...box(.62, 1.4, .34, [0, .7, 0]) },
    { m: 'emissive-white', ...box(.42, .3, .03, [0, 1.15, .19]) }], footprint: .7 },
  'hydrant': { label: 'Fire hydrant', cat: 'Street', parts: [
    { m: 'paint-red', ...cyl(0.15, 0.18, .68, 10, [0, .34, 0]) },
    { m: 'paint-red', ...sph(0.17, [0, .72, 0]) },
    { m: 'paint-yellow', ...box(.44, .1, .1, [0, .5, 0]) }], footprint: .4 },
  'post-box': { label: 'Letter box', cat: 'Street', parts: [
    { m: 'paint-red', ...box(.55, 1.2, .5, [0, .6, 0]) },
    { m: 'paint-red', ...sph(.28, [0, 1.28, 0], 1, .5, 1) }], footprint: .6, shadow: 1 },
  'bollard-tactile-node': { label: 'Tactile traverse node', cat: 'Pedestrian', parts: [
    { m: 'paint-yellow-lume', ...cyl(0.15, 0.15, .04, 12, [0, .02, 0]) }], footprint: .3 },
  'phone-box': { label: 'Telephone kiosk', cat: 'Street', parts: [
    { m: 'paint-red', ...box(.95, 2.35, .95, [0, 1.18, 0]) },
    { m: 'glass', ...box(.8, 1.5, .02, [0, 1.3, .48]) },
    { m: 'paint-red', ...box(1.1, .18, 1.1, [0, 2.42, 0]) }], shadow: 1, footprint: 1.1 },
  'kiosk': { label: 'News kiosk', cat: 'Street', parts: [
    { m: 'timber', ...box(2.6, 2.2, 1.7, [0, 1.1, 0]) },
    { m: 'metal-dark', ...box(2.9, .16, 2.0, [0, 2.28, 0]) },
    { m: 'glass', ...box(2.2, 1.1, .05, [0, 1.25, .87]) },
    { m: 'lamp', ...box(2.2, .07, .1, [0, 1.85, .9]) }], shadow: 1, light: { y: 1.9, i: .35 }, footprint: 2.9 },
  'art-panel': { label: 'Underpass art panel', cat: 'Street', parts: [
    { m: 'mural', ...box(3.4, 1.9, .06, [0, 1.6, 0]) }], footprint: 3.4 },
  'cctv': { label: 'CCTV camera', cat: 'Street', parts: [
    { m: 'metal', ...cyl(0.05, 0.05, 4.4, 6, [0, 2.2, 0]) },
    { m: 'metal', ...box(.14, .1, .5, [0, 4.4, .2], [.2, 0, 0]) },
    { m: 'paint-black', ...box(.24, .26, .4, [0, 4.3, .5]) }], footprint: .3 },
  'sign-frame': { label: 'Direction sign (fingerpost)', cat: 'Signing', parts: [
    { m: 'metal', ...cyl(0.07, 0.08, 3.4, 8, [0, 1.7, 0]) },
    { m: 'sign-blue', ...box(2.0, .38, .07, [.5, 3.0, 0]) },
    { m: 'sign-white', ...box(1.7, .34, .07, [.35, 2.55, .02]) },
    { m: 'sign-green', ...box(1.9, .34, .07, [.45, 2.15, 0]) }], shadow: 1, footprint: .4 },
  'sign-round': { label: 'Regulatory round sign', cat: 'Signing', parts: [
    { m: 'metal', ...cyl(0.045, 0.055, 2.3, 6, [0, 1.15, 0]) },
    { m: 'paint-white', ...cyl(0.36, 0.36, .04, 16, [0, 2.35, 0], [X, 0, 0]) },
    { m: 'paint-red', ...cyl(0.36, 0.36, .05, 16, [0, 2.35, -.005], [X, 0, 0], 0.68) }], footprint: .3 },
  'sign-triangle': { label: 'Warning triangle', cat: 'Signing', parts: [
    { m: 'metal', ...cyl(0.045, 0.055, 2.1, 6, [0, 1.05, 0]) },
    { m: 'paint-white', ...cone(0.42, 0.72, [0, 2.42, 0], 3) }], footprint: .3 },
  'crossbuck': { label: 'Railway crossbuck', cat: 'Rail', parts: [
    { m: 'metal', ...cyl(0.05, 0.06, 2.4, 6, [0, 1.2, 0]) },
    { m: 'paint-white', ...box(1.5, .22, .05, [0, 2.35, 0], [0, 0, .55]) },
    { m: 'paint-white', ...box(1.5, .22, .05, [0, 2.35, 0], [0, 0, -.55]) }], footprint: .4 },
  'crossing-light': { label: 'Level crossing light head', cat: 'Rail', parts: [
    { m: 'metal-dark', ...cyl(0.09, 0.11, 2.6, 8, [0, 1.3, 0]) },
    { m: 'metal-dark', ...box(.7, .34, .22, [0, 2.68, 0]) },
    { m: 'lamp-red', ...sph(0.15, [-.22, 2.68, .12]) },
    { m: 'lamp-red', ...sph(0.15, [.22, 2.68, .12]) },
    { m: 'metal-dark', ...cyl(0.14, 0.14, .2, 10, [0, 2.95, 0]) }], blinking: 1, footprint: .6 },
  'crossing-bell': { label: 'Crossing bell + plinth', cat: 'Rail', parts: [
    { m: 'concrete-sw', ...box(.6, .8, .5, [0, .4, 0]) },
    { m: 'metal-dark', ...cone(0.22, .3, [0, .95, 0]) }], footprint: .7 },
  'gate-barrier': { label: 'Crossing barrier arm', cat: 'Rail', parts: [
    { m: 'paint-white', ...box(0.16, 0.16, 6.2, [0, 0, 3.1]) },
    { m: 'paint-red', ...box(0.17, 0.17, 0.7, [0, 0, 5.6]) },
    { m: 'paint-red', ...box(0.17, 0.17, 0.7, [0, 0, 3.6]) },
    { m: 'emissive-red', ...box(0.1, 0.1, 0.16, [0, .12, 6.15]) }], animated: 'gate', footprint: .3 },
  'gate-post': { label: 'Barrier machinery cabinet', cat: 'Rail', parts: [
    { m: 'concrete-sw', ...cyl(0.3, 0.34, 1.1, 10, [0, .55, 0]) },
    { m: 'metal-dark', ...cyl(0.36, 0.36, .16, 10, [0, 1.16, 0]) }], footprint: .8 },
  'signal-head': { label: 'Traffic signal (3 aspect)', cat: 'Signing', parts: [
    { m: 'metal', ...cyl(0.11, 0.13, 3.6, 8, [0, 1.8, 0]) },
    { m: 'metal-dark', ...box(.34, .92, .3, [0, 4.1, .1]) },
    { m: 'lamp-red', ...sph(0.12, [0, 4.44, .26]) },
    { m: 'lamp-amber', ...sph(0.12, [0, 4.1, .26]) },
    { m: 'lamp-green', ...sph(0.12, [0, 3.76, .26]) },
    { m: 'metal-dark', ...box(.5, .1, .5, [0, 3.62, .06]) }], signal: 1, footprint: .5 },
  'signal-ped': { label: 'Pedestrian signal', cat: 'Signing', parts: [
    { m: 'metal', ...cyl(0.07, 0.08, 2.4, 6, [0, 1.2, 0]) },
    { m: 'metal-dark', ...box(.28, .58, .2, [0, 2.55, .06]) },
    { m: 'lamp-red', ...sph(0.08, [0, 2.72, .17]) },
    { m: 'lamp-green', ...sph(0.08, [0, 2.38, .17]) }], signal: 1, footprint: .3 },
  'speed-camera': { label: 'Average-speed camera gantry', cat: 'Signing', parts: [
    { m: 'metal', ...box(0.22, 5.2, 0.22, [0, 2.6, 0]) },
    { m: 'sign-yellow', ...box(1.3, .7, .12, [0, 4.9, .2]) },
    { m: 'paint-black', ...box(.4, .3, .5, [-.3, 4.2, .3], [0, .4, 0]) }], footprint: .6 },
  'gantry-motorway': { label: 'Overhead gantry sign', cat: 'Signing', parts: [
    { m: 'metal', ...cyl(0.28, 0.34, 8.6, 8, [-9, 4.3, 0]) },
    { m: 'metal', ...cyl(0.28, 0.34, 8.6, 8, [9, 4.3, 0]) },
    { m: 'metal', ...box(19, .5, .6, [0, 8.4, 0]) },
    { m: 'sign-blue', ...box(15, 1.9, .3, [0, 7.3, .1]) }], shadow: 1, footprint: 19 },
  'guard-beam': { label: 'Guardrail · W-beam', cat: 'Safety', parts: [
    { m: 'metal', ...box(.14, .7, .09, [0, .45, 0]) },
    { m: 'metal', ...box(.31, .1, 2.0, [0, .6, 0]) },
    { m: 'metal', ...box(.31, .1, 2.0, [0, .48, 0], [0, 0, .04]) }], shadow: 1, rail: 1, footprint: .35 },
  'guard-wire': { label: 'Wire rope safety fence', cat: 'Safety', parts: [
    { m: 'metal-dark', ...cyl(0.08, 0.09, 1.15, 6, [0, .58, 0]) },
    { m: 'metal', ...box(.03, .03, 4.0, [0, 1.02, 0]) },
    { m: 'metal', ...box(.03, .03, 4.0, [0, .82, 0]) }], rail: 1, footprint: .2 },
  'guard-stone': { label: 'Stone wall / kerb guard', cat: 'Safety', parts: [
    { m: 'stone-wall', ...box(.55, .68, 2.2, [0, .34, 0]) }], rail: 1, shadow: 1, footprint: .6 },
  'guard-timber': { label: 'Timber post & rail', cat: 'Safety', parts: [
    { m: 'bark', ...box(.14, 1.1, .14, [0, .55, 0]) },
    { m: 'timber', ...box(.1, .12, 2.4, [0, .95, 0]) },
    { m: 'timber', ...box(.1, .12, 2.4, [0, .6, 0]) }], rail: 1, footprint: .2 },
  'handrail-steel': { label: 'Pedestrian handrail', cat: 'Safety', parts: [
    { m: 'metal', ...cyl(0.035, 0.04, 1.05, 6, [0, .52, 0]) },
    { m: 'metal', ...cyl(0.045, 0.05, 2.4, 8, [0, 1.02, 0], [X, 0, 0]) }], rail: 1, footprint: .12 },
  'fence-mesh': { label: 'Line fencing · mesh panel', cat: 'Safety', parts: [
    { m: 'metal-dark', ...cyl(0.05, 0.05, 2.1, 6, [0, 1.05, 0]) },
    { m: 'fence-mesh-mat', ...box(0.04, 1.9, 2.9, [0, 1.05, 0]) }], rail: 1, footprint: .1 },
  'snow-pole': { label: 'Snow pole', cat: 'Safety', parts: [
    { m: 'paint-white', ...cyl(0.04, 0.05, 2.6, 5, [0, 1.3, 0]) },
    { m: 'paint-orange', ...box(.5, .14, .03, [0, 2.4, 0]) }], footprint: .1 },
  'steps': { label: 'Plaza steps', cat: 'Pedestrian', parts: [
    { m: 'paver-sw', ...box(4.0, .17, .38, [0, .085, -.55]) },
    { m: 'paver-sw', ...box(4.0, .17, .38, [0, .255, -.17]) },
    { m: 'paint-white', ...box(4.0, .02, .1, [0, .35, .05]) }], footprint: 4 },
  'ramp-disability': { label: 'Accessible ramp run', cat: 'Pedestrian', parts: [
    { m: 'concrete-sw', ...box(1.6, .1, 3.2, [0, .12, 0], [.06, 0, 0]) },
    { m: 'paint-yellow-lume', ...box(1.6, .02, .3, [0, .2, 1.4]) }], footprint: 1.6 },
  'subway-entrance': { label: 'Metro entrance · glazed', cat: 'Transit', parts: [
    { m: 'concrete-sw', ...box(4.6, .3, 3.0, [0, .15, 0]) },
    { m: 'metal', ...box(4.4, 2.9, .12, [0, 1.6, -1.4]) },
    { m: 'glass', ...box(4.4, 2.6, .06, [0, 1.6, 1.4]) },
    { m: 'metal', ...box(.14, 2.9, 2.9, [-2.2, 1.6, 0]) },
    { m: 'metal', ...box(.14, 2.9, 2.9, [2.2, 1.6, 0]) },
    { m: 'concrete-sw', ...box(4.2, .2, 2.6, [0, 3.05, 0]) },
    { m: 'emissive-blue', ...box(2.6, .34, .06, [0, 2.55, 1.44]) },
    { m: 'lamp', ...box(3.6, .08, 1.4, [0, 2.9, 0]) }], shadow: 1, light: { y: 2.6, i: .7 }, footprint: 4.8 },
  'vent-grate': { label: 'Subway vent grille', cat: 'Transit', parts: [
    { m: 'metal-dark', ...box(1.5, .1, 1.0, [0, .05, 0]) },
    { m: 'metal', ...box(1.36, .12, .08, [0, .06, -.3]) },
    { m: 'metal', ...box(1.36, .12, .08, [0, .06, 0]) },
    { m: 'metal', ...box(1.36, .12, .08, [0, .06, .3]) }], footprint: 1.5, noGround: 1 },
  'escalator': { label: 'Escalator / stairs flight', cat: 'Transit', parts: [
    { m: 'metal-dark', ...box(1.3, .3, 6.4, [0, -1.4, 0], [.24, 0, 0]) },
    { m: 'glass', ...box(.06, 1.1, 6.4, [-.72, -1.0, 0], [.24, 0, 0]) },
    { m: 'glass', ...box(.06, 1.1, 6.4, [.72, -1.0, 0], [.24, 0, 0]) }], footprint: 1.6 },
  'platform-seat': { label: 'Metro platform bench', cat: 'Transit', parts: [
    { m: 'metal', ...box(2.0, .1, .5, [0, .48, 0]) },
    { m: 'metal-dark', ...box(.1, .48, .5, [-.9, .24, 0]) },
    { m: 'metal-dark', ...box(.1, .48, .5, [.9, .24, 0]) }], footprint: 2 },
  'platform-sign': { label: 'Metro nameplate', cat: 'Transit', parts: [
    { m: 'metal', ...box(.08, 2.6, .08, [0, 1.3, 0]) },
    { m: 'metro-tile-mat', ...box(.04, .7, 3.2, [0, 2.2, 0]) }], footprint: .2 },
  'manhole': { label: 'Manhole cover', cat: 'Drainage', parts: [
    { m: 'iron', ...cyl(0.34, 0.34, .05, 14, [0, .02, 0]) },
    { m: 'metal-dark', ...cyl(0.24, 0.24, .06, 14, [0, .025, 0]) }], footprint: .7, noGround: 1 },
  'gully': { label: 'Kerb inlet / gully grate', cat: 'Drainage', parts: [
    { m: 'iron', ...box(.6, .05, .34, [0, .015, 0]) },
    { m: 'metal-dark', ...box(.52, .07, .05, [0, .02, -.1]) },
    { m: 'metal-dark', ...box(.52, .07, .05, [0, .02, 0]) },
    { m: 'metal-dark', ...box(.52, .07, .05, [0, .02, .1]) }], footprint: .65, noGround: 1 },
  'catch-basin': { label: 'Storm drain outfall', cat: 'Drainage', parts: [
    { m: 'concrete-sw', ...box(1.2, .8, .9, [0, .4, 0]) },
    { m: 'metal-dark', ...box(.9, .5, .06, [0, .35, .46]) }], footprint: 1.2 },
  'speed-cushion': { label: 'Speed cushion', cat: 'Traffic calming', parts: [
    { m: 'asphalt-hump', ...cyl(0.85, 0.9, .1, 14, [0, .05, 0], [0, 0, 0], 1) },
    { m: 'paint-white', ...cyl(0.55, 0.55, .11, 14, [0, .055, 0]) }], footprint: 1.9 },
  'gate-farm': { label: 'Cattle grid + gate', cat: 'Rural', parts: [
    { m: 'metal-dark', ...box(3.4, .12, 2.2, [0, .04, 0]) },
    { m: 'metal', ...box(.12, 1.2, .12, [-1.8, .6, 0]) },
    { m: 'metal', ...box(3.7, .09, .09, [0, 1.15, 0]) }], footprint: 3.4 },
  'milepost': { label: 'Trackside milepost', cat: 'Rail', parts: [
    { m: 'paint-white', ...box(.16, .8, .1, [0, .4, 0]) },
    { m: 'paint-black', ...box(.16, .18, .11, [0, .68, 0]) }], footprint: .2 },
  'telegraph': { label: 'Line-clearance gantry', cat: 'Rail', parts: [
    { m: 'metal', ...box(.28, 8.4, .28, [0, 4.2, 0]) },
    { m: 'metal', ...box(5.6, .22, .22, [1.4, 8.1, 0]) },
    { m: 'metal-dark', ...box(.1, .8, .1, [3.9, 7.6, 0]) }], shadow: 1, footprint: .5 },
  'catenary-mast': { label: 'OLE mast · cantilever', cat: 'Rail', parts: [
    { m: 'metal', ...cyl(0.14, 0.19, 8.8, 8, [0, 4.4, 0]) },
    { m: 'metal', ...box(3.0, .16, .16, [1.4, 8.5, 0], [0, 0, -.06]) },
    { m: 'metal-dark', ...box(.08, 1.5, .08, [2.7, 7.8, 0]) },
    { m: 'metal', ...box(.1, .1, 2.6, [2.9, 7.0, 0]) }], shadow: 1, footprint: .6 },
  'sleep': { label: 'Platform screen door', cat: 'Transit', parts: [
    { m: 'glass', ...box(2.6, 2.4, .07, [0, 1.2, 0]) },
    { m: 'metal-dark', ...box(2.7, .14, .12, [0, 2.46, 0]) },
    { m: 'emissive-cyan', ...box(2.2, .07, .06, [0, .12, .06]) }], footprint: 2.7 },
  'car': { label: 'Car (traffic)', cat: 'Traffic', parts: [
    { m: 'paint-car', ...box(1.82, .68, 4.2, [0, .72, 0]) },
    { m: 'paint-car', ...box(1.66, .52, 2.1, [0, 1.24, -.1]) },
    { m: 'glass', ...box(1.68, .44, 1.9, [0, 1.3, -.1]) },
    { m: 'tyre', ...cyl(0.33, 0.33, .2, 10, [-.85, .33, 1.3], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.33, 0.33, .2, 10, [.85, .33, 1.3], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.33, 0.33, .2, 10, [-.85, .33, -1.35], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.33, 0.33, .2, 10, [.85, .33, -1.35], [0, 0, X]) },
    { m: 'emissive-white', ...box(.5, .16, .05, [-.55, .78, 2.11]) },
    { m: 'emissive-white', ...box(.5, .16, .05, [.55, .78, 2.11]) },
    { m: 'emissive-red', ...box(.44, .14, .05, [-.6, .82, -2.11]) },
    { m: 'emissive-red', ...box(.44, .14, .05, [.6, .82, -2.11]) }], shadow: 1, vehicle: 1, footprint: 4.4 },
  'van': { label: 'Van / HGV', cat: 'Traffic', parts: [
    { m: 'paint-van', ...box(2.1, 1.1, 4.6, [0, 1.0, .5]) },
    { m: 'paint-van', ...box(2.0, 1.5, 2.0, [0, 1.2, -1.9]) },
    { m: 'glass', ...box(1.9, .7, 1.9, [0, 1.75, -1.9]) },
    { m: 'tyre', ...cyl(0.44, 0.44, .24, 10, [-.98, .44, 1.7], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.44, 0.44, .24, 10, [.98, .44, 1.7], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.44, 0.44, .24, 10, [-.98, .44, -1.8], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.44, 0.44, .24, 10, [.98, .44, -1.8], [0, 0, X]) }], shadow: 1, vehicle: 1, footprint: 6.6 },
  'bus': { label: 'Articulated bus', cat: 'Traffic', parts: [
    { m: 'paint-bus', ...box(2.5, 2.5, 11.6, [0, 1.85, 0]) },
    { m: 'glass', ...box(2.52, 1.0, 10.4, [0, 2.55, 0]) },
    { m: 'paint-bus', ...box(2.4, .5, 1.4, [0, .55, 4.4]) },
    { m: 'tyre', ...cyl(0.52, 0.52, .3, 10, [-1.15, .52, 3.9], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.52, 0.52, .3, 10, [1.15, .52, 3.9], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.52, 0.52, .3, 10, [-1.15, .52, -3.6], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.52, 0.52, .3, 10, [1.15, .52, -3.6], [0, 0, X]) },
    { m: 'emissive-white', ...box(1.9, .18, .06, [0, 1.05, 5.81]) },
    { m: 'emissive-red', ...box(1.9, .2, .06, [0, 1.05, -5.81]) }], shadow: 1, vehicle: 1, footprint: 12 },
  'cyclist': { label: 'Cyclist', cat: 'Traffic', parts: [
    { m: 'tyre', ...cyl(0.34, 0.34, .04, 12, [0, .34, .58], [0, 0, X]) },
    { m: 'tyre', ...cyl(0.34, 0.34, .04, 12, [0, .34, -.58], [0, 0, X]) },
    { m: 'bike-frame', ...box(.06, .5, 1.1, [0, .58, 0], [.1, 0, 0]) },
    { m: 'paint-hi-vis', ...box(.34, .78, .3, [0, 1.28, .04], [.28, 0, 0]) },
    { m: 'paint-helmet', ...sph(.14, [0, 1.78, .16]) }], shadow: 1, vehicle: 1, footprint: 1.7 },
  'pedestrian': { label: 'Pedestrian', cat: 'Traffic', parts: [
    { m: 'cloth-a', ...box(.36, .74, .22, [0, 1.06, 0]) },
    { m: 'cloth-b', ...box(.13, .68, .14, [-.11, .34, 0]) },
    { m: 'cloth-b', ...box(.13, .68, .14, [.11, .34, 0]) },
    { m: 'skin', ...sph(.13, [0, 1.58, 0]) }], shadow: 1, vehicle: 1, footprint: .5 },
  'person-seated': { label: 'Person on bench', cat: 'Street', parts: [
    { m: 'cloth-a', ...box(.36, .6, .24, [0, .78, 0]) },
    { m: 'cloth-b', ...box(.3, .14, .42, [0, .5, .2]) },
    { m: 'skin', ...sph(.12, [0, 1.2, 0]) }], footprint: .5 },
  'market-stall': { label: 'Market stall', cat: 'Street', parts: [
    { m: 'canvas', ...box(2.6, .1, 2.2, [0, 2.35, 0], [-.08, 0, 0]) },
    { m: 'timber', ...box(.1, 2.3, .1, [-1.2, 1.15, -.95]) },
    { m: 'timber', ...box(.1, 2.3, .1, [1.2, 1.15, -.95]) },
    { m: 'timber', ...box(2.4, .9, 1.0, [0, .45, 0]) }], shadow: 1, footprint: 2.7 },
  'fountain': { label: 'Plaza fountain', cat: 'Pedestrian', parts: [
    { m: 'granite', ...cyl(2.2, 2.4, .55, 24, [0, .27, 0]) },
    { m: 'water-shallow', ...cyl(2.05, 2.05, .1, 24, [0, .52, 0]) },
    { m: 'granite', ...cyl(0.35, 0.5, 1.4, 16, [0, 1.2, 0]) },
    { m: 'water-shallow', ...sph(.5, [0, 1.95, 0], 1, 1.6, 1) }], shadow: 1, footprint: 4.8 },
  'street-tree-guard': { label: 'Tree pit guard rail', cat: 'Pedestrian', parts: [
    { m: 'metal-dark', ...box(1.1, .06, 1.1, [0, .55, 0]) },
    { m: 'metal-dark', ...cyl(0.035, 0.04, .55, 6, [-.5, .28, -.5]) },
    { m: 'metal-dark', ...cyl(0.035, 0.04, .55, 6, [.5, .28, -.5]) },
    { m: 'metal-dark', ...cyl(0.035, 0.04, .55, 6, [-.5, .28, .5]) },
    { m: 'metal-dark', ...cyl(0.035, 0.04, .55, 6, [.5, .28, .5]) }], footprint: 1.1 },
  'soil-pit': { label: 'Tree pit soil', cat: 'Greenery', parts: [
    { m: 'soil', ...box(1.4, .06, 1.4, [0, .12, 0]) }], footprint: 1.4, noGround: 1 },
  'sleeper': { label: 'Railway sleeper / tie', cat: 'Rail', parts: [
    { m: 'sleeper-mat', ...box(2.55, .15, .27, [0, .075, 0]) }], footprint: 2.6, noGround: 1 },
  'tactile-paving': { label: 'Tactile paving · truncated domes', cat: 'Pedestrian', parts: [
    { m: 'tactile-mat', ...box(1, .05, 1, [0, .025, 0]) }], footprint: 1, noGround: 1 },
  'block-building': { label: 'Context massing block', cat: 'Massing', parts: [
    { m: 'facade', ...box(1, 1, 1, [0, 0, 0]) }], shadow: 1, unit: 1, footprint: 1 },
  'wall-block': { label: 'Party wall', cat: 'Massing', parts: [
    { m: 'facade-dull', ...box(1, 1, 1, [0, 0, 0]) }], unit: 1, footprint: 1 },
  'pavilion': { label: 'Park pavilion', cat: 'Street', parts: [
    { m: 'timber', ...box(4.2, .22, 3.0, [0, 2.7, 0]) },
    { m: 'metal', ...box(.16, 2.6, .16, [-1.8, 1.3, -1.2]) },
    { m: 'metal', ...box(.16, 2.6, .16, [1.8, 1.3, -1.2]) },
    { m: 'metal', ...box(.16, 2.6, .16, [-1.8, 1.3, 1.2]) },
    { m: 'metal', ...box(.16, 2.6, .16, [1.8, 1.3, 1.2]) },
    { m: 'paver-sw', ...box(4.4, .14, 3.2, [0, .07, 0]) }], shadow: 1, footprint: 4.4 },
  'pier-pile': { label: 'Bridge pier', cat: 'Structures', parts: [
    { m: 'concrete-sw', ...box(2.4, 1, 3.2, [0, .5, 0]) }], unit: 1, footprint: 3.2 },
  'bearing-pad': { label: 'Pier bearing pad', cat: 'Structures', parts: [
    { m: 'metal-dark', ...box(2.8, .18, 3.8, [0, .09, 0]) }], footprint: 3.8 },
};

/** materials referenced by props, kept out of the pavement bucket set */
export const PROP_MATERIALS = {
  'metal': { kind: 'metal', rough: .38, metal: .85, tint: [.62, .63, .66] },
  'metal-dark': { kind: 'metal', rough: .55, metal: .8, tint: [.22, .23, .25] },
  'concrete-sw': { kind: 'concrete', rough: .85, tint: [.62, .62, .6], tex: 'concrete', scale: 1.6 },
  'paver-sw': { kind: 'paver', rough: .8, tint: [.6, .57, .52], tex: 'paver', scale: .8 },
  'granite': { kind: 'paver', rough: .6, tint: [.66, .65, .62], tex: 'granite', scale: .9 },
  'stone-wall': { kind: 'paver', rough: .85, tint: [.58, .55, .5], tex: 'cobble', scale: 1.1 },
  'iron': { kind: 'metal', rough: .62, metal: .72, tint: [.24, .24, .25], tex: 'iron', scale: .35 },
  'paint-white': { kind: 'paint', rough: .5, tint: [.93, .93, .9] },
  'paint-yellow': { kind: 'paint', rough: .5, tint: [.94, .78, .12] },
  'paint-yellow-lume': { kind: 'paint', rough: .45, tint: [.96, .86, .32], emissive: [.34, .28, .04] },
  'paint-red': { kind: 'paint', rough: .5, tint: [.72, .12, .1] },
  'paint-orange': { kind: 'paint', rough: .55, tint: [.92, .45, .06] },
  'paint-black': { kind: 'paint', rough: .6, tint: [.09, .09, .1] },
  'paint-green': { kind: 'paint', rough: .5, tint: [.18, .55, .4] },
  'paint-hi-vis': { kind: 'paint', rough: .45, tint: [.78, .92, .1] },
  'paint-helmet': { kind: 'paint', rough: .35, tint: [.9, .9, .92] },
  'paint-car': { kind: 'carPaint', rough: .25, metal: .25, tint: [.5, .52, .55] },
  'paint-van': { kind: 'carPaint', rough: .3, tint: [.85, .85, .84] },
  'paint-bus': { kind: 'carPaint', rough: .3, tint: [.75, .16, .12] },
  'bus-red': { kind: 'paint', rough: .45, tint: [.78, .1, .1] },
  'carPaint': { kind: 'carPaint', rough: .28, tint: [.5, .5, .55] },
  'bike-frame': { kind: 'metal', rough: .3, metal: .6, tint: [.14, .14, .16] },
  'tyre': { kind: 'rubber', rough: .92, tint: [.075, .075, .08] },
  'skin': { kind: 'skin', rough: .7, tint: [.72, .56, .46] },
  'cloth-a': { kind: 'cloth', rough: .85, tint: [.3, .34, .46] },
  'cloth-b': { kind: 'cloth', rough: .85, tint: [.2, .2, .24] },
  'canvas': { kind: 'cloth', rough: .8, tint: [.78, .74, .66] },
  'glass': { kind: 'glass', rough: .06, metal: 0, tint: [.42, .55, .6], opacity: .32 },
  'timber': { kind: 'timber', rough: .8, tint: [.42, .3, .19], tex: 'timber', scale: .8 },
  'bark': { kind: 'bark', rough: .92, tint: [.31, .26, .21], tex: 'bark', scale: .6 },
  'leaf-1': { kind: 'foliage', rough: .85, tint: [.21, .35, .16], tintKey: 'tree' },
  'leaf-2': { kind: 'foliage', rough: .85, tint: [.27, .42, .19], tintKey: 'tree' },
  'blossom': { kind: 'foliage', rough: .8, tint: [.86, .55, .62], tintKey: 'bloom' },
  'soil': { kind: 'soil', rough: .95, tint: [.22, .18, .13] },
  'grass': { kind: 'foliage', rough: .9, tint: [.28, .4, .2], tex: 'grass', scale: .8 },
  'mural': { kind: 'mural', rough: .7, tint: [.6, .55, .5], tex: 'mural', scale: 1 },
  'lamp': { kind: 'lamp', rough: .25, tint: [.95, .9, .74], emissive: [.9, .82, .58], emissiveScale: 1 },
  'emissive-white': { kind: 'lamp', rough: .3, tint: [.9, .9, .9], emissive: [.7, .7, .68] },
  'emissive-red': { kind: 'lamp', rough: .3, tint: [.6, .1, .08], emissive: [.85, .1, .06] },
  'emissive-blue': { kind: 'lamp', rough: .3, tint: [.2, .4, .8], emissive: [.16, .34, .8] },
  'emissive-cyan': { kind: 'lamp', rough: .3, tint: [.3, .7, .8], emissive: [.2, .6, .68] },
  'lamp-red': { kind: 'lamp', rough: .3, tint: [.55, .08, .06], emissive: [.9, .12, .06], blink: 1 },
  'lamp-amber': { kind: 'lamp', rough: .3, tint: [.6, .4, .06], emissive: [.8, .48, .05] },
  'lamp-green': { kind: 'lamp', rough: .3, tint: [.08, .5, .12], emissive: [.1, .7, .18] },
  'sign-blue': { kind: 'sign', rough: .4, tint: [.06, .18, .5], emissive: [.02, .05, .12] },
  'sign-green': { kind: 'sign', rough: .4, tint: [.02, .3, .18] },
  'sign-yellow': { kind: 'sign', rough: .4, tint: [.92, .78, .08] },
  'fence-mesh-mat': { kind: 'fence', rough: .6, metal: .5, tint: [.4, .42, .44], tex: 'mesh', scale: .4, opacity: .78 },
  'metro-tile-mat': { kind: 'tile', rough: .3, tint: [.86, .86, .82], tex: 'tile', scale: .5 },
  'facade': { kind: 'facade', rough: .72, tint: [.44, .44, .46], tex: 'facade', scale: 1, emissive: [.04, .04, .05] },
  'facade-dull': { kind: 'facade', rough: .8, tint: [.34, .34, .36], tex: 'facade', scale: 1 },
  'water-shallow': { kind: 'water', rough: .07, tint: [.34, .52, .58], opacity: .8 },
  'asphalt-hump': { kind: 'asphalt', rough: .9, tint: [.34, .34, .36], tex: 'asphalt', scale: 1.6 },
  'sleeper-mat': { kind: 'concrete', rough: .9, tint: [.44, .42, .38], tex: 'concrete', scale: .55 },
  'tactile-mat': { kind: 'tactile', rough: .55, tint: [.88, .74, .18], tex: 'tactile', scale: .5 },
  'cones': { kind: 'paint', rough: .55, tint: [.86, .32, .04] },
  'pavement-plain': { kind: 'concrete', rough: .85, tint: [.55, .55, .54] },
};

export const PROP_GROUPS = [...new Set(Object.values(PROP_SPECS).map(p => p.cat))];
export const propAt = (key) => PROP_SPECS[key] || PROP_SPECS['bollard'];

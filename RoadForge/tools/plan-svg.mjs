// Dump a plan-view SVG of generated quads for visual topology inspection. Usage: node tools/plan-svg.mjs <scenario> <out.svg>
import { readFileSync, writeFileSync } from 'node:fs'; import vm from 'node:vm';
globalThis.RF = {};
for (const f of ['math','spline','mesh','profiles','network','roadmesh']) vm.runInThisContext(readFileSync(new URL(`../src/${f}.js`, import.meta.url),'utf8'),{filename:f});
const RF = globalThis.RF;
const scenarios = {
  cross: () => ({ nodes: { c:{p:[0,0,0]}, n:{p:[0,0,-80]}, s:{p:[0,0,80]}, e:{p:[80,0,0]}, w:{p:[-80,0,0]} }, roads: { r1:{a:'n',b:'c',profile:'avenue'}, r2:{a:'c',b:'s',profile:'avenue'}, r3:{a:'w',b:'c',profile:'street'}, r4:{a:'c',b:'e',profile:'street'} } }),
  merge: () => { const ang = 12*Math.PI/180; return { nodes: { m:{p:[0,0,0]}, a:{p:[-200,0,0]}, b:{p:[120,0,0]}, r:{p:[-200*Math.cos(ang),0,200*Math.sin(ang)]} }, roads: { hw1:{a:'a',b:'m',profile:'carriageway'}, hw2:{a:'m',b:'b',profile:'carriageway3'}, ramp:{a:'r',b:'m',profile:'ramp'} } }; },
  y: () => ({ nodes: { c:{p:[0,0,0]}, a:{p:[-120,0,0]}, b:{p:[100,0,-60]}, d:{p:[100,0,60]} }, roads: { r1:{a:'a',b:'c',profile:'collector',pts:[[-60,0,15]]}, r2:{a:'c',b:'b',profile:'street'}, r3:{a:'c',b:'d',profile:'street'} } }),
  skew: () => ({ nodes: { c:{p:[0,0,0]}, n:{p:[-40,0,-90]}, s:{p:[40,0,90]}, e:{p:[90,0,0]}, w:{p:[-90,0,0]} }, roads: { r1:{a:'n',b:'c',profile:'street'}, r2:{a:'c',b:'s',profile:'street'}, r3:{a:'w',b:'c',profile:'avenue'}, r4:{a:'c',b:'e',profile:'avenue'} } }),
  five: () => { const nodes = { c:{p:[0,0,0]} }, roads = {}; for (let i=0;i<5;i++){ const a=(i/5)*Math.PI*2+0.3; nodes['n'+i]={p:[Math.cos(a)*90,0,Math.sin(a)*90]}; roads['r'+i]={a:'c',b:'n'+i,profile:i%2?'street':'collector'}; } return { nodes, roads }; },
  bend: () => ({ nodes: { a:{p:[-80,0,0]}, m:{p:[0,0,0]}, b:{p:[60,0,60]} }, roads: { r1:{a:'a',b:'m',profile:'street'}, r2:{a:'m',b:'b',profile:'street'} } }),
};
const name = process.argv[2] || 'cross', outFile = process.argv[3] || `/tmp/plan-${name}.svg`;
const sc = scenarios[name](); const project = { settings:{}, nodes:{}, roads:{} };
for (const [id,n] of Object.entries(sc.nodes)) project.nodes[id] = { id, ...n };
for (const [id,r] of Object.entries(sc.roads)) project.roads[id] = { id, name:id, pts:[], ...r };
const net = RF.network.solve(project); const out = {};
for (const R of net.roads.values()) RF.roadmesh.buildRoad(R, net, {}, out);
for (const rec of net.nodes.values()) if (rec.kind==='junction'||rec.kind==='transition') RF.roadmesh.buildJunction(rec, net, {}, out);
const colors = { asphalt:'#55565a', concrete_road:'#8a8a86', curb:'#b9b6ae', paving:'#c9c2b4', paint_white:'#f2f2f2', paint_yellow:'#e6c04a', grass:'#6f8d4e', barrier:'#a9a9a9' };
const order = ['grass','asphalt','concrete_road','curb','paving','barrier','paint_white','paint_yellow'];
let minx=1e9,minz=1e9,maxx=-1e9,maxz=-1e9;
for (const b of Object.values(out)) for (let i=0;i<b.pos.length;i+=3){ minx=Math.min(minx,b.pos[i]); maxx=Math.max(maxx,b.pos[i]); minz=Math.min(minz,b.pos[i+2]); maxz=Math.max(maxz,b.pos[i+2]); }
const W=1400, scale = W/(maxx-minx), H=(maxz-minz)*scale;
let svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="${minx} ${minz} ${maxx-minx} ${maxz-minz}" style="background:#202020">`;
for (const mat of order) { const b = out[mat]; if (!b) continue; svg += `<g fill="${colors[mat]}" stroke="#000" stroke-opacity="0.35" stroke-width="${0.06}" stroke-linejoin="round">`;
  for (let i=0;i<b.quads.length;i+=4){ const pts=[0,1,2,3].map(k=>{const v=b.quads[i+k]*3; return b.pos[v].toFixed(3)+','+b.pos[v+2].toFixed(3);}); svg += `<polygon points="${pts.join(' ')}"/>`; } svg += '</g>'; }
svg += '</svg>'; writeFileSync(outFile, svg);
let q=0; for (const b of Object.values(out)) q+=b.quads.length/4; console.log(outFile, 'quads', q);

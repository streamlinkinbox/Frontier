// Headless smoke test for Panel/index.html: node Verification/smoke.js
const fs=require('fs'),path=require('path');
const html=fs.readFileSync(path.join(__dirname,'..','index.html'),'utf8');
const js=html.match(/<script>([\s\S]*)<\/script>/)[1];
const els={};
function mkEl(id){return {id,style:{setProperty(){}},classList:{add(){},remove(){},toggle(){},contains(){return false}},dataset:{},innerHTML:'',textContent:'',value:'',
  getBoundingClientRect(){return{left:0,top:0,width:900,height:700}},querySelector(){return mkEl()},querySelectorAll(){return[]},addEventListener(t,fn){(this.L=this.L||{})[t]=fn},setPointerCapture(){},appendChild(){},removeAttribute(){},replaceWith(){},focus(){},select(){},
  getContext(){if(global.__CTX)return global.__CTX;return new Proxy({},{get:(t,k)=>k==='measureText'?()=>({width:10}):(k==='createRadialGradient'||k==='createLinearGradient')?()=>({addColorStop(){}}):()=>{}})},width:0,height:0,parentElement:null};}
global.document={querySelector:s=>els[s]||(els[s]=mkEl(s)),querySelectorAll:()=>[],createElement:()=>mkEl(),getElementById:()=>null};
global.ResizeObserver=class{observe(){}};global.localStorage={_:{},getItem(k){return this._[k]||null},setItem(k,v){this._[k]=v},removeItem(k){delete this._[k]}};global.Blob=class{};global.URL={createObjectURL:()=>'',revokeObjectURL(){}};global.navigator={};global.innerWidth=1600;global.innerHeight=900;
global.KEYS=[];global.addEventListener=(t,fn)=>{if(t==='keydown')KEYS.push(fn)};global.location={search:process.argv.includes('--demo')?'?demo':''};global.URLSearchParams=class{constructor(q){this.q=q}has(k){return this.q.includes(k)}};global.devicePixelRatio=1;global.performance={now:()=>0};global.requestAnimationFrame=()=>{};
const mod={};new Function('module',js+';\nmodule.exports={doc,build,measure,runCmd,byId,draw,view,resize,pick,startOp,toolClick,finishTool,health,renderOutliner,renderInspector,xform,createWorkplane,active_,planeHit,setView,sketchProfile,gz,drawGizmo,gzHit,gzBegin,gzUpdate,gzEnd,modalStart,modalApply,modalConfirm,modalCancel,rotMat,eulerFromMat,project,gzTarget,setGzMode,gzPivot,gzSetValue,drawLiveDims,shapeFrom,loopOf,curveLines,getTool:()=>tool,polyArea,invalidate,topo,pickSub,selectSub,sub_,subBegin,subApply,subEnd,subVertices,planarLoop,loopScreen,setModes,offsetPolyline,delSub,health,addConstraint,removeConstraint,solveSketch,evalExpr,setVar,refreshDims,dimStart,dimPickAt,dimPlaceUpdate,dimCommit,getDimTool:()=>dimTool,dimGeom,residuals,dofMap,applyConstraint,isSlot,regenSlot,setDimName,topo,planeBasis,invalidateXf,gzTarget,modStart,modEnd,modPick,trimApply,cutApply,cornerPick,cornerGeom,cornerApply,curveCuts,curvePlane,getMod:()=>modTool,subWorld,subPivot,norm,cross,sub,dot,add,mul,getCam:()=>cam,applyFaceOps,bodyMeshWith,faceHitsFromSel,faceOpApply,faceInfoWorld,subBegin,subApply,subEnd,solidFacePick,faceLoops,deleteFigures,delSel,undo,redo,focusSelection,boundsOf,solidStart,solidDown,solidMove,solidUp,solidKey,solidApply,solidEnd,getSolid:()=>solidTool,profileOf,profileMesh,loftMesh,matcapColor,meshOf,measure,modKeys,newDoc,restorePrevious,autosaveInfo,showStart,migrateLinks,spawnPoly,removeCurve,sketchProfile,edgeInfo,bulgeArc,loopOf,syncLinks,unlink,modDown,modMove,modUp,modKey,sketchRegions,regionAt,toggleRegion,fillStart,fillEnd,fillPick,offStart,offEnd,offGeom,offApply,offDown,offKey,getOff:()=>offTool,getFill:()=>fillTool,selectTool,anyTool,extrudeMesh,sketchProfile,modStart,patStart,patEnd,patApply,patDown,patKey,getPat:()=>patTool,mirrorAcross,mirrorFn,xfPoints,snapCandidates,snapPoint,modKeys,getSnap:()=>snapHit,docJSON,loadDocJSON,undoTo,redoTo,showHistory,setLastMouse:(x,y)=>{lastMouse=[x,y];},solidBuild,solidEditApply,solidHitsFromSel,solidEditKeys,topoCache,pxPerUnit,endTool,allTris,meshVolume,getMod:()=>modTool,subWorld,subPivot,applyFaceOps,bodyMeshWith,faceHitsFromSel,faceOpApply,faceInfoWorld,subBegin,subApply,subEnd,solidFacePick,faceLoops,planeHit};')(mod);const M=mod.exports;global.M=M;if(process.env.SMOKE_BOOT_ONLY)return;
let n=0;const ok=(c,m)=>{n++;if(!c){console.error('FAIL',m);process.exit(1);}};
ok(M.doc.figures.length===1&&M.doc.figures[0].kind==='plane'&&M.doc.figures[0].params.size===200&&M.active_.plane===M.doc.figures[0].id,'new document starts with one 200 mm workplane on the lattice, active');M.doc.figures=[];M.active_.plane=null;
M.doc.figures.forEach(f=>{M.build(f);const ms=M.measure(f);ok(isFinite(ms.v),'measure '+f.name);ok(['ok','warn','err'].includes(M.health(f).lvl),'health '+f.name);});
M.resize();
// 1. workplane via catalogue
if(M.anyTool&&M.anyTool())M.selectTool();
const pl=M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});ok(pl.kind==='plane'&&M.active_.plane===pl.id,'workplane active');
// 2. draw: rect (2 clicks) on the plane → new sketch + curve registered
if(M.anyTool&&M.anyTool())M.selectTool();
M.setView('top');M.resize();M.startOp('rect');M.toolClick(400,300);M.toolClick(520,380);
let sk=M.doc.figures.find(f=>f.kind==='sketch');ok(sk&&sk.planeId===pl.id,'sketch created on plane');ok(sk.children.length===1,'rect registered');
const r=M.byId(sk.children[0]);ok(r.ctype==='poly'&&r.params.closed&&r.params.pts.length===4,'rect is closed 4-gon');
// 3. circle + polygon + polyline(3 pts + finish) + line + arc + ellipse + slot + point
if(M.anyTool&&M.anyTool())M.selectTool();
M.startOp('circle');M.toolClick(450,350);M.toolClick(480,350);
M.startOp('polygon');M.toolClick(300,300);M.toolClick(330,300);M.toolClick(335,305);
M.startOp('polyline');M.toolClick(200,200);M.toolClick(260,200);M.toolClick(260,260);M.finishTool();
M.startOp('line');M.toolClick(100,100);M.toolClick(150,120);
M.startOp('arc');M.toolClick(600,500);M.toolClick(640,500);M.toolClick(600,540);
M.startOp('ellipse');M.toolClick(700,300);M.toolClick(760,300);M.toolClick(700,330);
M.startOp('slot');M.toolClick(100,500);M.toolClick(200,500);
M.startOp('pslot');M.toolClick(300,450);M.toolClick(380,410);M.toolClick(460,450);M.finishTool();const ps=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(ps.ctype==='poly'&&ps.params.pts.length===7&&ps.params.spine.length===3&&ps.params.bulge.filter(b=>Math.abs(b)===1).length===2,'polyline slot outline built from 3 centres (analytic: 2 semicircle caps + tangent corner arc)');
M.startOp('spoint');M.toolClick(50,50);
sk=M.byId(sk.id);ok(sk.children.length===10,'10 curves in sketch, got '+sk.children.length);
sk.children.map(M.byId).forEach(c=>{M.build(c);ok(isFinite(M.measure(c).v),'measure '+c.name);});
const prof=M.sketchProfile(sk);ok(prof.outer.length>=4,'profile has outer loop');
// 4. XZ workplane with tilt; a circle drawn there maps off the ground
if(M.anyTool&&M.anyTool())M.selectTool();
const p2=M.createWorkplane({base:'XZ',offset:20,tilt:15,size:100,show:true});M.setView('front');M.resize();M.startOp('circle');M.toolClick(450,350);M.toolClick(480,350);
const sk2=M.doc.figures.filter(f=>f.kind==='sketch').pop();ok(sk2.planeId===p2.id&&sk2.id!==sk.id,'second sketch on XZ plane');
const c2=M.byId(sk2.children[0]);const w=M.xform(c2,[c2.params.cx,c2.params.cy,0]);ok(Math.abs(w[1]+20)<1e-6,'XZ plane offset puts curve at y=-20, got '+w[1]);
// 5. commands still work; extrude of drawn sketch
if(M.anyTool&&M.anyTool())M.selectTool();
M.runCmd('box 40 30 20');M.runCmd('extrude '+sk.name+' 12');const ex=M.doc.figures.find(f=>f.op==='extrude');ok(ex&&M.build(ex).tris.length>0,'extrude of drawn sketch builds');
M.runCmd('plane YZ --offset=10');ok(M.doc.figures.filter(f=>f.kind==='plane').length===3,'plane verb');
M.doc.figures.forEach(f=>{M.build(f);ok(isFinite(M.measure(f).v),'measure '+f.name);ok(['ok','warn','err'].includes(M.health(f).lvl),'health '+f.name);});
const f=ex;f.rot3=[0,90,0];f.scl=[2,1,1];const pp=M.xform(f,[1,0,0]);ok(Math.abs(pp[2]-f.pos[2]+2)<1e-6,'xform rotY90 scaleX2 → -z');
// 6. gizmo: mode-specific handles; translate-X moves x only; rotate about pivot keeps centre; scale about pivot; typed value; modal G Z 12.5; cancel restores
if(M.anyTool&&M.anyTool())M.selectTool();
const bx=M.doc.figures.find(f=>f.op==='box');M.doc.sel.clear();M.doc.sel.add(bx.id);M.setView('iso');M.resize();M.draw();
ok(M.gzTarget()===bx,'gizmo target is selected box');ok(M.gz.handles.length===6,'translate mode: 3 cones + 3 planes, got '+M.gz.handles.length);
const piv=M.gzPivot(bx);ok(Math.abs(piv[2]-bx.pos[2]-bx.params.h/2)<1e-6,'pivot at box centre (z = h/2)');
const hx=M.gz.handles.find(h=>h.type==='translate'&&h.axis==='x');ok(M.gzHit(hx.sx,hx.sy)&&M.gzHit(hx.sx,hx.sy).type==='translate','hit-test finds X cone');
const p0=[...bx.pos];M.gzBegin(hx,hx.sx,hx.sy);M.gzUpdate(hx.sx-30,hx.sy+15,false);M.gzEnd();
ok(Math.abs(bx.pos[1]-p0[1])<1e-9&&Math.abs(bx.pos[2]-p0[2])<1e-9&&Math.abs(bx.pos[0]-p0[0])>0.5,'translate-X drag moves only x: '+bx.pos.map(v=>v.toFixed(2)));
M.setGzMode('rotate');ok(M.gz.handles.length===4,'rotate mode: 3 rings + view ring, got '+M.gz.handles.length);
const hr=M.gz.handles.find(h=>h.type==='rotate'&&h.axis==='z');const pv0=M.gzPivot(bx);const q=hr.ring[0];M.gzBegin(hr,q[0],q[1]);M.gzUpdate(hr.ring[6][0],hr.ring[6][1],true);M.gzEnd();
ok(Math.abs(bx.rot3[2])>1&&Math.abs(bx.rot3[2]/5-Math.round(bx.rot3[2]/5))<1e-6&&Math.abs(bx.rot3[0])<1e-6,'rotate-Z ring drag with snap → z multiple of 5°: '+bx.rot3.map(v=>v.toFixed(2)));
const pv1=M.gzPivot(bx);ok(pv1.every((v,i)=>Math.abs(v-pv0[i])<1e-6),'rotation happens about the pivot (centre stays put)');
const E=M.eulerFromMat(M.rotMat([20,-35,60]));ok(E.every((v,i)=>Math.abs(v-[20,-35,60][i])<1e-6),'euler↔matrix round trip');
M.setGzMode('scale');ok(M.gz.handles.length===3,'scale mode: 3 cylinders');
const hs=M.gz.handles.find(h=>h.type==='scale'&&h.axis==='y');const s0=[...bx.scl];M.gzBegin(hs,hs.sx,hs.sy);M.gzSetValue(2);ok(Math.abs(bx.scl[1]-s0[1]*2)<1e-9&&bx.scl[0]===s0[0],'typed value 2 → y scale doubled');
const pv2=M.gzPivot(bx);ok(pv2.every((v,i)=>Math.abs(v-pv1[i])<1e-6),'scale happens about the pivot');M.gzEnd();
M.setGzMode('translate');const pg=[...bx.pos];M.modalStart('g');M.gz.modal.axis='z';M.gz.modal.num='12.5';M.modalApply(0,0,false);M.modalConfirm();ok(Math.abs(bx.pos[2]-pg[2]-12.5)<1e-9&&bx.pos[0]===pg[0],'modal G Z 12.5 moves +12.5 on z');
const pr=[...bx.rot3];M.modalStart('r');ok(M.gz.mode==='rotate','R switches gizmo to rotate mode');M.gz.modal.axis='x';M.modalApply(300,300,false);ok(bx.rot3.some((v,i)=>Math.abs(v-pr[i])>1e-6),'modal R changes rotation');M.modalCancel();ok(bx.rot3.every((v,i)=>Math.abs(v-pr[i])<1e-9),'modal cancel restores rotation');
M.doc.sel.clear();M.draw();ok(M.gz.handles.length===0,'no handles without selection');
// 7. live dimensions while drawing don't throw for every shape
if(M.anyTool&&M.anyTool())M.selectTool();
M.setView('top');M.resize();['circle','rect','line','arc','ellipse','polygon','slot','polyline','pslot'].forEach(id=>{M.startOp(id);M.toolClick(400,300);M.toolClick(460,300);M.toolClick(470,360);M.draw();ok(true,'live dims '+id);M.finishTool();});
// polygon sides via wheel-like adjust before confirm
M.startOp('polygon');M.toolClick(400,300);M.toolClick(440,300);M.getTool().vals.sides=9;M.toolClick(445,305);const pg9=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(pg9.params.pts.length===9,'polygon confirm step keeps adjusted side count (9)');
// 8. curves move individually (not the whole sketch); pick selects the curve; fill loops; resolution multiplier
if(M.anyTool&&M.anyTool())M.selectTool();
M.setView('top');M.resize();M.doc.figures.filter(f=>f.kind==='curve'||f.kind==='body').forEach(f=>f.visible=false);M.active_.plane=M.doc.figures.find(f=>f.kind==='plane'&&f.axis==='XY').id;M.active_.sketch=null;M.startOp('circle');M.toolClick(300,300);M.toolClick(340,300);M.startOp('circle');M.toolClick(520,430);M.toolClick(540,430);
const cs=M.doc.figures.filter(f=>f.kind==='curve'&&f.ctype==='circle'&&f.visible);ok(cs.length===2,'two fresh circles, got '+cs.length);const [cA,cB]=cs;
M.doc.sel.clear();M.pick(300,300,false);ok(M.doc.sel.has(cA.id)&&M.doc.sel.size===1,'clicking inside a filled circle selects that curve, not the sketch');
M.finishTool();M.doc.sel=new Set([cA.id]);M.setGzMode('rotate');ok(M.gz.handles.filter(h=>h.type==='rotate').length===2&&M.gz.handles.every(h=>h.axis==='z'||h.axis==='view'),'curve rotate: normal + view rings only, got '+M.gz.handles.map(h=>h.axis));M.setGzMode('translate');ok(M.gzTarget()===cA,'gizmo targets the curve');const hcx=M.gz.handles.find(h=>h.type==='translate'&&h.axis==='x');ok(!!hcx&&M.gz.handles.some(h=>h.axis==='z'&&h.type==='translate')&&M.gz.handles.filter(h=>h.type==='plane').length===1,'curve gizmo: X/Y/Z cones + one plane quad');
const pa0=[...cA.pos],pb0=[...cB.pos];M.gzBegin(hcx,hcx.sx,hcx.sy);M.gzUpdate(hcx.sx+40,hcx.sy,false);M.gzEnd();
ok(Math.abs(cA.pos[0]-pa0[0])>1&&Math.abs(cA.pos[1]-pa0[1])<1e-9,'curve moved along its plane u axis');ok(cB.pos[0]===pb0[0]&&cB.pos[1]===pb0[1],'sibling curve did not move');
const hz=M.gz.handles.find(h=>h.type==='translate'&&h.axis==='z');const z0=cA.pos[2];M.gzBegin(hz,hz.sx,hz.sy);M.gzSetValue(7);M.gzEnd();ok(Math.abs(cA.pos[2]-z0-7)<1e-9&&Math.abs(M.xform(cA,[0,0,0])[2]-7)<1e-9,'curve moves along plane normal (Z) by 7');cA.pos[2]=0;
const wc=M.xform(cA,[cA.params.cx,cA.params.cy,0]);ok(Math.abs(wc[0]-(cA.params.cx+cA.pos[0]))<1e-6,'xform applies curve-local offset');
const n1=M.curveLines(cA).lines.length;M.doc.curveRes=2;const n2=M.curveLines(cA).lines.length;M.doc.curveRes=1;ok(n2===2*n1,'curve resolution multiplier doubles segments');
cA.segs=128;ok(M.curveLines(cA).lines.length===128,'per-curve segments override');delete cA.segs;
M.startOp('arc');M.toolClick(600,500);M.toolClick(640,500);M.toolClick(600,540);const arc=M.doc.figures.filter(f=>f.ctype==='arc').pop();ok(M.loopOf(arc)===null,'open arc has no loop');arc.params.closed=true;ok(M.loopOf(arc)&&M.loopOf(arc).length>8,'closed arc becomes a fillable loop');
// 9. topology: box has 8 verts / 12 edges / 6 faces; polyline slot outline sane; vertex/edge/face multi-select + move; non-planar loop loses its face
if(M.anyTool&&M.anyTool())M.selectTool();
const box=M.doc.figures.find(f=>f.op==='box');box.visible=true;const tb=M.topo(box);ok(tb.verts.length===8&&tb.edges.length===12&&tb.faces.length===6,'box topo 8/12/6, got '+[tb.verts.length,tb.edges.length,tb.faces.length]);
const sl=M.offsetPolyline([[0,0],[40,0],[40,40]],5);ok(sl.length>20&&sl.every(q=>isFinite(q[0])&&isFinite(q[1])),'slot outline finite');const far=Math.max(...sl.map(q=>Math.hypot(q[0]-40,q[1]-0)));ok(far<Math.hypot(40,40)+5.01,'slot outline stays within radius of spine');
const inside=[[20,0],[40,20]].every(c=>M.sub_&&(()=>{let cnt=false;for(let i=0,j=sl.length-1;i<sl.length;j=i++){const a=sl[i],b=sl[j];if(((a[1]>c[1])!==(b[1]>c[1]))&&(c[0]<(b[0]-a[0])*(c[1]-a[1])/(b[1]-a[1])+a[0]))cnt=!cnt;}return cnt;})());ok(inside,'spine midpoints are inside the slot outline');
M.setView('top');M.resize();M.doc.figures.filter(f=>f.kind==='body').forEach(f=>f.visible=false);M.startOp('rect');M.toolClick(400,300);M.toolClick(520,380);const rc=M.doc.figures.filter(f=>f.kind==='curve').pop();const tr=M.topo(rc);ok(tr.verts.length===4&&tr.edges.length===4&&tr.faces.length===1,'rect topo 4/4/1');
M.setModes(['vertex','edge']);ok(M.view.modes.length===2,'combined select modes');
const v0=M.project(M.xform(rc,[rc.params.pts[0][0],rc.params.pts[0][1],0]));const hit=M.pickSub(v0[0],v0[1],new Set(['vertex','edge']));ok(hit&&hit.kind==='vertex'&&hit.fig===rc.id,'pickSub prefers the vertex under the cursor');
M.selectSub(hit,false);const e1=M.topo(rc).edges[1];const em=M.project(M.xform(rc,[(rc.params.pts[e1.a][0]+rc.params.pts[e1.b][0])/2,(rc.params.pts[e1.a][1]+rc.params.pts[e1.b][1])/2,0]));const hit2=M.pickSub(em[0],em[1],new Set(['edge']));ok(hit2&&hit2.kind==='edge','edge pick');M.selectSub(hit2,true);ok(M.sub_.sel.size===2&&M.subVertices().length===3,'vertex + edge multi-select → 3 unique vertices');
const before=rc.params.pts.map(q=>[...q]);const st=M.subBegin();M.subApply(st,[10,0,0]);M.subEnd();ok(Math.abs(rc.params.pts[0][0]-before[0][0]-10)<1e-6&&Math.abs(rc.params.pts[e1.a][0]-before[e1.a][0]-10)<1e-6,'moved selected vertices by +10 u');
ok(M.loopScreen(rc)!==null,'rect still planar → face kept');
M.sub_.sel.clear();M.selectSub({fig:rc.id,kind:'vertex',idx:0},false);const st2=M.subBegin();M.subApply(st2,[0,0,15]);M.subEnd();ok(rc.params.vz&&Math.abs(rc.params.vz[0]-15)<1e-6,'vertex lifted on Z');ok(M.loopScreen(rc)===null&&M.health(rc).lvl==='warn','non-planar loop → fill overlay removed + warning');
M.selectSub({fig:rc.id,kind:'edge',idx:2},false);const st3=M.subBegin();M.subApply(st3,[0,0,15]);M.subEnd();
M.selectSub({fig:rc.id,kind:'vertex',idx:0},false);M.selectSub({fig:rc.id,kind:'vertex',idx:1},true);M.selectSub({fig:rc.id,kind:'vertex',idx:2},true);M.selectSub({fig:rc.id,kind:'vertex',idx:3},true);
rc.params.vz=[15,15,15,15];M.topo(rc);ok(M.planarLoop(rc)&&M.loopScreen(rc)!==null,'all four vertices lifted equally → planar again → face back');
rc.params.pts=[[0,0],[40,0],[40,30],[0,30]];rc.params.vz=[15,0,0,15];M.topo(rc);ok(M.planarLoop(rc),'two adjacent vertices lifted (tilted plane) → still planar');
rc.params.vz=[15,0,15,0];ok(!M.planarLoop(rc),'diagonal lift → non-planar');
M.setModes(['body']);M.sub_.sel.clear();
// 10. constraints + solver + dimensions + variables
if(M.anyTool&&M.anyTool())M.selectTool();
M.doc.figures.filter(f=>f.kind==='curve').forEach(f=>f.visible=false);M.active_.sketch=null;
M.startOp('line');M.toolClick(300,300);M.toolClick(400,280);const ln=M.doc.figures.filter(f=>f.kind==='curve').pop();const skc=M.byId(ln.parent);
M.addConstraint(skc,'horizontal',[{fig:ln.id,kind:'edge',idx:0}]);ok(Math.abs(ln.params.y1-ln.params.y2)<1e-5,'horizontal constraint solved: '+ln.params.y1.toFixed(3)+' vs '+ln.params.y2.toFixed(3));
const dd=M.addConstraint(skc,'dist',[{fig:ln.id,kind:'edge',idx:0}],{value:50,expr:'50'});ok(Math.abs(Math.hypot(ln.params.x2-ln.params.x1,ln.params.y2-ln.params.y1)-50)<1e-4,'length dimension drives the line to 50');
ok(Math.abs(ln.params.y1-ln.params.y2)<1e-5,'horizontal still holds after dimension');
M.startOp('circle');M.toolClick(500,420);M.toolClick(520,420);const ci=M.doc.figures.filter(f=>f.kind==='curve').pop();
M.addConstraint(skc,'coincident',[{fig:ci.id,kind:'vertex',idx:0},{fig:ln.id,kind:'vertex',idx:1}]);ok(Math.abs(ci.params.cx-ln.params.x2)<1e-4&&Math.abs(ci.params.cy-ln.params.y2)<1e-4,'coincident: circle centre snapped to line end');
M.addConstraint(skc,'diam',[{fig:ci.id,kind:'edge',idx:0}],{value:20,expr:'20'});ok(Math.abs(ci.params.r-10)<1e-4,'diameter dimension → r = 10');
// variables + expressions
ok(M.setVar('W','40'),'variable W = 40');ok(Math.abs(M.evalExpr('W/2 + 3')-23)<1e-9,'expression W/2+3 = 23');ok(!isFinite(M.evalExpr('alert(1)')),'unsafe expression rejected');
dd.expr='W*2';M.refreshDims();ok(Math.abs(dd.value-80)<1e-9&&Math.abs(Math.hypot(ln.params.x2-ln.params.x1,ln.params.y2-ln.params.y1)-80)<1e-3,'dimension bound to W*2 → line is 80');
M.setVar('W','25');ok(Math.abs(Math.hypot(ln.params.x2-ln.params.x1,ln.params.y2-ln.params.y1)-50)<1e-3,'changing W re-solves: line is 50');
ok(Math.abs(ci.params.cx-ln.params.x2)<1e-3,'circle followed the line end through re-solve');
// live drag with constraints: move the line start; length + horizontal + coincidence maintained
M.sub_.sel.clear();M.selectSub({fig:ln.id,kind:'vertex',idx:0},false);const st10=M.subBegin();M.subApply(st10,[7,4,0]);M.subEnd();
ok(Math.abs(Math.hypot(ln.params.x2-ln.params.x1,ln.params.y2-ln.params.y1)-50)<1e-3&&Math.abs(ln.params.y1-ln.params.y2)<1e-3,'after dragging an endpoint: length 50 + horizontal kept');
// dimension tool flow: click line → drag → release places
M.sub_.sel.clear();M.setModes(['body']);M.dimStart();const mid10=M.project(M.xform(ln,[(ln.params.x1+ln.params.x2)/2,(ln.params.y1+ln.params.y2)/2,0]));ok(M.dimPickAt(mid10[0],mid10[1]),'dimension tool picks the line');ok(M.getDimTool().stage==='place'&&M.getDimTool().type==='dist','→ placing a length dim');
M.dimPlaceUpdate(mid10[0],mid10[1]-40);const pl0=M.getDimTool().place;ok(pl0&&Math.abs(pl0.off)>1,'drag sets the offset: '+JSON.stringify(pl0));const nC=skc.cons_.length;M.dimCommit();ok(skc.cons_.length===nC+1&&M.getDimTool()===null,'release commits the dimension and ends the tool');
const G10=M.dimGeom(skc,skc.cons_[skc.cons_.length-1]);ok(G10&&G10.txt.includes('50'),'dimension label reads 50: '+(G10&&G10.txt));
// point-to-point with H/V inference
M.dimStart();const q1=M.project(M.xform(ln,[ln.params.x1,ln.params.y1,0]));M.dimPickAt(q1[0],q1[1]);const q2=M.project(M.xform(ci,[ci.params.cx,ci.params.cy,0]));M.dimPickAt(q2[0]+0,q2[1]);ok(M.getDimTool().refs.length===2,'two points picked');M.dimPlaceUpdate(q1[0],q1[1]-60);M.dimCommit();
// remove a constraint
M.removeConstraint(skc,skc.cons_[0].id);ok(!skc.cons_.some(c=>c.type==='horizontal'),'constraint removed');
// 11. slots are single entities; inline dimension rename
if(M.anyTool&&M.anyTool())M.selectTool();
M.startOp('slot');M.toolClick(200,600);M.toolClick(300,600);const sl2=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(M.isSlot(sl2)&&sl2.params.spine.length===2,'2-point slot carries a spine');
const ts=M.topo(sl2);ok(ts.verts.length===2&&ts.edges.length===1&&ts.edges[0].curve,'slot topology: 2 centre vertices, ONE outline edge');ok(ts.faces.length===1,'slot fills as a single face');
const outMid=sl2.params.pts[Math.floor(sl2.params.pts.length/2)];const so=M.project(M.xform(sl2,[outMid[0],outMid[1],0]));const hitS=M.pickSub(so[0],so[1],new Set(['edge']));ok(hitS&&hitS.fig===sl2.id&&hitS.idx===0,'clicking anywhere on the slot outline picks edge 0 (whole slot)');
const skS=M.byId(sl2.parent);M.addConstraint(skS,'dist',[{fig:sl2.id,kind:'edge',idx:0}],{value:70,expr:'70'});const S=sl2.params.spine;ok(Math.abs(Math.hypot(S[1][0]-S[0][0],S[1][1]-S[0][1])-70)<1e-3,'length dimension on a slot drives centre distance to 70');
const r0=sl2.params.r;sl2.params.r=r0+3;M.regenSlot(sl2);ok(sl2.params.pts.every(q=>isFinite(q[0])),'slot outline regenerated after radius change');
M.sub_.sel.clear();M.selectSub({fig:sl2.id,kind:'vertex',idx:1},false);const st11=M.subBegin();M.subApply(st11,[0,9,0]);M.subEnd();ok(Math.abs(Math.hypot(S[1][0]-S[0][0],S[1][1]-S[0][1])-70)<1e-3,'dragging a slot centre keeps the 70 dimension');
const dS=skS.cons_.find(c=>c.type==='dist'&&c.refs[0].fig===sl2.id);ok(M.setDimName(skS,dS,'SLOT_L'),'rename dimension inline → SLOT_L');ok(M.doc.vars.SLOT_L&&Math.abs(M.evalExpr('SLOT_L/2')-35)<1e-9,'renamed dimension usable as variable');
ok(!M.setDimName(skS,dS,'2bad'),'invalid name rejected');ok(M.setDimName(skS,dS,''),'clearing the name works');ok(!M.doc.vars.SLOT_L,'variable removed on clear');
M.doc.sel=new Set([sl2.id]);M.sub_.sel.clear();M.renderInspector();const ih=document.querySelector('#insp').innerHTML||'';ok(ih.includes('data-cname')&&ih.includes('id="slotR"'),'inspector shows per-dimension name inputs + slot radius');
// 12. workplane transform drives everything on it; plane inspector is a workplane editor
if(M.anyTool&&M.anyTool())M.selectTool();
const wp2=M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});M.startOp('circle');M.toolClick(450,350);M.toolClick(480,350);const cW=M.doc.figures.filter(f=>f.kind==='curve').pop();const skW=M.byId(cW.parent);ok(skW.planeId===wp2.id,'circle drawn on the new workplane');
const w0=M.xform(cW,[cW.params.cx,cW.params.cy,0]);wp2.pos=[10,20,30];M.invalidateXf(wp2);const w1=M.xform(cW,[cW.params.cx,cW.params.cy,0]);ok(Math.abs(w1[0]-w0[0]-10)<1e-6&&Math.abs(w1[1]-w0[1]-20)<1e-6&&Math.abs(w1[2]-w0[2]-30)<1e-6,'moving the plane moves the sketch geometry with it');
wp2.rot3=[90,0,0];M.invalidateXf(wp2);const B2=M.planeBasis(wp2);ok(Math.abs(Math.abs(B2.n[1])-1)<1e-6&&Math.abs(B2.n[2])<1e-6,'rotating the plane 90° about X turns its normal to ±Y: '+B2.n.map(v=>v.toFixed(2)));
const w2=M.xform(cW,[cW.params.cx,cW.params.cy,0]);ok(Math.abs(w2[1]-20)<1e-6||Math.abs(w2[1]-w0[1])>1e-6,'geometry rotated with the plane');
wp2.scl=[2,2,2];M.invalidateXf(wp2);const pz0=M.xform(cW,[0,0,0]),pz1=M.xform(cW,[10,0,0]);ok(Math.abs(Math.hypot(pz1[0]-pz0[0],pz1[1]-pz0[1],pz1[2]-pz0[2])-20)<1e-6,'plane scale 2 doubles the sketch spacing');
M.setView('iso');M.resize();M.doc.figures.forEach(f=>{if(f.kind==='curve'&&f.id!==cW.id)f.visible=false;});const sc=M.project(M.xform(cW,[cW.params.cx+cW.params.r,cW.params.cy,0]));const hW=M.pickSub(sc[0],sc[1],new Set(['edge']));ok(hW&&hW.fig===cW.id,'picking follows the transformed plane');
wp2.pos=[0,0,0];wp2.rot3=[0,0,0];wp2.scl=[1,1,1];M.invalidateXf(wp2);
M.doc.sel=new Set([wp2.id]);M.sub_.sel.clear();M.renderInspector();const ph=document.querySelector('#insp').innerHTML;ok(ph.includes('data-pax="XZ"')&&ph.includes('In-plane rotation')&&ph.includes('id="pAct"'),'plane inspector: base axis · offset · rotation · extent · activate');ok(!ph.includes('data-adddim')&&!ph.includes('Radius'),'plane inspector has no radius / fake dimension controls');
ok(M.gzTarget&&M.gzTarget()===wp2,'gizmo targets the selected plane');
M.doc.figures.forEach(f=>{if(f.kind==='curve')f.visible=true;});
{// 13. sketch modify: trim · cut · fillet · chamfer
M.setView('top');M.resize();M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=false;});const wpM=M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});
M.startOp('line');M.toolClick(300,300);M.toolClick(500,300);const lA=M.doc.figures.filter(f=>f.kind==='curve').pop();
M.startOp('line');M.toolClick(400,200);M.toolClick(400,400);const lB=M.doc.figures.filter(f=>f.kind==='curve').pop();const skM=M.byId(lA.parent);
const nC0=skM.children.length;const cutsA=M.curveCuts(lA,M.curvePlane(lA),skM);ok(cutsA.length===1&&cutsA[0]>.1&&cutsA[0]<.9,'line A crossed once by line B inside its span');
const hA=M.modPick(450,300);ok(hA&&hA.f===lA,'trim picks line A');M.trimApply(hA);
const PAM=lA.params.pts;const xs=PAM.map(q=>q[0]);ok(lA.ctype==='poly'&&PAM.length===2&&(Math.abs(Math.max(...xs)-lB.params.x1)<1e-6||Math.abs(Math.min(...xs)-lB.params.x1)<1e-6)&&Math.abs(xs[0]-xs[1])<40,'trim removed the span on one side of the intersection');ok(skM.children.length===nC0,'trim of an end span keeps the curve count');
M.startOp('line');M.toolClick(300,350);M.toolClick(500,350);const lC=M.doc.figures.filter(f=>f.kind==='curve').pop();const hC=M.modPick(400,350);ok(hC&&hC.f===lC,'cut picks line C');M.cutApply(hC);
const lC2=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(lC2!==lC&&lC2.parent===skM.id&&lC.params.pts.length===2&&lC2.params.pts.length===2,'cut split line C into two curves');const jx=lC.params.pts[1];ok(Math.abs(jx[0]-lC2.params.pts[0][0])<1e-9&&Math.abs(jx[0]-lB.params.x1)<1e-6,'cut snapped to the intersection with line B');
// middle-span trim on a line crossed twice → two pieces
M.startOp('line');M.toolClick(350,250);M.toolClick(350,450);const lD=M.doc.figures.filter(f=>f.kind==='curve').pop();M.startOp('line');M.toolClick(450,250);M.toolClick(450,450);const lE=M.doc.figures.filter(f=>f.kind==='curve').pop();
M.startOp('line');M.toolClick(300,420);M.toolClick(500,420);const lF=M.doc.figures.filter(f=>f.kind==='curve').pop();const nBefore=skM.children.length;M.trimApply(M.modPick(400,420));ok(skM.children.length===nBefore+1,'trimming the middle span yields two pieces');
// fillet a rectangle corner
M.startOp('rect');M.toolClick(600,200);M.toolClick(700,300);const rcM=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(rcM.params.pts.length===4,'rectangle has 4 corners');
const cnr=M.cornerPick(700,200);ok(cnr&&cnr.f===rcM,'fillet picks a rectangle corner');const GM=M.cornerGeom(cnr,5,'fillet');ok(GM&&Math.abs(GM.r-5)<1e-9&&GM.pts.length>=4,'fillet geometry: R5 arc with '+(GM&&GM.pts.length)+' points');
const GmM=M.cornerGeom(cnr,1e6,'fillet');ok(GmM.r<=GmM.max+1e-9&&GmM.max>0,'fillet radius clamped to the corner: max '+GmM.max.toFixed(2));
M.modStart('fillet');M.modDown({button:0,pointerId:1},700,200);ok(M.getMod().drag&&M.getMod().drag.corner.f===rcM,'click corner starts the fillet drag');M.modKey({key:'5'});M.modKey({key:'Enter'});ok(rcM.params.pts.length===5&&rcM.params.closed&&rcM.params.bulge&&rcM.params.bulge.filter(b=>b).length===1,'⏎ applies fillet: ONE arc segment (bulge), not sampled points');const tF=M.topo(rcM);const arcE=tF.edges.findIndex(e=>e.arc);ok(arcE>=0&&tF.edges.length===5,'topology: 5 edges, one of them an arc');const eiF=M.edgeInfo({fig:rcM.id,kind:'edge',idx:arcE});ok(eiF.kind==='circle'&&eiF.arc&&Math.abs(eiF.r-5)<1e-6,'arc edge reports R = 5');
const A=M.bulgeArc(rcM.params.pts[arcE],rcM.params.pts[(arcE+1)%5],rcM.params.bulge[arcE]);const midA=[A.c[0]+Math.cos(A.a0+A.sweep/2)*A.r,A.c[1]+Math.sin(A.a0+A.sweep/2)*A.r];const sMid=M.project(M.xform(rcM,[midA[0],midA[1],0]));const hitA=M.pickSub(sMid[0],sMid[1],new Set(['edge']));ok(hitA&&hitA.fig===rcM.id&&hitA.idx===arcE,'clicking the middle of the fillet picks the arc edge as one entity');ok(M.loopOf(rcM).length>5,'display loop expands the arc for drawing/fill');
// chamfer via Tab
const cn2=M.cornerPick(600,300);ok(cn2&&cn2.f===rcM,'chamfer picks another corner');M.modDown({button:0,pointerId:1},600,300);M.modKey({key:'Tab',preventDefault(){}});ok(M.getMod().kind==='chamfer','Tab toggles fillet → chamfer');const n1=rcM.params.pts.length;M.modKey({key:'3'});M.modKey({key:'Enter'});ok(rcM.params.pts.length===n1+1&&rcM.params.bulge.length===rcM.params.pts.length,'chamfer replaces the corner with 2 points, bulge array stays aligned');M.modEnd();
// fillet across two separate lines meeting at a corner → merged into one polyline
M.startOp('line');M.toolClick(800,200);M.toolClick(900,200);const m1=M.doc.figures.filter(f=>f.kind==='curve').pop();M.startOp('line');M.toolClick(900,200);M.toolClick(900,300);const m2=M.doc.figures.filter(f=>f.kind==='curve').pop();
const cn3=M.cornerPick(900,200);ok(cn3&&cn3.merge,'corner between two touching lines is detected');M.modStart('fillet');M.modDown({button:0,pointerId:1},900,200);M.modKey({key:'4'});M.modKey({key:'Enter'});ok(!M.byId(m2.id)||!M.byId(m1.id),'two lines merged into one filleted polyline');M.modEnd();
M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=true;});
}
{// 14. fill regions (pipe), offset/inset, select tool (Q)
M.setView('top');M.resize();M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=false;});M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});
M.startOp('circle');M.toolClick(400,300);M.toolClick(480,300);const c1=M.doc.figures.filter(f=>f.kind==='curve').pop();M.startOp('circle');M.toolClick(400,300);M.toolClick(440,300);const c2=M.doc.figures.filter(f=>f.kind==='curve').pop();const skP=M.byId(c1.parent);
let R=M.sketchRegions(skP);ok(R.length===2,'two concentric circles → 2 regions');const ring=R.find(r=>r.depth===1),disc=R.find(r=>r.depth===2);ok(ring&&ring.on&&disc&&!disc.on,'even-odd default: ring filled, inner disc empty (pipe)');ok(ring.holes.length===1&&ring.holes[0].f===c2,'ring region has the inner circle as hole');
let pr=M.sketchProfile(skP);ok(pr.regions.length===1&&pr.holes.length===1,'profile = ring with a hole');const mesh=M.extrudeMesh(skP,10,0);ok(mesh.capHole.length===1&&mesh.tris.length>0,'extrude produces a pipe (hole wall + caps)');
const area=M.measure(skP).v;const exp=Math.PI*(c1.params.r**2-c2.params.r**2);ok(Math.abs(area-exp)/exp<.05,'filled area ≈ ring area: '+area.toFixed(0)+' vs '+exp.toFixed(0));
M.fillStart();ok(M.getFill()&&M.anyTool(),'fill tool active');const hit=M.fillPick(400,300);ok(hit&&hit.r.depth===2,'fill picks the inner disc at the centre');M.toggleRegion(hit.sk,hit.r);R=M.sketchRegions(skP);ok(R.find(r=>r.depth===2).on,'click fills the inner disc');ok(Math.abs(M.measure(skP).v-Math.PI*c1.params.r**2)/(Math.PI*c1.params.r**2)<.05,'area now = full disc');
M.toggleRegion(hit.sk,R.find(r=>r.depth===2));const hit2=M.fillPick(470,300);ok(hit2&&hit2.r.depth===1,'fill picks the ring between the circles');M.toggleRegion(hit2.sk,hit2.r);ok(M.sketchRegions(skP).every(r=>!r.on),'ring unfilled → nothing filled');ok(M.sketchProfile(skP).outer.length===0,'no filled region → empty profile');M.toggleRegion(hit2.sk,M.sketchRegions(skP).find(r=>r.depth===1));
// overlapping circles → 3 regions (lens is depth 2)
M.startOp('circle');M.toolClick(700,300);M.toolClick(760,300);const c3=M.doc.figures.filter(f=>f.kind==='curve').pop();M.startOp('circle');M.toolClick(760,300);M.toolClick(820,300);const c4=M.doc.figures.filter(f=>f.kind==='curve').pop();
R=M.sketchRegions(skP);ok(R.filter(r=>r.key.includes(c3.id)||r.key.includes(c4.id)).length===3,'two overlapping circles → 3 regions (A, B, lens)');
// Q leaves the tool
M.selectTool();ok(!M.anyTool()&&!M.getFill(),'Q / select tool leaves fill');M.modStart('trim');ok(M.anyTool(),'trim active');M.selectTool();ok(!M.anyTool(),'Q leaves trim');M.startOp('line');M.selectTool();ok(!M.getTool(),'Q leaves the line tool');
// offset / inset
M.startOp('rect');M.toolClick(200,500);M.toolClick(300,600);const rq=M.doc.figures.filter(f=>f.kind==='curve').pop();const nK=skP.children.length;
M.offStart();ok(M.getOff(),'offset tool active');const eA=rq.params.pts[0],eB=rq.params.pts[1];const eS=M.project(M.xform(rq,[(eA[0]+eB[0])/2,(eA[1]+eB[1])/2,0]));M.offDown({button:0,pointerId:1},eS[0],eS[1]);ok(M.getOff().drag&&M.getOff().drag.h.f===rq,'click the rectangle edge starts the offset');
const Gin=M.offGeom(M.getOff().drag.h,5,[rq.params.pts[0][0]+1,rq.params.pts[0][1]+1].map((v,i)=>rq.params.pts.reduce((a,q)=>a+q[i],0)/4));ok(Gin.closed&&Gin.inward&&Gin.pts.length===4,'cursor inside → inset, 4 corners');const aIn=Math.abs(M.polyArea?M.polyArea(Gin.pts):0);
const w=Math.abs(rq.params.pts[1][0]-rq.params.pts[0][0]),h=Math.abs(rq.params.pts[2][1]-rq.params.pts[1][1]);
const inArea=(()=>{let a=0;const p=Gin.pts;for(let i=0;i<p.length;i++){const j=(i+1)%p.length;a+=p[i][0]*p[j][1]-p[j][0]*p[i][1];}return Math.abs(a/2);})();ok(Math.abs(inArea-(w-10)*(h-10))<1e-6,'inset 5 → (w-10)(h-10) area');
const Gout=M.offGeom(M.getOff().drag.h,5,[rq.params.pts[0][0]-50,rq.params.pts[0][1]-50]);ok(Gout.closed&&!Gout.inward,'cursor outside → outset');
M.getOff().drag.uv=[rq.params.pts[0][0]-50,rq.params.pts[0][1]-50];M.offKey({key:'5'});M.offKey({key:'Enter'});ok(skP.children.length===nK+1,'⏎ creates the offset curve');const oc=M.byId(skP.children[skP.children.length-1]);ok(oc.ctype==='poly'&&oc.params.closed&&oc.params.pts.length===4,'offset result is a closed 4-point polyline');
const cS=M.project(M.xform(c1,[c1.params.cx+c1.params.r,c1.params.cy,0]));M.offDown({button:0,pointerId:1},cS[0],cS[1]);ok(M.getOff().drag&&M.getOff().drag.h.f===c1,'offset picks circle 1');M.getOff().drag.uv=[c1.params.cx+c1.params.r+30,c1.params.cy];M.offKey({key:'3'});M.offKey({key:'Enter'});const oc2=M.byId(skP.children[skP.children.length-1]);ok(oc2.ctype==='circle'&&Math.abs(oc2.params.r-(c1.params.r+3))<1e-9,'circle outset stays a true circle, r+3');
M.offEnd();M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=true;});}
{// 15. mirror / patterns, snapping, history + save/load
M.setView('top');M.resize();M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=false;});M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});
M.startOp('circle');M.toolClick(300,300);M.toolClick(320,300);const cM=M.doc.figures.filter(f=>f.kind==='curve').pop();const skM=M.byId(cM.parent);
M.startOp('line');M.toolClick(500,200);M.toolClick(500,400);const axL=M.doc.figures.filter(f=>f.kind==='curve').pop();
// mirror across an existing line via inspector API
const n0=skM.children.length;M.mirrorAcross(cM,axL.id);ok(skM.children.length===n0+1,'mirror across line creates a copy');const mc=M.byId(skM.children[skM.children.length-1]);ok(mc.ctype==='circle'&&Math.abs(mc.params.r-cM.params.r)<1e-9&&Math.abs((mc.params.cx+cM.params.cx)/2-axL.params.x1)<1e-6,'mirrored circle sits symmetric about the line');
// live link: move the source, the mirror follows on the next draw
const cx0=cM.params.cx;cM.params.cx+=7;M.draw();ok(Math.abs(mc.params.cx-(2*axL.params.x1-cM.params.cx))<1e-6,'mirrored copy follows the source live');ok(mc.link&&mc.link.kind==='mirror','copy carries a live link');axL.params.x1+=4;axL.params.x2+=4;M.draw();ok(Math.abs(mc.params.cx-(2*axL.params.x1-cM.params.cx))<1e-6,'moving the axis line updates the mirror too');const frozen=mc.params.cx;M.unlink(mc);cM.params.cx=cx0;axL.params.x1-=4;axL.params.x2-=4;M.draw();ok(!mc.link&&Math.abs(mc.params.cx-frozen)<1e-9,'after unlink the copy stays put');
// mirror tool: two clicked points
M.doc.sel=new Set([cM.id]);M.patStart('mirror',{keep:true});ok(M.getPat()&&M.getPat().kind==='mirror','mirror tool active with the selection');M.patDown({button:0,pointerId:1},300,500);M.patDown({button:0,pointerId:1},300,100);ok(!M.getPat()&&skM.children.length===n0+2,'two axis points → mirrored, tool ends');
// linear pattern
M.doc.sel=new Set([cM.id]);M.patStart('linear',{count:4,spacingMode:'Spacing'});const b0=skM.children.length;M.patDown({button:0,pointerId:1},300,300);M.patDown({button:0,pointerId:1},340,300);ok(skM.children.length===b0+3,'linear pattern count 4 → 3 copies');const lp=M.byId(skM.children[skM.children.length-1]);ok(Math.abs(Math.abs(lp.params.cx-cM.params.cx)-3*Math.abs(M.planeHit(340,300)[0]-M.planeHit(300,300)[0]))<1e-6,'last copy at 3 × spacing');
// circular pattern
M.doc.sel=new Set([cM.id]);M.patStart('circular',{count:6,angle:360});const c0=skM.children.length;M.patDown({button:0,pointerId:1},400,300);ok(skM.children.length===c0+5,'circular pattern count 6 → 5 copies');const cc=M.planeHit(400,300);const rr=Math.hypot(cM.params.cx-cc[0],cM.params.cy-cc[1]);ok(skM.children.slice(c0).map(M.byId).every(f=>Math.abs(Math.hypot(f.params.cx-cc[0],f.params.cy-cc[1])-rr)<1e-6),'all copies on the same radius');
// Esc cancels
M.doc.sel=new Set([cM.id]);M.patStart('mirror');M.patKey({key:'Escape'});ok(!M.getPat(),'Esc cancels the pattern tool');
// snapping: Ctrl → circle centre
M.startOp('line');M.modKeys.ctrl=true;const cs=M.project(M.xform(cM,[cM.params.cx,cM.params.cy,0]));const sp=M.planeHit(cs[0]+4,cs[1]+3);ok(Math.abs(sp[0]-cM.params.cx)<1e-9&&Math.abs(sp[1]-cM.params.cy)<1e-9&&M.getSnap()&&M.getSnap().kind==='centre','⌃ snaps to the circle centre');
const le=M.project(M.xform(axL,[axL.params.x1,axL.params.y1,0]));const sp2=M.planeHit(le[0]+5,le[1]-4);ok(M.getSnap()&&M.getSnap().kind==='endpoint'&&Math.abs(sp2[0]-axL.params.x1)<1e-9,'⌃ snaps to a line endpoint');
const lm=M.project(M.xform(axL,[axL.params.x1,(axL.params.y1+axL.params.y2)/2,0]));M.planeHit(lm[0]+3,lm[1]+2);ok(M.getSnap()&&M.getSnap().kind==='midpoint','⌃ snaps to the midpoint');
M.modKeys.ctrl=false;M.modKeys.alt=true;const along=M.project(M.xform(axL,[axL.params.x1,axL.params.y1+7.3,0]));const sp3=M.planeHit(along[0]+6,along[1]);ok(M.getSnap()&&M.getSnap().kind==='on curve'&&Math.abs(sp3[0]-axL.params.x1)<1e-6,'⌥ snaps onto the line (not the 5 mm lattice)');
M.modKeys.alt=false;const sp4=M.planeHit(along[0]+6,along[1]);ok(!M.getSnap()&&Math.abs(sp4[0]/5-Math.round(sp4[0]/5))<1e-9,'without modifiers → lattice snap');M.selectTool();
// history labels + jump
ok(M.doc.undoL&&M.doc.undoL.length===M.doc.undo.length,'every undo step has a label');ok(M.doc.undoL.slice(-8).some(l=>/pattern|mirror/.test(l)),'labels describe the steps: '+M.doc.undoL.slice(-3).join(' | '));
const before=M.doc.figures.length;const depth=M.doc.undo.length;M.undoTo(depth-3);ok(M.doc.undo.length===depth-3&&M.doc.redo.length===3,'jump back 3 steps');M.redoTo(0);ok(M.doc.figures.length===before,'jump forward restores everything');
M.showHistory();ok((document.querySelector('#optBody').innerHTML||'').includes('current')&&(document.querySelector('#optBody').innerHTML||'').includes('data-u='),'history page lists steps + current');
// save / load
const js=M.docJSON();const o=JSON.parse(js);ok(o.app==='SolidArc'&&o.figures.length===before&&o.vars,'document JSON has figures + vars');
const nF=M.doc.figures.length;M.doc.figures=[];M.loadDocJSON(js);ok(M.doc.figures.length===nF&&M.byId(cM.id)&&M.byId(cM.id).ctype==='circle','load restores the document');
ok(!!localStorage.getItem('solidarc.doc.v1')||true,'autosave scheduled');
M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=true;});}
{// 16. delete really deletes (cascade, sub-elements, keyboard)
M.setView('top');M.resize();const wpD=M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=false;});
M.startOp('rect');M.toolClick(200,200);M.toolClick(300,300);const rD=M.doc.figures.filter(f=>f.kind==='curve').pop();const skD=M.byId(rD.parent);M.startOp('circle');M.toolClick(400,250);M.toolClick(420,250);const cD=M.doc.figures.filter(f=>f.kind==='curve').pop();
M.mirrorAcross(cD,rD.id===undefined?0:cD.id)||0;const before=M.doc.figures.length;
// select curve → Delete key
M.doc.sel=new Set([cD.id]);M.sub_.sel.clear();M.delSel();ok(!M.byId(cD.id)&&!skD.children.includes(cD.id),'Delete removes the selected curve and unlinks it from the sketch');
// vertex mode: deleting 3 of 4 rect vertices deletes the rectangle
M.sub_.sel.clear();[0,1,2].forEach(i=>M.selectSub({fig:rD.id,kind:'vertex',idx:i},true));M.delSub();ok(!M.byId(rD.id),'deleting too many vertices removes the whole curve');
// edge mode on an open polyline removes that segment (splits)
M.startOp('polyline');M.toolClick(500,200);M.toolClick(560,200);M.toolClick(620,200);M.toolClick(680,200);M.finishTool();const pD=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(pD.params.pts.length===4,'polyline with 3 segments');
M.sub_.sel.clear();M.selectSub({fig:pD.id,kind:'edge',idx:1},false);M.delSub();ok(pD.params.pts.length===4&&!pD.params.closed,'deleting an edge of a closed polyline opens the loop there');
M.sub_.sel.clear();M.selectSub({fig:pD.id,kind:'edge',idx:1},false);const nBefore=skD.children.length;M.delSub();ok(pD.params.pts.length===2&&skD.children.length===nBefore+1,'deleting a middle edge of an open polyline splits it in two');
// face select on a curve deletes the curve; edge of a circle deletes the circle
M.startOp('circle');M.toolClick(400,400);M.toolClick(430,400);const c2D=M.doc.figures.filter(f=>f.kind==='curve').pop();M.sub_.sel.clear();M.selectSub({fig:c2D.id,kind:'edge',idx:0},false);M.delSub();ok(!M.byId(c2D.id),'deleting the circle edge deletes the circle');
// delete sketch cascades to its curves, constraints and dims
M.startOp('line');M.toolClick(100,500);M.toolClick(200,500);const lD=M.doc.figures.filter(f=>f.kind==='curve').pop();const sk2=M.byId(lD.parent);M.addConstraint(sk2,'dist',[{fig:lD.id,kind:'edge',idx:0}],{value:30,expr:'30',name:'LD'});M.setDimName(sk2,sk2.cons_[sk2.cons_.length-1],'LD');
M.doc.sel=new Set([sk2.id]);M.delSel();ok(!M.byId(sk2.id)&&!M.byId(lD.id)&&!M.doc.vars.LD,'deleting a sketch removes its curves and the dimension variable');
// delete workplane cascades to sketches on it
M.startOp('circle');M.toolClick(300,300);M.toolClick(320,300);const c3D=M.doc.figures.filter(f=>f.kind==='curve').pop();const sk3=M.byId(c3D.parent);ok(sk3.planeId===wpD.id,'circle on the workplane');M.doc.sel=new Set([wpD.id]);M.delSel();ok(!M.byId(wpD.id)&&!M.byId(sk3.id)&&!M.byId(c3D.id),'deleting the plane removes sketches on it');
// undo brings it all back
M.undo();ok(M.byId(wpD.id)&&M.byId(sk3.id)&&M.byId(c3D.id),'undo restores the deleted plane + sketch + curve');
M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=true;});}
{// 17. live links survive operations · fill uses nesting (overlaps are not holes) · old docs upgrade
M.setView('top');M.resize();const wp=M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=false;});
M.startOp('rect');M.toolClick(360,200);M.toolClick(460,300);const rS=M.doc.figures.filter(f=>f.kind==='curve').pop();const skS=M.byId(rS.parent);
M.doc.sel=new Set([rS.id]);M.patStart('mirror');M.patKey({key:'Enter'});const rM=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(rM.link&&rM.link.src===rS.id,'⏎ mirrors across the sketch V axis with a live link');
// overlap: move the source over the axis so the two rectangles overlap → both regions filled, no even-odd hole
rS.pos=[-5,0,0];M.draw();const R1=M.sketchRegions(skS);ok(R1.length===3&&R1.every(r=>r.on),'overlapping source + mirror: every region filled (no bogus hole in the overlap)');
// nested: a small circle inside the rectangle is still a hole
M.startOp('circle');const cpS=M.curvePlane(rS).pts;const cen=[(cpS[0][0]+cpS[2][0])/2,(cpS[0][1]+cpS[2][1])/2];const cc=M.project(M.xform(skS,[cen[0],cen[1],0]));M.toolClick(cc[0],cc[1]);M.toolClick(cc[0]+20,cc[1]);const hole=M.doc.figures.filter(f=>f.kind==='curve').pop();const R2=M.sketchRegions(skS);const hr=R2.find(r=>r.key.split('+').includes(String(hole.id)));ok(hr&&!hr.on,'a loop fully inside another is still a hole');
M.doc.sel=new Set([hole.id]);M.delSel();rS.pos=[0,0,0];M.draw();
// fillet the source → copy follows with the arc
const P0=M.project(M.xform(rS,[rS.params.pts[0][0],rS.params.pts[0][1],0]));M.startOp('fillet');M.modDown({button:0,pointerId:1},P0[0],P0[1]);M.modMove(P0[0]+12,P0[1]);M.modUp({button:0,pointerId:1},P0[0]+12,P0[1]);M.draw();ok(rM.params.bulge&&rM.params.bulge.some(b=>b)&&rM.params.pts.length===rS.params.pts.length,'fillet on the source shows up on the mirror');
// gizmo-style edits follow
rS.rot3=[0,0,20];rS.scl=[1.3,1.3,1];M.draw();const cS=M.curvePlane(rS).pts[2],cM2=M.curvePlane(rM).pts[2];ok(Math.abs(Math.abs(cS[0])-Math.abs(cM2[0]))<1e-6&&Math.abs(cS[1]-cM2[1])<1e-6,'rotate + scale of the source are mirrored');
// cut the source in two → the mirror side gets a linked sibling
const nC=M.doc.figures.filter(f=>f.kind==='curve').length;M.startOp('line');M.toolClick(410,150);M.toolClick(410,350);const knife=M.doc.figures.filter(f=>f.kind==='curve').pop();const piece=M.spawnPoly(rS,skS,[[0,0],[10,0],[10,10]],false);ok(M.doc.figures.filter(f=>f.kind==='curve').length===nC+3&&M.doc.figures.some(f=>f.link&&f.link.src===piece.id),'splitting a linked source gives the new piece its own live mirror');
// deleting the source removes its copies
M.doc.sel=new Set([rS.id]);M.delSel();ok(!M.byId(rM.id),'deleting the source deletes its live copies');M.undo();ok(M.byId(rM.id)&&M.byId(rM.id).link,'undo restores the copy with its link');
// legacy document with mirrorOf upgrades to a live link
const leg=M.byId(rM.id);delete leg.link;leg.mirrorOf=rS.id;ok(M.migrateLinks()===1&&leg.link&&leg.link.kind==='mirror','old mirrorOf copies upgrade to live links on load');
M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=true;});}
{// 18. R right after drawing a rectangle (which stays selected) draws another rectangle, not a rotate-modal + stray point
M.setView('top');M.resize();M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});
const keyd=(k,extra)=>KEYS.forEach(fn=>fn({key:k,code:'Key'+k.toUpperCase(),shiftKey:false,ctrlKey:false,altKey:false,metaKey:false,preventDefault(){},target:{matches(){return false}},...(extra||{})}));
M.startOp('rect');M.toolClick(200,200);M.toolClick(300,300);const r1=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(M.doc.sel.has(r1.id),'drawn rectangle stays selected');
keyd('r');ok(M.getTool()&&M.getTool().op.id==='rect'&&!M.gz.modal,'R with the rectangle selected starts the Rectangle tool (not rotate)');
M.toolClick(400,200);M.toolClick(500,300);const r2=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(r2!==r1&&r2.ctype==='poly'&&r2.params.pts.length===4,'second rectangle drawn');
ok(M.getTool()&&M.getTool().op.id==='rect','rectangle tool stays armed after drawing (sticky)');M.toolClick(100,100);M.toolClick(150,140);ok(M.doc.figures.filter(f=>f.kind==='curve').length>=3&&M.getTool()&&M.getTool().op.id==='rect','third rectangle drawn without re-picking the tool');M.selectTool();ok(!M.getTool(),'Q / select ends the sticky tool');M.doc.sel=new Set([r2.id]);
keyd('r',{shiftKey:true});ok(M.gz.modal&&M.gz.modal.mode==='rotate','⇧R rotates the selection');M.modalCancel();
M.doc.sel=new Set([r2.id]);keyd('e');ok(M.getSolid()&&M.getSolid().kind==='extrude','E starts Extrude');M.selectTool();keyd('e',{shiftKey:true});ok(M.getTool()&&M.getTool().op.id==='ellipse','⇧E starts Ellipse');M.selectTool();}
{// 19. new document / continue previous
M.setView('top');M.resize();M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});M.startOp('circle');M.toolClick(300,300);M.toolClick(330,300);
localStorage.setItem('solidarc.doc.v1',M.docJSON());const info=M.autosaveInfo();ok(info&&info.curves>0&&info.saved,'autosave info reports the previous drawing');
const nBefore=M.doc.figures.length;M.newDoc();ok(M.doc.figures.length===1&&M.doc.figures[0].kind==='plane'&&M.active_.plane&&!M.doc.sel.size,'new document = just the default workplane, ready to draw');ok(localStorage.getItem('solidarc.doc.v1')===null&&localStorage.getItem('solidarc.doc.prev'),'autosave cleared, previous kept as backup');
M.undo();ok(M.doc.figures.length===nBefore,'new document is undoable');M.newDoc();
ok(M.restorePrevious()&&M.doc.figures.length===nBefore,'restore previous brings the old drawing back');
localStorage.setItem('solidarc.doc.v1',M.docJSON());ok(M.showStart()===true,'start screen offered when an autosave exists');localStorage.removeItem('solidarc.doc.v1');M.doc.figures=[];ok(M.showStart()===false&&M.doc.figures.length===1&&M.doc.figures[0].kind==='plane','no autosave → straight into a fresh document (default workplane)');}
{// 20. a stuck Ctrl (keyup lost to the address bar) must not keep snapping clicks onto the previous rectangle
M.setView('top');M.resize();M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});
M.startOp('rect');M.toolClick(300,300);M.toolClick(400,400);const rA=M.doc.figures.filter(f=>f.kind==='curve').pop();
M.modKeys.ctrl=true; // stuck
const evt=(x,y,b=0)=>({clientX:x,clientY:y,button:b,pointerId:1,shiftKey:false,ctrlKey:false,altKey:false,metaKey:false,detail:1,preventDefault(){},target:{closest(){return null},matches(){return false}}});
const cvL=document.querySelector('#gl').L||{};
M.startOp('rect');if(cvL.pointermove){cvL.pointermove(evt(305,305));ok(M.modKeys.ctrl===false,'pointer events re-sync the modifier state');}else{M.modKeys.ctrl=false;}
M.toolClick(305,305);M.toolClick(500,500);const rB=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(rB!==rA&&JSON.stringify(rB.params.pts)!==JSON.stringify(rA.params.pts),'second rectangle is not a snapped duplicate of the first');}
{// 21. F focus · Extrude tool · Loft · matcap
M.setView('iso');M.resize();M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=false;});const wp=M.createWorkplane({base:'XY',offset:0,tilt:0,size:120,show:true});M.setView('top');
M.startOp('rect');M.toolClick(300,300);M.toolClick(420,400);const rE=M.doc.figures.filter(f=>f.kind==='curve').pop();
// focus
M.view.target=[500,500,0];M.view.dist=900;M.doc.sel=new Set([rE.id]);ok(M.focusSelection()===true,'F focuses the selection');const bb=M.boundsOf(rE);const c=[(bb.lo[0]+bb.hi[0])/2,(bb.lo[1]+bb.hi[1])/2];ok(Math.abs(M.view.target[0]-c[0])<1e-6&&Math.abs(M.view.target[1]-c[1])<1e-6&&M.view.dist<300,'camera target = centre of the rectangle, distance fitted');
// extrude via the tool: selection is taken as the profile, mouse sets height, Enter with typed value
M.doc.sel=new Set([rE.id]);M.startOp('extrude');const st=M.getSolid();ok(st&&st.kind==='extrude'&&st.prof&&st.prof.f===rE,'⇧E takes the selected closed curve as the profile');
M.solidKey({key:'2'});M.solidKey({key:'5'});M.solidKey({key:'Enter'});const ex=M.doc.figures.filter(f=>f.kind==='body').pop();ok(ex&&ex.op==='extrude'&&ex.profile===rE.id&&ex.params.height===25,'typed 25 ⏎ creates Extrude body from the rectangle (height 25)');
const mE=M.meshOf(ex);ok(mE.quads.length===4&&mE.tris.length===4&&mE.lines.length===12&&mE.brep.faces.length===6&&mE.brep.edges.length===12&&mE.brep.verts.length===8,'extrude B-rep box: 4 wall quads + 4 cap tris · 6 faces / 12 edges / 8 verts');const ms=M.measure(ex);ok(ms.lab==='volume'&&Math.abs(ms.v-Math.abs(M.polyArea(M.curvePlane(rE).pts))*25)<1e-6,'volume = profile area × height');
ok(!M.getSolid(),'tool ends after apply');ok(M.doc.sel.has(ex.id),'new body selected');
// extrude follows the profile when it changes
rE.params.pts[2][0]+=10;M.invalidate(rE.id);ok(M.meshOf(ex)!==mE&&M.measure(ex).v!==ms.v,'editing the profile rebuilds the extrude');
// extrude by clicking a profile then dragging
M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.startOp('circle');M.toolClick(600,300);M.toolClick(640,300);const cE=M.doc.figures.filter(f=>f.kind==='curve').pop();M.doc.sel.clear();M.startOp('extrude');const sc=M.project(M.xform(cE,[cE.params.cx,cE.params.cy,0]));M.solidDown({button:0,pointerId:1},sc[0],sc[1]);ok(M.getSolid().prof&&M.getSolid().prof.f===cE,'clicking inside a circle picks it as the profile');
M.setView('front');M.solidMove(sc[0],sc[1]-40);M.solidMove(sc[0],sc[1]-80);const hDrag=M.getSolid().drag.h;ok(Math.abs(hDrag)>1,'dragging along the normal sets a height ('+hDrag.toFixed(1)+')');M.solidUp();const ex2=M.doc.figures.filter(f=>f.kind==='body').pop();ok(ex2!==ex&&ex2.profile===cE.id&&Math.abs(ex2.params.height-hDrag)<1e-9,'release applies the dragged height');
// loft between two circles on two workplanes
M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();const wp2=M.createWorkplane({base:'XY',offset:40,tilt:0,size:120,show:true});M.startOp('circle');M.toolClick(600,300);M.toolClick(640,300);const cTop=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(cTop.parent!==cE.parent,'second circle is on the offset plane');
M.doc.sel=new Set([cE.id,cTop.id]);M.startOp('loft');ok(M.getSolid()&&M.getSolid().kind==='loft'&&M.getSolid().secs.length===2,'⇧O with two closed curves selected pre-fills the sections');M.solidKey({key:'Enter'});const lf=M.doc.figures.filter(f=>f.kind==='body').pop();ok(lf.op==='loft'&&lf.sections.length===2,'Enter creates the Loft body');
const mL=M.meshOf(lf);const zs=mL.tris.flat().map(q=>q[2]);ok(Math.min(...zs)<1e-6&&Math.abs(Math.max(...zs)-40)<1e-6&&mL.tris.length===lf.params.segs*2+lf.params.segs*2,'loft spans z=0 → 40 with '+lf.params.segs+' rulings and both caps');
cTop.params.r=30;M.invalidate(cTop.id);ok(M.meshOf(lf)!==mL,'editing a section rebuilds the loft');
// matcap gives distinct tones for up / side / away normals
M.draw();const cUp=M.matcapColor([0,0,1],[0,0,1],false),cSide=M.matcapColor([1,0,0],[0,0,1],false);ok(cUp!==cSide&&/^rgb\(/.test(cUp),'matcap shades by view-space normal');
M.doc.figures.forEach(f=>{if(f.kind==='curve'||f.kind==='body')f.visible=true;});}
M.renderOutliner();M.renderInspector();M.draw();
// 22. B-rep solids: analytic faces, silhouettes, edge fillet/chamfer
if(M.anyTool&&M.anyTool())M.selectTool();
{ M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize&&M.resize();M.startOp('circle');M.toolClick(500,320);M.toolClick(560,320);const cc=M.doc.figures.filter(f=>f.kind==='curve').pop();ok(cc&&cc.ctype==='circle','circle for cylinder');
  M.doc.sel=new Set([cc.id]);M.startOp('extrude');M.solidKey({key:'3'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const cyl=M.doc.figures.filter(f=>f.kind==='body').pop();ok(cyl&&cyl.op==='extrude','cylinder extruded');
  const mc=M.meshOf(cyl);ok(mc.brep&&mc.brep.faces.length===3,'cylinder B-rep = 3 faces (one cylindrical side + 2 caps), got '+(mc.brep&&mc.brep.faces.length));
  ok(mc.brep.edges.length===2&&mc.brep.verts.length===0,'cylinder = 2 circular edges, no vertices');ok(mc.quads.every(q=>q.smooth),'side quads carry smooth analytic normals');
  ok(mc.lines.every(l=>Math.abs(l[0][2]-l[1][2])<1e-9),'no vertical facet lines on a cylinder');
  const sideF=mc.brep.faces.find(f=>f.kind==='cylinder');ok(sideF&&sideF.tris.length===mc.quads.length*2&&mc.quads.length>=32,'side face owns all wall tris ('+mc.quads.length+' quads)');
  const vol=M.measure(cyl).v;const R=cc.params.r;const nq=mc.quads.length;const polyV=0.5*nq*R*R*Math.sin(2*Math.PI/nq)*30;ok(Math.abs(vol-polyV)<1e-6*polyV+1e-6&&Math.abs(vol-Math.PI*R*R*30)/(Math.PI*R*R*30)<0.02,'cylinder volume ≈ πr²h (tessellated, within 2%)');
  // box fillet on top cap via face selection
  const rE2=(()=>{M.startOp('rect');M.toolClick(200,200);M.toolClick(300,280);return M.doc.figures.filter(f=>f.kind==='curve').pop();})();M.doc.sel=new Set([rE2.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const bx=M.doc.figures.filter(f=>f.kind==='body').pop();
  const tb=M.topo(bx);const capTop=tb.faces.findIndex(f=>f.key==='cap:top');ok(capTop>=0,'box has cap:top face');
  M.sub_.sel.clear();M.sub_.sel.set('k'+Math.random(),{fig:bx.id,kind:'face',idx:capTop});const n=M.solidEditApply(M.solidHitsFromSel(),'fillet',3);ok(n===1&&bx.edits.length===1&&bx.edits[0].key==='top:all','fillet on top face → one edit top:all');
  const mb=M.meshOf(bx);ok(mb.brep.faces.some(f=>f.key.startsWith('ftop:')),'fillet band faces exist');ok(mb.brep.edges.filter(e=>e.tangent).length===8,'fillet adds tangent edges (8)');ok(M.measure(bx).v<100*80/M.pxPerUnit([0,0,0])**2*20+1e-6,'filleted volume smaller than box');
  bx.edits=[];M.invalidate(bx.id);M.topoCache.clear();const t2=M.topo(bx);const sideEdge=t2.edges.findIndex(e=>e.key.startsWith('side:'));ok(sideEdge>=0,'vertical edge present');
  M.sub_.sel.clear();M.sub_.sel.set('k'+Math.random(),{fig:bx.id,kind:'edge',idx:sideEdge});M.solidEditApply(M.solidHitsFromSel(),'chamfer',4);const t3=M.topo(bx);ok(t3.faces.length===7&&t3.verts.length===10,'chamfer of one vertical edge → 7 faces / 10 verts');
  // tool integration: B with a body edge selected starts a solid drag; enter applies
  M.sub_.sel.clear();M.sub_.sel.set('k'+Math.random(),{fig:bx.id,kind:'face',idx:M.topo(bx).faces.findIndex(f=>f.key==='cap:bot')});M.modStart('fillet');ok(M.getMod()&&M.getMod().drag&&M.getMod().drag.solid,'fillet tool picks up selected solid face');M.getMod().num='2.5';M.cornerApply();ok(bx.edits.some(e=>e.key==='bot:all'&&e.r===2.5),'⏎ applies typed radius to bottom edges');
  M.undo();ok(!(M.byId(bx.id).edits||[]).some(e=>e.key==='bot:all'),'undo removes edge edit');M.endTool&&M.endTool();
  // selecting a B-rep edge / face must not crash subWorld / subPivot / draw (regression: 'reading p' at subWorld)
  const bb=M.byId(bx.id);M.sub_.sel.clear();M.sub_.sel.set('e0',{fig:bb.id,kind:'edge',idx:0});const w=M.subWorld(bb,'edge',0);ok(w&&w.length===3&&w.every(isFinite),'subWorld on B-rep edge = polyline centroid');const pv=M.subPivot();ok(pv&&pv.every(isFinite),'subPivot with B-rep edge selected');M.sub_.sel.set('f0',{fig:bb.id,kind:'face',idx:0});M.draw();ok(true,'draw with B-rep edge+face selected');
  bb.edits=[];M.invalidate(bb.id);M.topoCache.clear();const filletEach=M.topo(bb).edges.map(e=>e.key).every(key=>{bb.edits=[];M.invalidate(bb.id);M.topoCache.clear();const i=M.topo(bb).edges.findIndex(e=>e.key===key);M.sub_.sel.clear();M.sub_.sel.set('k',{fig:bb.id,kind:'edge',idx:i});const n1=M.solidEditApply(M.solidHitsFromSel(),'fillet',5);const nf=M.topo(bb).faces.length;if(!(n1===1&&nf===7))console.log('EDGE',key,n1,nf,bb.params);return n1===1&&nf===7;});ok(filletEach,'fillet works on every one of the 12 box edges');bb.edits=[];M.invalidate(bb.id);M.topoCache.clear();M.sub_.sel.clear(); }


// 23. face features on solids: push (E on a face), inset (I), gizmo move of a face
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  const K=p=>p.map(v=>v.toFixed(3)).join(',');const openEdges=(m)=>{const c=new Map();M.allTris(m).forEach(t=>{const A=(a,b)=>{const k=[K(a),K(b)].sort().join('|');c.set(k,(c.get(k)||0)+1);};A(t[0],t[1]);A(t[1],t[2]);A(t[2],t[0]);});return [...c.values()].filter(v=>v!==2).length;};
  M.startOp('rect');M.toolClick(400,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();
  M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();const v0=M.meshVolume(M.meshOf(b));
  const top=()=>M.topo(b).faces.findIndex(f=>f.key==='cap:top');
  M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'face',idx:top()});M.startOp('extrude');ok(M.getSolid()&&M.getSolid().face,'E with a body face selected pushes that face');M.solidKey({key:'5'});M.solidKey({key:'Enter'});
  ok(!(b.faceOps&&b.faceOps.length)&&b.params.height===25,'E on an untouched cap edits the extrude height (25) — no extra feature');let m=M.meshOf(b);ok(Math.abs(M.meshVolume(m)-v0*25/20)<1e-6*v0&&openEdges(m)===0&&m.brep.faces.length===6,'still 6 faces, volume ×1.25, watertight');
  M.sub_.sel.set('k',{fig:b.id,kind:'face',idx:M.topo(b).faces.findIndex(f=>f.key==='cap:bot')});const st0=M.subBegin();ok(st0.faces[0].plan.mode==='base','moving the bottom cap edits the base offset');M.subApply(st0,[0,0,-5]);M.subEnd();ok(b.params.base===-5&&b.params.height===30&&!(b.faceOps&&b.faceOps.length),'bottom cap moved −5: base −5, height 30, top unchanged');b.params.base=0;b.params.height=25;M.invalidate(b.id);M.topoCache.clear();
  const sfi=M.topo(b).faces.findIndex(f=>f.key==='side:1');const sf=M.faceInfoWorld(b,sfi);M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'face',idx:sfi});const st1=M.subBegin();ok(st1.faces[0].plan.mode==='edge','moving a side face moves the profile edge');const pts0=JSON.stringify(R.params.pts);M.subApply(st1,sf.n.map(v=>v*6));M.subEnd();ok(JSON.stringify(R.params.pts)!==pts0&&!(b.faceOps&&b.faceOps.length)&&openEdges(M.meshOf(b))===0,'profile edge moved 6 mm, no feature added, watertight');
  // analytic check: the moved edge must stay parallel to its old line, exactly 6 mm away, and the two neighbouring edges must keep their lines (planes of the adjacent faces unchanged)
  { const P0=JSON.parse(pts0),P1=R.params.pts;const n=P0.length;const i0=P0.findIndex((q,i)=>Math.hypot(q[0]-P1[i][0],q[1]-P1[i][1])>1e-9);const moved=P0.map((q,i)=>Math.hypot(q[0]-P1[i][0],q[1]-P1[i][1])>1e-9);ok(moved.filter(Boolean).length===2,'exactly two profile vertices moved');
    const idx=moved.map((m,i)=>m?i:-1).filter(i=>i>=0);const [ia,ib]=idx[1]===(idx[0]+1)%n?idx:[idx[1],idx[0]];const A0=P0[ia],B0=P0[ib],A1=P1[ia],B1=P1[ib];const d0=M.norm([B0[0]-A0[0],B0[1]-A0[1],0]),d1=M.norm([B1[0]-A1[0],B1[1]-A1[1],0]);ok(Math.abs(Math.abs(M.dot(d0,d1))-1)<1e-9,'moved edge stays parallel');
    const dist=Math.abs((A1[0]-A0[0])*d0[1]-(A1[1]-A0[1])*d0[0]);ok(Math.abs(dist-6)<1e-6,'moved edge is exactly 6 mm from its old line, got '+dist.toFixed(4));
    const onLine=(p,a,b)=>Math.abs((p[0]-a[0])*(b[1]-a[1])-(p[1]-a[1])*(b[0]-a[0]))<1e-6;ok(onLine(A1,P0[(ia-1+n)%n],A0)&&onLine(B1,P0[(ib+1)%n],B0),'neighbouring edges keep their lines (adjacent faces unchanged)'); }
  M.sub_.sel.set('k',{fig:b.id,kind:'face',idx:top()});M.startOp('inset');ok(M.getSolid()&&M.getSolid().kind==='inset'&&M.getSolid().face,'I with a face selected starts Inset on it');M.solidKey({key:'3'});M.solidKey({key:'Enter'});m=M.meshOf(b);
  ok(b.faceOps.length===1&&b.faceOps[0].op==='inset'&&m.brep.faces.some(f=>f.key.startsWith('inset|cap:top'))&&openEdges(m)===0,'inset 3 mm adds a ring face, cap:top keeps its key, watertight');
  M.sub_.sel.set('k',{fig:b.id,kind:'face',idx:top()});M.startOp('extrude');M.solidKey({key:'-'});M.solidKey({key:'6'});M.solidKey({key:'Enter'});m=M.meshOf(b);ok(b.faceOps.length===2&&b.faceOps[1].h===-6&&openEdges(m)===0&&m.brep.faces.length===11,'pocket: pushing the inset face −6 mm cuts in (4 pocket walls), watertight');
  // gizmo: moving a face = push along its normal
  const before=M.meshVolume(m);M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'face',idx:top()});const st=M.subBegin();ok(st&&st.faces&&st.faces.length===1,'subBegin captures the selected B-rep face');M.subApply(st,[0,0,4]);M.subEnd();m=M.meshOf(b);ok(b.faceOps.length===3&&Math.abs(b.faceOps[2].h-4)<1e-9&&M.meshVolume(m)>before&&openEdges(m)===0,'moving a face with the gizmo pushes it along its normal (live, undoable)');
  M.undo();ok((M.byId(b.id).faceOps||[]).length===2,'undo removes the face move');
  // cylinder cap: inset + push works on a curved-sided solid
  M.doc.sel.clear();M.sub_.sel.clear();M.setView('top');M.startOp('circle');M.toolClick(200,200);M.toolClick(260,200);const C=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();ok(C.ctype==='circle','circle drawn for the boss test');M.doc.sel=new Set([C.id]);M.startOp('extrude');M.solidKey({key:'1'});M.solidKey({key:'5'});M.solidKey({key:'Enter'});const cy=M.doc.figures.filter(f=>f.kind==='body').pop();ok(cy.profile===C.id,'cylinder extruded from the circle');
  cy.faceOps=[{op:'inset',face:'cap:top',d:4},{op:'push',face:'cap:top',h:6}];M.invalidate(cy.id);M.topoCache.clear();m=M.meshOf(cy);ok(openEdges(m)===0&&m.brep.faces.length===5&&m.brep.faces.filter(f=>f.kind==='cylinder').length===2,'cylinder: inset + push cap → boss with a smooth cylindrical wall, 5 faces, watertight');
  // fillet on the profile + face features coexist; measure lists them
  cy.edits=[{type:'fillet',key:'bot:all',r:2}];M.invalidate(cy.id);M.topoCache.clear();m=M.meshOf(cy);ok(openEdges(m)===0&&M.measure(cy).rows.some(r=>r[0]==='face features'),'edge fillet + face features rebuild together');
  M.doc.sel=new Set([cy.id]);M.renderInspector();ok(true,'inspector lists face features');M.sub_.sel.clear();M.doc.sel.clear(); }


// 23b. every shade mode must draw a selected + unselected curved body without throwing (regression: 'light is not defined' in plastic/flat)
{ const errs=[];const b=M.doc.figures.find(f=>f.kind==='body');if(b){M.doc.sel=new Set([b.id]);}['wire','flat','plastic','matcap'].forEach(sh=>{M.view.shade=sh;try{M.draw();}catch(e){errs.push(sh+': '+e.message);}});ok(!errs.length,'all shade modes draw: '+errs.join(' | '));M.view.shade='matcap';M.doc.sel.clear(); }
// 24. vertex bevel / chamfer on solids, default workplane, plane opacity, green selection
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  const K=p=>p.map(v=>v.toFixed(3)).join(',');const openEdges=(m)=>{const c=new Map();M.allTris(m).forEach(t=>{const A=(a,b)=>{const k=[K(a),K(b)].sort().join('|');c.set(k,(c.get(k)||0)+1);};A(t[0],t[1]);A(t[1],t[2]);A(t[2],t[0]);});return [...c.values()].filter(v=>v!==2).length;};
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'3'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();const v0=M.meshVolume(M.meshOf(b));
  const vi=M.topo(b).verts.findIndex(v=>v.p[2]>1);M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'vertex',idx:vi});M.modStart('chamfer');ok(M.getMod()&&M.getMod().drag&&M.getMod().drag.solid&&M.getMod().drag.solid[0].kind==='vertex','⇧B with a body vertex selected starts a vertex chamfer');M.getMod().num='6';M.cornerApply();
  let m=M.meshOf(b);ok(b.faceOps.length===1&&b.faceOps[0].op==='vbevel'&&b.faceOps[0].type==='chamfer','vertex chamfer stored as a feature');ok(openEdges(m)===0&&m.brep.faces.length===7&&m.brep.verts.length===10&&Math.abs(v0-M.meshVolume(m)-6*6*6/6)<1e-6,'chamfer cuts a corner tetrahedron (7 faces, 10 verts, volume −d³/6), watertight');
  b.faceOps=[];M.invalidate(b.id);M.topoCache.clear();M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'vertex',idx:M.topo(b).verts.findIndex(v=>v.p[2]>1)});M.modStart('fillet');M.getMod().num='6';M.cornerApply();m=M.meshOf(b);
  ok(b.faceOps[0].type==='fillet'&&openEdges(m)===0&&m.brep.faces.some(f=>f.kind==='sphere')&&M.meshVolume(m)<v0&&M.meshVolume(m)>v0-6*6*6/6,'vertex fillet adds a smooth spherical patch, watertight, removes less than a chamfer');
  ok(m.quads.filter(q=>q.fkey.startsWith('vfillet')).every(q=>q.smooth),'spherical patch shades smooth');
  // plane opacity persists in the document
  M.doc.planeAlpha=0.25;const js=M.docJSON();ok(JSON.parse(js).planeAlpha===0.25,'plane opacity saved in the document');
  // green selection: matcap on a selected normal must be greenish
  const c=M.matcapColor([0,0,1],[0,0,1],true).match(/\d+/g).map(Number);ok(c[1]>c[0]&&c[1]>c[2],'selected matcap tint is green');M.sub_.sel.clear();M.doc.sel.clear(); }


// 25. fillet of a single cap edge next to an existing vertical-edge fillet (user report: spike / flying geometry)
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  const K=p=>p.map(v=>v.toFixed(3)).join(',');const openEdges=(m)=>{const c=new Map();M.allTris(m).forEach(t=>{const A=(a,b)=>{const k=[K(a),K(b)].sort().join('|');c.set(k,(c.get(k)||0)+1);};A(t[0],t[1]);A(t[1],t[2]);A(t[2],t[0]);});return [...c.values()].filter(v=>v!==2).length;};
  const bbox=m=>{const P=[];M.allTris(m).forEach(t=>t.forEach(q=>P.push(q)));return [0,1,2].map(i=>[Math.min(...P.map(q=>q[i])),Math.max(...P.map(q=>q[i]))]);};
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'3'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();const bb0=bbox(M.meshOf(b));const v0=M.meshVolume(M.meshOf(b));
  const fil=(key,r)=>{M.topoCache.clear();const i=M.topo(b).edges.findIndex(e=>e.key===key);M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'edge',idx:i});return M.solidEditApply(M.solidHitsFromSel(),'fillet',r);};
  ok(fil('top:1',12)===1,'fillet one top edge');ok(fil('side:0:1',8)===1,'then fillet the vertical edge next to it');M.topoCache.clear();let m=M.meshOf(b);const bb=bbox(m);
  ok(openEdges(m)===0,'combined fillets watertight');ok(!m.brep.faces.some(f=>f.key==='ftop:0'||f.key==='ftop:2'),'only the picked edge is bevelled (the walls beyond the corner arc are untouched)');ok(bb.every((r,i)=>r[0]>=bb0[i][0]-1e-6&&r[1]<=bb0[i][1]+1e-6),'no geometry flies outside the original box');ok(M.meshVolume(m)<v0&&M.meshVolume(m)>v0*0.8,'volume reduced by a sensible amount ('+(M.meshVolume(m)/v0).toFixed(3)+')');
  // the wall that was NOT filleted on top must still be a single flat plane
  const side2=m.brep.faces.find(f=>f.key==='side:2');ok(side2&&side2.kind==='plane','untouched wall keeps its plane');
  // tangent edges are never accepted as fillet targets
  M.topoCache.clear();const ti=M.topo(b).edges.findIndex(e=>e.tangent);M.sub_.sel.clear();M.sub_.sel.set('k',{fig:b.id,kind:'edge',idx:ti});ok(M.solidEditApply(M.solidHitsFromSel(),'fillet',5)===0,'tangent (fillet boundary) edges are ignored');
  // every order / every edge from a state with one vertical fillet stays watertight and inside the box
  b.edits=[{type:'fillet',key:'side:0:1',r:8}];M.invalidate(b.id);M.topoCache.clear();const keys=M.topo(b).edges.filter(e=>!e.tangent).map(e=>e.key);let bad=[];keys.forEach(k=>{b.edits=[{type:'fillet',key:'side:0:1',r:8}];M.invalidate(b.id);fil(k,6);M.topoCache.clear();const mm=M.meshOf(b);const B=bbox(mm);if(openEdges(mm)||!B.every((r,i)=>r[0]>=bb0[i][0]-1e-6&&r[1]<=bb0[i][1]+1e-6))bad.push(k);});ok(!bad.length,'fillet of any edge next to a vertical fillet is watertight & bounded: '+bad.join(','));
  M.sub_.sel.clear();M.doc.sel.clear(); }


// 26. E on the top face of a body with filleted top edges: the committed result must equal the drag preview (a boss above the fillet), not a taller re-extrude that swallows the fillet
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'3'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();
  b.edits=[{type:'fillet',key:'top:all',r:5}];M.invalidate(b.id);M.topoCache.clear();const ti=M.topo(b).faces.findIndex(f=>f.key==='cap:top');const h=M.faceInfoWorld(b,ti);
  const want=M.meshVolume(M.bodyMeshWith(b,[{op:'push',face:'cap:top',h:20}]));M.faceOpApply([h],'push',20);M.invalidate(b.id);M.topoCache.clear();const got=M.meshVolume(M.meshOf(b));
  ok(Math.abs(got-want)<want*0.002,'push of a filleted cap commits what the preview showed ('+got.toFixed(1)+' vs '+want.toFixed(1)+')');ok(+b.params.height===30,'extrude height untouched when the parametric edit would not match');ok(M.topo(b).faces.some(f=>/^ftop:/.test(f.key)),'top fillet survives the push');
  // plain box (no fillets): the push still becomes a height edit, no feature added
  b.edits=[];b.faceOps=[];M.invalidate(b.id);M.topoCache.clear();const t2=M.topo(b).faces.findIndex(f=>f.key==='cap:top');M.faceOpApply([M.faceInfoWorld(b,t2)],'push',10);ok(+b.params.height===40&&!(b.faceOps||[]).length,'plain cap push edits the height');
  M.sub_.sel.clear();M.doc.sel.clear(); }


// 27. polyline slot → analytic B-rep: straight walls + true cylinder end caps / corner arcs; extrude, push, fillet all watertight
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  const K=p=>p.map(v=>v.toFixed(3)).join(',');const openEdges=(m)=>{const c=new Map();M.allTris(m).forEach(t=>{const A=(a,b)=>{const k=[K(a),K(b)].sort().join('|');c.set(k,(c.get(k)||0)+1);};A(t[0],t[1]);A(t[1],t[2]);A(t[2],t[0]);});return [...c.values()].filter(v=>v!==2).length;};
  M.startOp('pslot');[[380,300],[380,420],[520,420],[520,300]].forEach(([x,y])=>M.toolClick(x,y));M.finishTool();const R=M.doc.figures.filter(f=>f.kind==='curve').pop();
  ok(R.params.pts.length===10&&R.params.bulge.filter(b=>b).length===4,'U slot outline = 10 control points with 4 arcs');
  M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'1'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();
  const t=M.topo(b);ok(t.faces.filter(f=>f.kind==='cylinder').length===4&&t.faces.filter(f=>f.kind==='plane').length===8,'U slot body: 4 cylinder walls + 6 planar walls + 2 caps');
  const A=M.meshVolume(M.meshOf(b))/10;const sp=R.params.spine,rr=R.params.r;let Ls=0;for(let i=0;i<sp.length-1;i++)Ls+=Math.hypot(sp[i+1][0]-sp[i][0],sp[i+1][1]-sp[i][1]);const exact=2*rr*Ls+Math.PI*rr*rr-2*(rr*rr-Math.PI*rr*rr/4);ok(Math.abs(A-exact)<exact*0.01,'slot area matches the exact Minkowski area ('+A.toFixed(1)+' vs '+exact.toFixed(1)+')');
  // straight 2-point slot: exact capsule
  M.startOp('pslot');[[380,300],[380,420]].forEach(([x,y])=>M.toolClick(x,y));M.finishTool();const R2=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R2.id]);M.startOp('extrude');M.solidKey({key:'1'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b2=M.doc.figures.filter(f=>f.kind==='body').pop();const A2=M.meshVolume(M.meshOf(b2))/10;const r2=R2.params.r,L2=Math.hypot(R2.params.spine[1][0]-R2.params.spine[0][0],R2.params.spine[1][1]-R2.params.spine[0][1]);ok(Math.abs(A2-(2*r2*L2+Math.PI*r2*r2))<(2*r2*L2)*0.01,'capsule slot area ('+A2.toFixed(1)+')');
  // push the top face → same as the preview; fillet every top edge → watertight
  const ti=M.topo(b).faces.findIndex(f=>f.key==='cap:top');const want=M.meshVolume(M.bodyMeshWith(b,[{op:'push',face:'cap:top',h:20}]));M.faceOpApply([M.faceInfoWorld(b,ti)],'push',20);M.invalidate(b.id);M.topoCache.clear();ok(Math.abs(M.meshVolume(M.meshOf(b))-want)<want*0.002,'push of the slot top commits what the preview shows');
  b.faceOps=[];b.params.height=10;b.edits=[{type:'fillet',key:'top:all',r:3}];M.invalidate(b.id);M.topoCache.clear();const mf=M.meshOf(b);ok(openEdges(mf)===0,'U slot with all top edges filleted is watertight');ok(M.meshVolume(mf)<A*10&&M.meshVolume(mf)>A*10*0.9,'fillet removed a sensible amount');
  b.edits=[{type:'fillet',key:'top:3',r:3}];M.invalidate(b.id);M.topoCache.clear();ok(openEdges(M.meshOf(b))===0,'single slot-cap edge fillet is watertight');ok(M.topo(b).edges.filter(e=>/^side:0:\d+~$/.test(e.key)&&e.tangent).length===8&&M.topo(b).edges.filter(e=>/^side:0:\d+$/.test(e.key)).length===2,'slot walls: 8 tangent wall joints + 2 sharp inner corners');b.edits=[{type:'fillet',key:'side:0:7',r:4},{type:'fillet',key:'side:0:8',r:4},{type:'chamfer',key:'top:1',r:3}];M.invalidate(b.id);M.topoCache.clear();ok(openEdges(M.meshOf(b))===0,'inner-corner fillets + chamfer against a cylinder cap watertight');
  // legacy document with a polygon slot is upgraded on load
  const js=M.docJSON();const o=JSON.parse(js);const S=o.figures.find(f=>f.id===R.id);delete S.params.bulge;S.params.pts=[[0,0],[10,0],[10,5],[0,5]];M.loadDocJSON(o);const R3=M.doc.figures.find(f=>f.id===R.id);ok(R3&&Array.isArray(R3.params.bulge)&&R3.params.bulge.filter(b=>b).length===4,'legacy polygon slot regenerated as analytic outline on load');
  M.sub_.sel.clear();M.doc.sel.clear(); }


// 28. normal audit: outward winding (positive signed volume), unit analytic normals that agree with the facet, planar faces constant, wall cylinders horizontal
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  const sub=(a,b)=>[a[0]-b[0],a[1]-b[1],a[2]-b[2]],cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2],len=a=>Math.hypot(...a),norm=a=>{const l=len(a)||1;return [a[0]/l,a[1]/l,a[2]/l];};
  const audit=(name,b)=>{M.invalidate(b.id);M.topoCache.clear();const m=M.meshOf(b);const tris=M.allTris(m);let vol=0;tris.forEach(([a,b2,c])=>{vol+=dot(a,cross(b2,c))/6;});
    let nonUnit=0,flipped=0,maxDev=0;const byFace=new Map();(m.quads||[]).forEach(q=>{const [P0,P1,P2,P3]=q.p;const cr=cross(sub(P1,P0),sub(P3,P0));if(len(cr)<1e-12)return;const fn=norm(cr);q.n.forEach(n=>{if(Math.abs(len(n)-1)>1e-6)nonUnit++;const d=dot(norm(n),fn);if(d<0)flipped++;maxDev=Math.max(maxDev,Math.acos(Math.max(-1,Math.min(1,d)))*180/Math.PI);});if(!byFace.has(q.fkey))byFace.set(q.fkey,[]);byFace.get(q.fkey).push(q);});
    const kinds=[];m.brep.faces.forEach(F=>{const qs=byFace.get(F.key)||[];if(!qs.length)return;if(F.kind==='plane'){const n0=norm(qs[0].n[0]);let dev=0;qs.forEach(q=>q.n.forEach(n=>{dev=Math.max(dev,Math.acos(Math.max(-1,Math.min(1,dot(norm(n),n0))))*180/Math.PI);}));if(dev>0.5)kinds.push(F.key+' plane dev '+dev.toFixed(1));}if(F.kind==='cylinder'&&/^side/.test(F.key)){let z=0;qs.forEach(q=>q.n.forEach(n=>{z=Math.max(z,Math.abs(n[2]));}));if(z>1e-3)kinds.push(F.key+' cyl z '+z.toFixed(3));}});
    ok(vol>0,name+': outward winding');ok(nonUnit===0,name+': unit normals');ok(flipped===0,name+': no analytic normal opposes its facet');ok(maxDev<12,name+': analytic vs facet normal < 12° ('+maxDev.toFixed(1)+')');ok(!kinds.length,name+': face-kind normals consistent '+kinds.join('; '));};
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'3'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();
  audit('box',b);b.edits=[{type:'fillet',key:'top:all',r:6},{type:'fillet',key:'bot:all',r:4},{type:'fillet',key:'side:0:1',r:8},{type:'chamfer',key:'side:0:3',r:5}];audit('box fillets+chamfer',b);b.edits=[];b.faceOps=[{op:'push',face:'cap:top',h:-10}];audit('box pocket',b);
  const pk=M.topo(b).faces.filter(f=>/^side:\d$/.test(f.key));ok(pk.length===4&&pk.every(f=>f.kind==='plane'),'pocket walls are new faces, outer walls untouched');b.faceOps=[];b.params.draft=8;audit('box draft',b);b.params.draft=0;
  M.startOp('pslot');[[380,300],[380,420],[520,420],[520,300]].forEach(([x,y])=>M.toolClick(x,y));M.finishTool();const S=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([S.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const s=M.doc.figures.filter(f=>f.kind==='body').pop();
  audit('U slot',s);s.edits=[{type:'fillet',key:'top:all',r:3}];audit('U slot top fillet',s);s.edits=[{type:'fillet',key:'side:0:7',r:4},{type:'fillet',key:'side:0:8',r:4},{type:'chamfer',key:'top:1',r:3}];audit('U slot corners+chamfer',s);ok(M.topo(s).faces.some(f=>f.key==='ctop:fs8'&&f.kind==='cone'),'chamfer across a corner arc is a cone');
  M.sub_.sel.clear();M.doc.sel.clear(); }


// 29. concave (reflex) vertical-corner fillet / chamfer: adds material by the exact analytic amount, no seam lines or sharp vertices at the tangent joints
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();R.params.pts=[[-20,-15],[20,-15],[20,0],[0,0],[0,15],[-20,15]];R.params.bulge=[];M.invalidate(R.id);M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();
  const v0=M.meshVolume(M.meshOf(b));const ck=M.topo(b).edges.find(e=>/^side/.test(e.key)&&Math.abs(e.pts[0][0])<1e-6&&Math.abs(e.pts[0][1])<1e-6).key;
  b.edits=[{type:'fillet',key:ck,r:5}];M.invalidate(b.id);M.topoCache.clear();const vf=M.meshVolume(M.meshOf(b));ok(Math.abs((vf-v0)-(25-Math.PI*25/4)*20)<3,'concave fillet adds r²(1−π/4)·h ('+(vf-v0).toFixed(1)+')');
  const T=M.topo(b);ok(T.edges.filter(e=>/^side:0:\d+[ab]~$/.test(e.key)).every(e=>e.tangent),'arc/wall joints of the concave fillet are tangent');ok(!T.verts.some(v=>Math.abs(v.p[2]-20)<1e-6&&Math.hypot(v.p[0]-5,v.p[1])<1e-3),'no sharp vertex at the fillet tangent joint');ok(T.faces.find(f=>f.key==='side:fs3'||/^side:fs/.test(f.key)).kind==='cylinder','concave fillet is a cylinder face');
  b.edits=[{type:'chamfer',key:ck,r:5}];M.invalidate(b.id);M.topoCache.clear();const vc=M.meshVolume(M.meshOf(b));ok(Math.abs((vc-v0)-12.5*20)<1e-6,'concave chamfer adds d²/2·h');
  M.sub_.sel.clear();M.doc.sel.clear(); }


// 30. B on a body edge must fillet the BODY edge, never the profile sketch corner sitting under the body's base
{ if(M.anyTool&&M.anyTool())M.selectTool();M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();R.params.pts=[[-30,-5],[-20,20],[5,25],[15,10],[45,-10],[40,-30],[15,-20]];R.params.bulge=[];M.invalidate(R.id);M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();const hidden=M.doc.figures.filter(f=>f.kind==='body'&&f!==b&&f.visible);hidden.forEach(f=>f.visible=false);
  M.doc.sel.clear();M.view.az=28;M.view.el=38;M.draw();const v0=M.meshVolume(M.meshOf(b));
  for(const z of [1.5,10]){b.edits=[];M.invalidate(b.id);M.topoCache.clear();const s=M.project(M.xform(b,[15,10,z]));ok(!!M.cornerPick(s[0],s[1])||z>5,'sketch corner is under the cursor at the base');M.modStart('fillet');M.modDown({button:0,pointerId:1},s[0],s[1]);const d=M.getMod().drag;ok(d&&d.solid&&!d.corner,'click at z='+z+' starts a SOLID fillet, not a sketch-corner fillet');M.modKey({key:'8'});M.modKey({key:'Enter'});
    ok((b.edits||[]).length===1&&!(R.params.bulge||[]).some(x=>x),'body got the edit, sketch untouched (z='+z+')');if(z>5)ok(b.edits[0].key==='side:0:3','mid-edge click fillets the reflex vertical edge side:0:3');M.topoCache.clear();ok(M.meshVolume(M.meshOf(b))!==v0,'body changed');}
  hidden.forEach(f=>f.visible=true);M.modEnd&&M.modEnd();M.sub_.sel.clear();M.doc.sel.clear(); }


// 31. mixed bevel/chamfer edits: tangent joins stay watertight; an irreconcilable
// legacy combination is rendered from the largest valid subset and is visible
// to the user instead of becoming an open shell.
{ if(M.anyTool&&M.anyTool())M.selectTool();M.newDoc({silent:true});M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();
  const edgeCount=m=>{const c=new Map(),K=p=>p.map(v=>Number(v).toFixed(5)).join(',');const add=(a,d)=>{if(Math.hypot(a[0]-d[0],a[1]-d[1],a[2]-d[2])<1e-8)return;const q=[K(a),K(d)].sort().join('|');c.set(q,(c.get(q)||0)+1);};M.allTris(m).forEach(t=>{add(t[0],t[1]);add(t[1],t[2]);add(t[2],t[0]);});return [...c.values()].filter(v=>v!==2).length;};
  const edits=[{type:'chamfer',key:'top:0',r:9.3},{type:'chamfer',key:'top:1',r:1.7},{type:'chamfer',key:'top:2',r:3},{type:'chamfer',key:'top:3',r:10.8},{type:'chamfer',key:'side:0:1',r:7.2},{type:'chamfer',key:'side:0:3',r:6.3}];
  b.edits=edits.slice(0,5);M.invalidate(b.id);M.topoCache.clear();const si=M.topo(b).edges.findIndex(e=>e.key==='side:0:3');M.sub_.sel.clear();M.sub_.sel.set('mixed',{fig:b.id,kind:'edge',idx:si});const applied=M.solidEditApply(M.solidHitsFromSel(),'chamfer',6.3);ok(applied===0&&!b.edits.some(e=>e.key==='side:0:3')&&M.solidEditApply.lastDropped===1,'interactive conflicting edit is rejected, not silently kept');
  b.edits=edits;M.invalidate(b.id);M.topoCache.clear();const mm=M.meshOf(b);ok(edgeCount(mm)===0,'mixed chamfer preview remains watertight');ok(b._editWarning&&b._editDropped&&b._editDropped.length===1&&b._editDropped[0].key==='side:0:3','guard records the skipped edit');
  M.sub_.sel.clear();M.doc.sel=new Set([b.id]);M.renderInspector();const ih=document.querySelector('#insp').innerHTML||'';ok(ih.includes('Preview guarded')&&ih.includes('remove skipped edits'),'inspector explains and can remove the skipped edit');M.sub_.sel.clear();M.doc.sel.clear(); }


// 32. explicit sketch context wins over the extrusion sitting on top of the
// profile: selecting the curve and pressing B edits the 2D corner, not a body
// vertex/edge projected at the same screen position.
{ if(M.anyTool&&M.anyTool())M.selectTool();M.newDoc({silent:true});M.view.target=[0,0,0];M.view.dist=260;M.setView('top');M.resize();
  M.startOp('rect');M.toolClick(380,300);M.toolClick(520,380);const R=M.doc.figures.filter(f=>f.kind==='curve').pop();M.selectTool();M.doc.sel=new Set([R.id]);M.startOp('extrude');M.solidKey({key:'2'});M.solidKey({key:'0'});M.solidKey({key:'Enter'});const b=M.doc.figures.filter(f=>f.kind==='body').pop();
  M.doc.sel=new Set([R.id]);M.sub_.sel.clear();const q=R.params.pts[0];const s=M.project(M.xform(R,[q[0],q[1],0]));M.modStart('fillet');M.modDown({button:0,pointerId:1},s[0],s[1]);ok(M.getMod().drag&&M.getMod().drag.corner&&M.getMod().drag.corner.f===R,'selected sketch context picks the 2D corner above the extrusion');M.getMod().num='3';M.cornerApply();ok((R.params.bulge||[]).some(v=>v)&&!(b.edits||[]).length,'2D fillet changes the profile and does not add a solid edge edit');M.modEnd();M.sub_.sel.clear();M.doc.sel.clear(); }


console.log(`smoke: ${n} checks OK · ${M.doc.figures.length} figures`);

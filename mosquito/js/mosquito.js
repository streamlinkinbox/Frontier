// ═══════════════════════════════════════════════════════════════════
// mosquito.js — Procedural female Aedes aegypti (units: 1 = 1 mm)
// Research-locked morphology: lyre scutum, short white-tipped palps,
// pilose antennae, banded tarsi, 6-stylet fascicle + buckling labium.
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

// ── small helpers ──────────────────────────────────────────────────
const V3 = (x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
function segMesh(rTop, rBot, len, mat, radial=10){
  const g = new THREE.CylinderGeometry(rTop, rBot, len, radial, 1);
  g.translate(0, -len/2, 0);            // origin at joint, extends down -Y
  const m = new THREE.Mesh(g, mat);
  m.castShadow = true;
  return m;
}
function ball(r, mat, w=12, h=10){
  const m = new THREE.Mesh(new THREE.SphereGeometry(r, w, h), mat);
  m.castShadow = true;
  return m;
}
function sleeve(r, len, mat){           // crisp pale band ring
  const g = new THREE.CylinderGeometry(r, r, len, 12, 1, true);
  const m = new THREE.Mesh(g, mat);
  m.castShadow = false;
  return m;
}

// ── canvas textures ────────────────────────────────────────────────
function canvasTex(w, h, draw, srgb=true){
  const c = document.createElement('canvas'); c.width=w; c.height=h;
  draw(c.getContext('2d'), w, h);
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}
function noiseBump(){
  return canvasTex(256,256,(x,w,h)=>{
    x.fillStyle='#808080'; x.fillRect(0,0,w,h);
    for(let i=0;i<9000;i++){
      const g = 110+Math.random()*90|0;
      x.fillStyle=`rgb(${g},${g},${g})`;
      x.fillRect(Math.random()*w, Math.random()*h, 1.4, 1.4);
    }
  }, false);
}
function ommatidiaTex(){
  return canvasTex(512,512,(x,w,h)=>{
    x.fillStyle='#181110'; x.fillRect(0,0,w,h);
    const r=7, dx=r*1.74, dy=r*1.5;
    for(let row=0; row*dy<h+r; row++){
      for(let col=0; col*dx<w+r; col++){
        const cx=col*dx+(row%2?dx/2:0), cy=row*dy;
        const v = 18+Math.random()*26;
        x.beginPath();
        for(let k=0;k<6;k++){
          const a=Math.PI/3*k+Math.PI/6;
          const px=cx+Math.cos(a)*(r-0.7), py=cy+Math.sin(a)*(r-0.7);
          k?x.lineTo(px,py):x.moveTo(px,py);
        }
        x.closePath();
        x.fillStyle=`rgb(${v+22|0},${v*0.72|0},${v*0.6|0})`;
        x.fill();
        x.fillStyle='rgba(255,190,150,0.10)';
        x.fillRect(cx-2,cy-2,2,2);
      }
    }
  });
}
// Wing membrane: veins + vein scales + trailing fringe. u = span, v: 0 trailing → 1 leading
function wingTexture(){
  return canvasTex(1024,256,(x,w,h)=>{
    x.clearRect(0,0,w,h);
    // membrane base
    const g = x.createLinearGradient(0,0,0,h);
    g.addColorStop(0,'rgba(168,178,196,0.30)');
    g.addColorStop(0.5,'rgba(150,162,184,0.22)');
    g.addColorStop(1,'rgba(120,132,156,0.34)');
    x.fillStyle=g;
    x.fillRect(0,0,w,h);
    // cell noise shimmer
    for(let i=0;i<500;i++){
      x.fillStyle=`rgba(255,255,255,${Math.random()*0.05})`;
      x.fillRect(Math.random()*w, Math.random()*h, 3, 2);
    }
    // longitudinal veins (v positions: leading→trailing)
    const veins=[
      {v:0.965,w:5.0},{v:0.88,w:2.6},{v:0.78,w:2.2},{v:0.68,w:2.0},{v:0.585,w:2.0},
      {v:0.47,w:1.8},{v:0.36,w:1.6},{v:0.24,w:1.5},{v:0.12,w:1.4},
    ];
    for(const vn of veins){
      const y = h*(1-vn.v);
      x.strokeStyle='rgba(24,18,14,0.92)'; x.lineWidth=vn.w;
      x.beginPath(); x.moveTo(0,y);
      x.bezierCurveTo(w*0.3,y-3, w*0.7,y+3, w*0.995,y+6);
      x.stroke();
      // pale/dark scale speckles riding the vein
      for(let s=0;s<130;s++){
        const px=Math.random()*w, py=y+(Math.random()-0.5)*5;
        x.fillStyle = Math.random()<0.28 ? 'rgba(232,228,218,0.85)' : 'rgba(20,14,10,0.85)';
        x.save(); x.translate(px,py); x.rotate((Math.random()-0.5)*0.5);
        x.fillRect(-2.4,-0.8,4.8,1.6); x.restore();
      }
    }
    // crossveins
    x.strokeStyle='rgba(24,18,14,0.7)'; x.lineWidth=1.6;
    const cv=[[0.30,0.60,0.70],[0.44,0.47,0.585],[0.55,0.36,0.47]];
    for(const [u,a,b] of cv){
      x.beginPath(); x.moveTo(w*u,h*(1-a)); x.lineTo(w*u+8,h*(1-b)); x.stroke();
    }
    // trailing fringe (bottom edge = trailing)
    for(let i=0;i<170;i++){
      const px = (i/170)*w + Math.random()*4;
      const len = 8+Math.random()*10;
      const gr = x.createLinearGradient(0,h,0,h+len);
      gr.addColorStop(0,'rgba(30,24,20,0.9)'); gr.addColorStop(1,'rgba(30,24,20,0)');
      x.strokeStyle=gr; x.lineWidth=1.3;
      x.beginPath(); x.moveTo(px,h-1); x.lineTo(px+(Math.random()-0.5)*3,h+len); x.stroke();
    }
    // fade wing tip + base outline
    const tip = x.createLinearGradient(w*0.93,0,w,0);
    tip.addColorStop(0,'rgba(0,0,0,0)'); tip.addColorStop(1,'rgba(10,10,12,0.55)');
    x.fillStyle=tip; x.fillRect(w*0.93,0,w*0.07,h);
  });
}

// ── materials ──────────────────────────────────────────────────────
function makeMaterials(){
  const bump = noiseBump();
  bump.wrapS=bump.wrapT=THREE.RepeatWrapping; bump.repeat.set(3,3);
  const eye = ommatidiaTex();
  eye.wrapS=eye.wrapT=THREE.RepeatWrapping; eye.repeat.set(1,1);  // true ~20µm facet scale
  const M = {
    cuticle: new THREE.MeshPhysicalMaterial({color:0x241c15, roughness:0.42, metalness:0.08,
      clearcoat:0.55, clearcoatRoughness:0.45, bumpMap:bump, bumpScale:0.6}),
    scutum: new THREE.MeshPhysicalMaterial({color:0x100d0a, roughness:0.5, metalness:0.05,
      clearcoat:0.4, clearcoatRoughness:0.5, bumpMap:bump, bumpScale:0.8}),
    abdomen: new THREE.MeshPhysicalMaterial({color:0x201914, roughness:0.38, metalness:0.05,
      clearcoat:0.7, clearcoatRoughness:0.35, bumpMap:bump, bumpScale:0.5,
      emissive:0x000000, emissiveIntensity:1}),
    pale: new THREE.MeshStandardMaterial({color:0xe9e4d6, roughness:0.62, metalness:0.0}),
    leg: new THREE.MeshPhysicalMaterial({color:0x1b1512, roughness:0.46, metalness:0.06,
      clearcoat:0.4, clearcoatRoughness:0.5, bumpMap:bump, bumpScale:0.4}),
    hair: new THREE.MeshStandardMaterial({color:0x0f0c0a, roughness:0.8}),
    eye: new THREE.MeshPhysicalMaterial({color:0xffffff, map:eye, roughness:0.16, metalness:0.1,
      clearcoat:1.0, clearcoatRoughness:0.12, iridescence:0.35, iridescenceIOR:1.3}),
    wing: new THREE.MeshPhysicalMaterial({map:wingTexture(), transparent:true, roughness:0.28,
      metalness:0.0, clearcoat:0.5, iridescence:0.9, iridescenceIOR:1.32,
      side:THREE.DoubleSide, depthWrite:false, opacity:0.96}),
    vein: new THREE.MeshStandardMaterial({color:0x191411, roughness:0.6}),
    fascicle: new THREE.MeshPhysicalMaterial({color:0x2b1410, roughness:0.25, metalness:0.15,
      clearcoat:0.8, clearcoatRoughness:0.2}),
    labellum: new THREE.MeshStandardMaterial({color:0x3a2c22, roughness:0.55}),
    pulvillus: new THREE.MeshPhysicalMaterial({color:0x8a7a66, roughness:0.35, clearcoat:0.6,
      transparent:true, opacity:0.95}),
    blur: new THREE.MeshBasicMaterial({color:0x9fb0cc, transparent:true, opacity:0.0,
      side:THREE.DoubleSide, depthWrite:false, blending:THREE.AdditiveBlending}),
  };
  M.wing.shadowSide = THREE.DoubleSide;
  return M;
}

// ── wing geometry (right wing, +X span) ────────────────────────────
function wingGeometry(len=2.9){
  const NU=26, NV=9;
  const chordAt = u => { // chord profile: narrow base, max ~40%, round tip
    const c = 0.78 * Math.sin(Math.PI*Math.min(1, 0.12+0.88*u))**0.7;
    return Math.max(0.06, c*(1 - 0.35*Math.max(0,(u-0.82)/0.18)**2));
  };
  const pos=[], uv=[], idx=[];
  for(let i=0;i<=NU;i++){
    const u=i/NU, x=u*len;
    const ch=chordAt(u);
    for(let j=0;j<=NV;j++){
      const v=j/NV;                       // 0 trailing → 1 leading
      const z=(v-0.42)*ch;                // chordwise (forward +)
      const camber=Math.sin(v*Math.PI)*0.045*ch;   // gentle camber
      const droop=-0.10*u*u*len*0.25;      // slight spanwise droop
      pos.push(x, camber+droop, z);
      uv.push(u, v);
    }
  }
  for(let i=0;i<NU;i++)for(let j=0;j<NV;j++){
    const a=i*(NV+1)+j, b=a+NV+1;
    idx.push(a,b,a+1, b,b+1,a+1);
  }
  const g=new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos,3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv,2));
  g.setIndex(idx); g.computeVertexNormals();
  return g;
}

// ═══════════════════════════════════════════════════════════════════
export class Mosquito {
  constructor(){
    this.M = makeMaterials();
    this.root = new THREE.Group();
    this.root.name='mosquito';
    this.body = new THREE.Group();        // bank / pitch applied here
    this.root.add(this.body);
    this.fill = 0;                        // 0 unfed → 1 replete
    this.fluid = 'blood';                 // 'blood' | 'fuel'
    this.wingSpin = 0;                    // 0 folded … 1 full flight
    this.gripSmooth = 0;                  // 0 rough/claw … 1 smooth/pulvilli
    this._t = 0;

    this._buildThorax();
    this._buildHead();
    this._buildWings();
    this._buildLegs();
    this._buildAbdomen();
  }

  // ── thorax: humped scutum + lyre + scutellum ─────────────────────
  _buildThorax(){
    const M=this.M, T=this.thorax=new THREE.Group();
    this.body.add(T);
    const scutum = new THREE.Mesh(new THREE.SphereGeometry(0.5, 28, 22), M.scutum);
    scutum.scale.set(0.95, 0.88, 1.55);
    scutum.position.set(0, 0.14, 0.08);
    scutum.castShadow=true;
    T.add(scutum);
    // lyre marking: median line + 2 curved acrostichal lines hugging the dome
    const lyreMat = M.pale;
    const dome = (x,z)=> 0.14 + 0.44*Math.sqrt(Math.max(0.05, 1-(x/0.475)**2-((z-0.08)/0.775)**2));
    const mkLine=(pts)=>{
      const curve=new THREE.CatmullRomCurve3(pts.map(([x,z])=>V3(x, dome(x,z)+0.004, z)));
      const m=new THREE.Mesh(new THREE.TubeGeometry(curve, 24, 0.016, 6), lyreMat);
      T.add(m);
    };
    mkLine([[0,0.62],[0,0.3],[0,-0.05],[0,-0.4]]);                       // median
    mkLine([[-0.10,0.60],[-0.185,0.32],[-0.20,0.0],[-0.15,-0.33],[-0.10,-0.45]]); // left arc
    mkLine([[0.10,0.60],[0.185,0.32],[0.20,0.0],[0.15,-0.33],[0.10,-0.45]]);      // right arc
    // supra-alar white patches + scutellar scales
    for(const s of [-1,1]){
      const p=ball(0.055, M.pale); p.scale.set(1,0.45,1.4);
      p.position.set(s*0.34, 0.42, -0.28); T.add(p);
    }
    // scutellum: 3 lobes
    for(const [x,z,r] of [[0,-0.62,0.10],[-0.13,-0.58,0.075],[0.13,-0.58,0.075]]){
      const s=ball(r, M.cuticle); s.position.set(x,0.30,z); T.add(s);
      const tip=ball(r*0.45, M.pale, 8, 6); tip.position.set(x,0.30+r*0.75,z); T.add(tip);
    }
    // postnotum
    const pn=ball(0.14, M.cuticle); pn.scale.set(1.1,1.2,0.7); pn.position.set(0,0.05,-0.72); T.add(pn);
    // scutal bristles (instanced, raked backward)
    {
      const g=new THREE.ConeGeometry(0.008, 0.16, 5); g.translate(0,0.08,0);
      const n=70, inst=new THREE.InstancedMesh(g, M.hair, n);
      const d=new THREE.Object3D();
      for(let i=0;i<n;i++){
        const x=(Math.random()-0.5)*0.7, z=0.08+(Math.random()-0.5)*1.2;
        d.position.set(x, dome(x,z), z);
        d.rotation.set(0.5+Math.random()*0.5, Math.random()*Math.PI*2, (Math.random()-0.5)*0.6);
        d.updateMatrix(); inst.setMatrixAt(i, d.matrix);
      }
      inst.instanceMatrix.needsUpdate=true; T.add(inst);
    }
    // thoracic side stripe (pale scale patch)
    for(const s of [-1,1]){
      const p=ball(0.07, M.pale); p.scale.set(0.5,1.1,1.8);
      p.position.set(s*0.44, -0.05, 0.1); T.add(p);
    }
  }

  // ── head: eyes, antennae, clypeus, palps, proboscis ──────────────
  _buildHead(){
    const M=this.M;
    const H=this.head=new THREE.Group();
    H.position.set(0,-0.02,0.95);
    this.body.add(H);
    const cap=ball(0.34, M.cuticle, 20, 16); cap.scale.set(1,0.95,0.85); H.add(cap);
    // compound eyes (kidney-ish ellipsoids)
    this.eyes=[];
    for(const s of [-1,1]){
      const e=new THREE.Mesh(new THREE.SphereGeometry(0.235, 24, 20), M.eye);
      e.scale.set(0.72,1.18,0.92);
      e.position.set(s*0.255, 0.06, 0.10);
      e.rotation.set(0.1, s*-0.35, s*0.12);
      e.castShadow=true; H.add(e); this.eyes.push(e);
      // pale eye rim (postocular scales)
      const rim=new THREE.Mesh(new THREE.TorusGeometry(0.20,0.018,8,24,Math.PI*1.1), M.pale);
      rim.position.set(s*0.30,0.06,0.02); rim.rotation.set(0.3,s*1.2,s*0.5);
      H.add(rim);
    }
    // clypeus + 2 white spots (aegypti key character)
    const cly=ball(0.09, M.cuticle, 10, 8); cly.scale.set(1.1,1.2,0.6);
    cly.position.set(0,0.0,0.30); H.add(cly);
    for(const s of [-1,1]){
      const sp=ball(0.026, M.pale, 8, 6); sp.position.set(s*0.055,0.03,0.345); H.add(sp);
    }
    // antennae: pedicel + 13-seg pilose flagellum (female: short sparse whorls)
    this.antennae=[];
    for(const s of [-1,1]){
      const A=new THREE.Group(); A.position.set(s*0.115,0.20,0.24); H.add(A);
      const ped=ball(0.082, M.cuticle, 12, 10); A.add(ped);
      const ring=new THREE.Mesh(new THREE.TorusGeometry(0.070,0.014,8,16), M.pale);
      ring.rotation.x=Math.PI/2; ring.position.y=0.045; A.add(ring);
      const flag=new THREE.Group(); flag.position.y=0.06; A.add(flag);
      const curve=new THREE.CatmullRomCurve3([V3(0,0,0),V3(s*0.06,0.45,0.16),V3(s*0.16,0.95,0.30),V3(s*0.26,1.30,0.38)]);
      flag.add(new THREE.Mesh(new THREE.TubeGeometry(curve,16,0.016,6), M.leg));
      // whorl hairs
      const hg=new THREE.ConeGeometry(0.006,0.075,4); hg.translate(0,0.037,0);
      const inst=new THREE.InstancedMesh(hg, M.hair, 13*5);
      const d=new THREE.Object3D(); let k=0;
      for(let wSeg=0;wSeg<13;wSeg++){
        const t=(wSeg+0.5)/13, p=curve.getPoint(t);
        for(let a=0;a<5;a++){
          const ang=a/5*Math.PI*2+wSeg*0.5;
          d.position.copy(p);
          d.rotation.set(0,ang,0); d.rotateX(1.29);   // radial whorl, tilted up
          d.updateMatrix(); inst.setMatrixAt(k++, d.matrix);
        }
      }
      inst.instanceMatrix.needsUpdate=true; flag.add(inst);
      A.rotation.set(-0.55, s*0.30, s*-0.18);   // rake up-forward-out
      this.antennae.push({root:A, flag, side:s, seed:Math.random()*9});
    }
    // maxillary palps: short, 4 segs, white-tipped (female culicine)
    this.palps=[];
    for(const s of [-1,1]){
      const P=new THREE.Group(); P.position.set(s*0.085,-0.10,0.28); H.add(P);
      let parent=P; const segs=[];
      const lens=[0.13,0.12,0.11,0.12];
      lens.forEach((L,i)=>{
        const g=new THREE.Group();
        if(i>0) g.position.z=lens[i-1];
        const geo=new THREE.CylinderGeometry(0.030-0.004*i,0.034-0.004*i,L,8);
        geo.rotateX(Math.PI/2); geo.translate(0,0,L/2);
        const mesh=new THREE.Mesh(geo, i===3?M.pale:M.leg);
        mesh.castShadow=true; g.add(mesh); parent.add(g); parent=g; segs.push(g);
      });
      P.rotation.set(0.85, s*-0.28, 0);          // angled down-forward
      this.palps.push({root:P, segs, side:s, seed:Math.random()*9});
    }
    // proboscis: gutter labium (8 buckling segs) + sliding 6-stylet fascicle
    const PB=this.proboscisBase=new THREE.Group();
    PB.position.set(0,-0.13,0.30); H.add(PB);
    this.labium=[];
    {
      let parent=PB; const L=0.23;
      for(let i=0;i<8;i++){
        const g=new THREE.Group();
        if(i>0) g.position.z=L;
        const r0=0.046-0.0022*i, r1=0.044-0.0022*i;
        const geo=new THREE.CylinderGeometry(r1,r0,L,10);
        geo.rotateX(Math.PI/2); geo.translate(0,0,L/2);
        const mesh=new THREE.Mesh(geo, M.leg); mesh.castShadow=true;
        g.add(mesh);
        // dorsal labial scales shimmer
        parent.add(g); parent=g; this.labium.push(g);
      }
      // labella: paired lobes at tip
      this.labella=new THREE.Group(); this.labella.position.z=L; parent.add(this.labella);
      for(const s of [-1,1]){
        const lb=ball(0.045, M.labellum, 10, 8);
        lb.scale.set(0.8,0.6,1.5); lb.position.set(s*0.035,0,0.05);
        this.labella.add(lb);
      }
    }
    this.fascicle=new THREE.Group(); PB.add(this.fascicle);
    {
      // bundle: labrum (food canal) + hypopharynx + 2 maxillae + 2 mandibles
      const bundle=new THREE.CylinderGeometry(0.013,0.006,2.1,8);
      bundle.rotateX(Math.PI/2); bundle.translate(0,0,1.05);
      const m=new THREE.Mesh(bundle, M.fascicle); m.castShadow=false;
      this.fascicle.add(m);
      // serrated maxilla hint: two hair-thin barbed stylets flanking tip
      for(const s of [-1,1]){
        const st=new THREE.CylinderGeometry(0.004,0.002,0.5,5);
        st.rotateX(Math.PI/2); st.translate(s*0.016,0,1.95);
        this.fascicle.add(new THREE.Mesh(st, M.fascicle));
      }
      this.fascicleSlide=0;                        // 0 sheathed → 1 fully out
    }
    PB.rotation.x=0.62;                            // rest: down-forward
  }

  // ── wings + halteres ─────────────────────────────────────────────
  _buildWings(){
    const M=this.M;
    this.wings={};
    const wg=wingGeometry(2.9);
    for(const s of [-1,1]){
      const side=s<0?'L':'R';
      const tilt=new THREE.Group(); tilt.position.set(s*0.26,0.34,-0.02);
      const stroke=new THREE.Group(), dev=new THREE.Group(), pitch=new THREE.Group();
      tilt.add(stroke); stroke.add(dev); dev.add(pitch);
      const mesh=new THREE.Mesh(wg, M.wing);
      mesh.scale.x=s; mesh.castShadow=true;   // mirror for left
      pitch.add(mesh);
      // costa vein tube along leading edge
      const costa=new THREE.CatmullRomCurve3([
        V3(0.02*s,0.01,0.24),V3(0.9*s,0.0,0.30),V3(1.9*s,-0.02,0.22),V3(2.75*s,-0.05,0.05)]);
      pitch.add(new THREE.Mesh(new THREE.TubeGeometry(costa,20,0.016,6), M.vein));
      // tegula (wing-base scale cap)
      const teg=ball(0.07, M.pale, 10, 8); teg.position.set(s*0.02,0.03,0.02); pitch.add(teg);
      // blur envelope (stroke-arc ghost for full-speed flight)
      const blur=new THREE.Mesh(wg, M.blur.clone());
      blur.scale.set(s,1,1.9); blur.position.z=0; pitch.add(blur);
      this.body.add(tilt);
      this.wings[side]={tilt,stroke,dev,pitch,mesh,blur,side:s};
    }
    // halteres (Diptera balancing organs) oscillate antiphase to wings
    this.halteres=[];
    for(const s of [-1,1]){
      const Hg=new THREE.Group(); Hg.position.set(s*0.20,0.16,-0.52);
      const stalk=segMesh(0.022,0.03,0.34,M.leg,8);
      stalk.geometry=stalk.geometry.clone();
      stalk.rotation.z=s*1.15; stalk.position.set(s*0.0,0.0,0);
      // orient stalk outward: wrap
      const arm=new THREE.Group(); arm.add(stalk);
      stalk.position.set(0,0,0);
      const knob=ball(0.068,M.cuticle,10,8);
      knob.position.set(s*0.31,-0.12,0);   // at stalk tip (out-down)
      Hg.add(arm); Hg.add(knob);
      Hg.userData={arm,knob,s,
        armBase:arm.rotation.clone(), knobBase:knob.position.clone()};
      this.body.add(Hg); this.halteres.push(Hg);
    }
  }

  // ── legs ×6 ──────────────────────────────────────────────────────
  _buildLegs(){
    const M=this.M;
    // [pair, side] — pair 0 front, 1 mid, 2 hind
    const defs=[
      {pair:0, hip:[0.20,-0.26,0.55], F:1.60, T:1.70, yaw: 0.95},
      {pair:1, hip:[0.25,-0.30,0.05], F:1.65, T:1.75, yaw: 1.50},
      {pair:2, hip:[0.23,-0.28,-0.45],F:1.95, T:2.05, yaw: 2.30},
    ];
    this.legs=[];
    for(const s of [-1,1]) for(const d of defs){
      const root=new THREE.Group();
      root.position.set(s*d.hip[0], d.hip[1], d.hip[2]);
      this.body.add(root);
      const yawG=new THREE.Group(); root.add(yawG);
      const coxa=segMesh(0.055,0.05,0.22,M.leg); yawG.add(coxa);
      const femurG=new THREE.Group(); femurG.position.y=-0.22; yawG.add(femurG);
      const femur=segMesh(0.055,0.042,d.F,M.leg); femurG.add(femur);
      // pale knee spot (aegypti)
      const knee=sleeve(0.046,0.10,M.pale); knee.position.y=-d.F+0.05; femurG.add(knee);
      const tibiaG=new THREE.Group(); tibiaG.position.y=-d.F; femurG.add(tibiaG);
      const tibia=segMesh(0.038,0.026,d.T,M.leg); tibiaG.add(tibia);
      // tibial bristles
      {
        const g=new THREE.ConeGeometry(0.007,0.12,4); g.translate(0,0.06,0);
        const inst=new THREE.InstancedMesh(g,M.hair,8);
        const o=new THREE.Object3D();
        for(let i=0;i<8;i++){
          o.position.set(0,-0.2-1.3*i/7,0);
          o.rotation.set(0, i*2.4, 0); o.rotateX(2.07);  // radial fan, raked down
          o.updateMatrix(); inst.setMatrixAt(i,o.matrix);
        }
        inst.instanceMatrix.needsUpdate=true; tibiaG.add(inst);
      }
      // tarsus: 5 articulated tarsomeres with basal pale bands
      const tarsusG=new THREE.Group(); tarsusG.position.y=-d.T; tibiaG.add(tarsusG);
      const tLens=[0.36,0.26,0.20,0.16,0.24];
      const tarsals=[]; let parent=tarsusG;
      tLens.forEach((L,i)=>{
        const g=new THREE.Group();
        if(i>0) g.position.y=-tLens[i-1];
        const r=0.024-0.0024*i;
        const m=segMesh(r, Math.max(0.010,r-0.003), L, (d.pair===2&&i===4)?M.pale:M.leg, 8);
        g.add(m);
        if(!(d.pair===2&&i===4) && i<4){       // basal bands; hind t5 all-white
          const b=sleeve(r+0.002, Math.min(0.09,L*0.4), M.pale);
          b.position.y=-0.03; g.add(b);
        }
        if(d.pair===0&&i===0){ /* fore t1 slightly thicker */ }
        parent.add(g); parent=g; tarsals.push(g);
      });
      // pretarsus: paired claws + pulvilli + empodium
      const pre=new THREE.Group(); pre.position.y=-tLens[4]; parent.add(pre);
      const claws=[];
      for(const cs of [-1,1]){
        const cg=new THREE.Group(); cg.rotation.z=cs*0.5; pre.add(cg);
        const c1=segMesh(0.012,0.008,0.07,M.vein,6); cg.add(c1);
        const tip=new THREE.Group(); tip.position.y=-0.07; tip.rotation.x=0.9; cg.add(tip);
        const c2=segMesh(0.008,0.001,0.06,M.vein,6); tip.add(c2);
        claws.push({base:cg,tip});
      }
      const pulv=[];
      for(const cs of [-1,1]){
        const p=ball(0.045,M.pulvillus,8,6);
        p.scale.set(0.8,0.5,1.2); p.position.set(cs*0.035,-0.02,0.01);
        pre.add(p); pulv.push(p);
      }
      const emp=segMesh(0.008,0.001,0.08,M.hair,5);
      emp.position.z=0.01; pre.add(emp);
      // tibio-tarsal pale band
      const tb=sleeve(0.028,0.07,M.pale); tb.position.y=-d.T+0.03; tibiaG.add(tb);

      yawG.rotation.y = s*d.yaw;
      this.legs.push({
        root, yawG, femurG, tibiaG, tarsusG, tarsals, pre, claws, pulv,
        side:s, pair:d.pair, F:d.F, T:d.T, coxaLen:0.22,
        tarsusLen:tLens.reduce((a,b)=>a+b,0),
        neutralYaw:s*d.yaw,
        // neutral foothold in body frame (workspace centres — non-overlapping!)
        neutral:V3(s*(d.pair===1?2.8:2.4), -2.45, d.pair===0?2.1:(d.pair===1?0.1:-2.4)),
        wsR:new THREE.Vector3(0.70, 0, d.pair===1?0.90:0.75),  // workspace ellipse radii
      });
    }
  }

  // ── abdomen: 8 articulated segments + cerci ──────────────────────
  _buildAbdomen(){
    const M=this.M;
    this.abdomenG=new THREE.Group();
    this.abdomenG.position.set(0,0.04,-0.62);
    this.body.add(this.abdomenG);
    const radii=[0.28,0.315,0.325,0.315,0.295,0.26,0.215,0.16];
    const segLen=0.32;
    this.abSegs=[];
    let parent=this.abdomenG;
    radii.forEach((r,i)=>{
      const g=new THREE.Group();
      if(i>0) g.position.z=-segLen;
      const m=new THREE.Mesh(new THREE.SphereGeometry(r,18,14), M.abdomen);
      m.scale.set(1,0.82,1.18); m.position.z=-segLen*0.5; m.castShadow=true;
      g.add(m);
      if(i>0&&i<7){                                   // pale basal band
        const band=new THREE.Mesh(new THREE.TorusGeometry(r*0.96,0.028,8,22,Math.PI*1.5), M.pale);
        band.position.z=-0.06; band.rotation.z=Math.PI*0.75; band.scale.set(1,0.82,1);
        g.add(band);
      }
      parent.add(g); parent=g;
      this.abSegs.push({g, mesh:m, r, i});
    });
    // cerci: paired finger-like lobes (female)
    this.cerci=new THREE.Group(); this.cerci.position.z=-segLen; parent.add(this.cerci);
    for(const s of [-1,1]){
      const c=segMesh(0.020,0.008,0.14,M.cuticle,8);
      const w=new THREE.Group(); w.rotation.x=-1.9; w.rotation.y=s*0.25;
      w.add(c); w.position.set(s*0.03,0,0); this.cerci.add(w);
    }
    // abdominal setae — parented per segment so they ride articulation + swell
    {
      const g=new THREE.ConeGeometry(0.006,0.09,4); g.translate(0,0.045,0);
      const up=new THREE.Vector3(0,1,0), dir=new THREE.Vector3();
      const o=new THREE.Object3D();
      this.abSegs.forEach((sg,sgi)=>{
        const inst=new THREE.InstancedMesh(g,M.hair,8);
        for(let a=0;a<8;a++){
          const ang=a/8*Math.PI*2+sgi*0.4;
          o.position.set(Math.cos(ang)*sg.r*0.92, Math.sin(ang)*sg.r*0.75, -segLen*0.5);
          dir.set(Math.cos(ang),Math.sin(ang),-0.55).normalize();
          o.quaternion.setFromUnitVectors(up,dir);
          o.updateMatrix(); inst.setMatrixAt(a,o.matrix);
        }
        inst.instanceMatrix.needsUpdate=true;
        sg.g.add(inst);
      });
    }
  }

  // ── engorgement: swell + fluid colour ────────────────────────────
  setFill(f, fluid){
    this.fill=THREE.MathUtils.clamp(f,0,1);
    if(fluid) this.fluid=fluid;
    const M=this.M, F=this.fill;
    const prof=i=> i===0?0.35 : i<6 ? 1 : 0.55;      // midgut swells most amidships
    for(const s of this.abSegs){
      const sw=1+0.62*F*prof(s.i);
      s.mesh.scale.set(sw, 0.82*sw, 1.18*(1+0.10*F));
      s.g.position.z = s.i===0?0:-0.32*(1+0.10*F);
    }
    const blood=new THREE.Color(0x4d0d0c), fuel=new THREE.Color(0x6e3f08);
    const eBlood=new THREE.Color(0xff2e12), eFuel=new THREE.Color(0xff9a1e);
    const isBlood=this.fluid==='blood';
    M.abdomen.color.lerpColors(new THREE.Color(0x201914), isBlood?blood:fuel, F);
    M.abdomen.emissive.lerpColors(new THREE.Color(0x000000), isBlood?eBlood:eFuel, F*0.75);
    M.abdomen.clearcoat=0.7+0.3*F;                    // stretched cuticle shines
  }

  // ── micro-life: breathing, antennae, palps ───────────────────────
  updateMicro(dt, t){
    this._t=t;
    // abdominal breathing (subtle metachronal pulse)
    const br=Math.sin(t*Math.PI*2*1.4)*0.5+0.5;
    for(const s of this.abSegs){
      const w=1+Math.sin(t*Math.PI*2*1.4 - s.i*0.55)*0.008;
      s.mesh.scale.x*=w; s.mesh.scale.y*=w;
    }
    // antennae: slow scanning sway + tiny tremor
    for(const a of this.antennae){
      a.root.rotation.x=-0.55+Math.sin(t*0.9+a.seed)*0.035+Math.sin(t*7.3+a.seed*2)*0.006;
      a.root.rotation.y=a.side*0.30+Math.sin(t*0.63+a.seed*1.7)*0.05;
    }
    // palp micro-motion
    for(const p of this.palps){
      p.root.rotation.x=0.85+Math.sin(t*1.1+p.seed)*0.03;
      p.segs[2].rotation.x=Math.sin(t*1.7+p.seed*2)*0.06;
    }
    // pulvilli spread vs claw hook by surface grip blend
    const g=this.gripSmooth;
    for(const leg of this.legs){
      for(const p of leg.pulv) p.scale.set(0.8+0.7*g, 0.5+0.35*g, 1.2+0.5*g);
      for(const c of leg.claws){ c.base.rotation.x=-0.15-0.5*(1-g); }
    }
    void br;
  }

  // world anchors for UI labels
  anchors(out={}){
    const o=new THREE.Object3D();
    const at=(obj,x=0,y=0,z=0)=>{
      o.position.set(x,y,z); obj.updateWorldMatrix(true,false);
      return obj.localToWorld(o.position.clone());
    };
    out.eye=at(this.eyes[1]);
    out.antenna=at(this.antennae[1].flag,0.26,1.3,0.38);
    out.palp=at(this.palps[1].segs[3],0,0,0.12);
    out.fascicle=at(this.fascicle,0,0,1.9*this.fascicleSlide+0.4);
    out.labium=at(this.labium[4],0,0,0.12);
    out.wing=at(this.wings.R.pitch,2.2,0,0.1);
    out.haltere=at(this.halteres[1],0.31,0.14,0);
    out.scutum=at(this.thorax,0,0.62,0.1);
    out.tarsus=at(this.legs[4].tarsals[2],0,-0.1,0);
    out.pulvillus=at(this.legs[4].pre,0,-0.05,0);
    out.abdomen=at(this.abSegs[3].g,0,0.3,-0.16);
    out.cercus=at(this.cerci,0.05,0,-0.1);
    out.knee=at(this.legs[5].tibiaG,0,0.25,0);
    return out;
  }
}


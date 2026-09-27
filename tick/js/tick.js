// ═══════════════════════════════════════════════════════════════════
// tick.js — Giant female hard tick (Ixodes-style), units: 1 = 1 mm
// Body ~90 mm unfed: scutum (never expands) + alloscutum (balloons),
// barbed hypostome + cement cone, Haller's organ, 8 walking legs.
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

const V3=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
function segMesh(rTop,rBot,len,mat,radial=10){
  const g=new THREE.CylinderGeometry(rTop,rBot,len,radial,1);
  g.translate(0,-len/2,0);
  const m=new THREE.Mesh(g,mat); m.castShadow=true; return m;
}
function ball(r,mat,w=14,h=12){
  const m=new THREE.Mesh(new THREE.SphereGeometry(r,w,h),mat);
  m.castShadow=true; return m;
}
function ctex(w,h,draw,srgb=true){
  const c=document.createElement('canvas'); c.width=w; c.height=h;
  draw(c.getContext('2d'),w,h);
  const t=new THREE.CanvasTexture(c);
  if(srgb) t.colorSpace=THREE.SRGBColorSpace;
  t.anisotropy=4; return t;
}
// scutum: dark shield with punctations + cervical grooves
function scutumTex(){
  return ctex(512,512,(x,w,h)=>{
    const g=x.createRadialGradient(w/2,h*0.42,40,w/2,h/2,340);
    g.addColorStop(0,'#2b1d12'); g.addColorStop(0.7,'#1c130c'); g.addColorStop(1,'#120d08');
    x.fillStyle=g; x.fillRect(0,0,w,h);
    for(let i=0;i<900;i++){ // punctations
      const px=Math.random()*w, py=Math.random()*h, r=1+Math.random()*2.6;
      x.fillStyle='rgba(0,0,0,0.5)'; x.beginPath(); x.arc(px,py,r,0,7); x.fill();
      x.fillStyle='rgba(255,220,180,0.10)'; x.beginPath(); x.arc(px-r*0.3,py-r*0.3,r*0.6,0,7); x.fill();
    }
    x.strokeStyle='rgba(0,0,0,0.65)'; x.lineWidth=7; // cervical grooves
    for(const s of [-1,1]){ x.beginPath(); x.moveTo(w/2+s*44,60);
      x.quadraticCurveTo(w/2+s*70,220,w/2+s*52,380); x.stroke(); }
    x.strokeStyle='rgba(255,225,190,0.12)'; x.lineWidth=3; // lateral carinae
    for(const s of [-1,1]){ x.beginPath(); x.moveTo(w/2+s*150,90);
      x.quadraticCurveTo(w/2+s*185,260,w/2+s*130,430); x.stroke(); }
  });
}
// alloscutum: mammillated + setae pits
function alloTex(){
  return ctex(512,512,(x,w,h)=>{
    x.fillStyle='#5a2e1a'; x.fillRect(0,0,w,h);
    for(let i=0;i<2600;i++){
      const px=Math.random()*w, py=Math.random()*h, r=1.5+Math.random()*4;
      x.fillStyle=`rgba(20,8,4,${0.12+Math.random()*0.2})`;
      x.beginPath(); x.arc(px,py,r,0,7); x.fill();
      x.fillStyle=`rgba(255,170,120,${Math.random()*0.10})`;
      x.beginPath(); x.arc(px-1,py-1,r*0.5,0,7); x.fill();
    }
  });
}
function noiseBump(){
  return ctex(256,256,(x,w,h)=>{
    x.fillStyle='#808080'; x.fillRect(0,0,w,h);
    for(let i=0;i<7000;i++){ const g=110+Math.random()*90|0;
      x.fillStyle=`rgb(${g},${g},${g})`; x.fillRect(Math.random()*w,Math.random()*h,1.5,1.5); }
  },false);
}
function makeMaterials(){
  const bump=noiseBump(); bump.wrapS=bump.wrapT=THREE.RepeatWrapping;
  return {
    scutum:new THREE.MeshPhysicalMaterial({map:scutumTex(), roughness:0.42, metalness:0.05,
      clearcoat:0.6, clearcoatRoughness:0.4, bumpMap:bump, bumpScale:0.5}),
    allo:new THREE.MeshPhysicalMaterial({map:alloTex(), roughness:0.5, metalness:0.02,
      clearcoat:0.5, clearcoatRoughness:0.5, bumpMap:bump, bumpScale:0.9,
      emissive:0x000000}),
    dark:new THREE.MeshPhysicalMaterial({color:0x241610, roughness:0.45, metalness:0.05,
      clearcoat:0.5, clearcoatRoughness:0.45, bumpMap:bump, bumpScale:0.4}),
    leg:new THREE.MeshPhysicalMaterial({color:0x3a2114, roughness:0.5, metalness:0.04,
      clearcoat:0.4, clearcoatRoughness:0.5, bumpMap:bump, bumpScale:0.3}),
    joint:new THREE.MeshStandardMaterial({color:0x8a6a4a, roughness:0.6}),
    pale:new THREE.MeshStandardMaterial({color:0xd8c9ae, roughness:0.55}),
    hair:new THREE.MeshStandardMaterial({color:0x140c07, roughness:0.85}),
    hypo:new THREE.MeshPhysicalMaterial({color:0x6e4a30, roughness:0.35, clearcoat:0.7}),
    cement:new THREE.MeshPhysicalMaterial({color:0xe8e0cc, roughness:0.4, clearcoat:0.3,
      transparent:true, opacity:0.92}),
    haller:new THREE.MeshStandardMaterial({color:0xf0e6cc, roughness:0.3,
      emissive:0x554422, emissiveIntensity:0.4}),
    spiracle:new THREE.MeshStandardMaterial({color:0xcbb98f, roughness:0.5}),
  };
}

export class Tick{
  constructor(){
    this.M=makeMaterials();
    this.root=new THREE.Group(); this.root.name='tick';
    this.body=new THREE.Group(); this.root.add(this.body);
    this.fill=0; this.quest=0; this.cement=0; this.hypo=0;
    this._t=0;
    this._buildBody();
    this._buildCapitulum();
    this._buildLegs();
    this._buildVenter();
  }
  // ── idiosoma: fixed scutum + expanding alloscutum ────────────────
  _buildBody(){
    const M=this.M, B=this.body;
    // alloscutum sac (posterior 2/3) — THE balloon
    this.alloG=new THREE.Group(); B.add(this.alloG);
    const allo=new THREE.Mesh(new THREE.SphereGeometry(30,30,24), M.allo);
    allo.scale.set(1.08,0.52,1.55); allo.position.set(0,2,-14);
    allo.castShadow=true; this.alloG.add(allo); this.alloMesh=allo;
    // marginal groove hint
    const groove=new THREE.Mesh(new THREE.TorusGeometry(30,1.1,8,40,Math.PI*1.2), M.dark);
    groove.rotation.set(Math.PI/2,0,Math.PI*0.9); groove.scale.set(1.02,1.5,0.5);
    groove.position.set(0,1,-14); this.alloG.add(groove);
    // festoons: 11 posterior marginal segments (ride the expansion)
    this.festoonG=new THREE.Group(); this.alloG.add(this.festoonG);
    for(let i=0;i<11;i++){
      const a=Math.PI*(0.12+0.76*i/10);       // fan across posterior
      const f=ball(4.6,M.allo,10,8);
      f.scale.set(1,0.55,1.25);
      f.position.set(Math.cos(a)*33.5, 0.5, -14-Math.sin(a)*44);
      this.festoonG.add(f);
    }
    // scutum shield (anterior, NEVER expands — key hard-tick trait)
    const sc=new THREE.Mesh(new THREE.SphereGeometry(26,28,20,0,Math.PI*2,0,Math.PI/2), M.scutum);
    sc.scale.set(1.22,0.42,1.05); sc.rotation.x=Math.PI; sc.position.set(0,9.5,16);
    sc.castShadow=true; B.add(sc); this.scutumMesh=sc;
    // scapulae (anterior shoulders)
    for(const s of [-1,1]){
      const sp=ball(3.4,M.scutum,8,6); sp.position.set(s*22,8.5,34); B.add(sp);
    }
    // scutal setae
    {
      const g=new THREE.ConeGeometry(0.35,5,4); g.translate(0,2.5,0);
      const inst=new THREE.InstancedMesh(g,M.hair,46);
      const o=new THREE.Object3D();
      for(let i=0;i<46;i++){
        const x=(Math.random()-0.5)*44, z=16+(Math.random()-0.5)*36;
        const dome=9.5+10*Math.sqrt(Math.max(0.05,1-(x/31.7)**2-((z-16)/27.3)**2));
        o.position.set(x,dome,z);
        o.rotation.set(0.4+Math.random()*0.5,Math.random()*6.28,(Math.random()-0.5)*0.5);
        o.updateMatrix(); inst.setMatrixAt(i,o.matrix);
      }
      inst.instanceMatrix.needsUpdate=true; B.add(inst);
    }
    // alloscutal setae (parented to sac → spread with engorgement)
    {
      const g=new THREE.ConeGeometry(0.4,6,4); g.translate(0,3,0);
      const inst=new THREE.InstancedMesh(g,M.hair,120);
      const o=new THREE.Object3D();
      for(let i=0;i<120;i++){
        const a=Math.random()*Math.PI*2, e=Math.random()*1.1;
        const px=Math.cos(a)*32*Math.cos(e), pz=-14-Math.sin(a)*46*Math.cos(e);
        const py=2+15*Math.sin(e)+2;
        o.position.set(px,py,pz);
        o.rotation.set(0,0,0); o.rotateY(a); o.rotateX(-0.6-e*0.3);
        o.updateMatrix(); inst.setMatrixAt(i,o.matrix);
      }
      inst.instanceMatrix.needsUpdate=true; this.alloG.add(inst);
    }
  }
  // ── capitulum: palps, chelicerae, barbed hypostome ────────────────
  _buildCapitulum(){
    const M=this.M;
    const C=this.capitulum=new THREE.Group();
    C.position.set(0,2.5,38); this.body.add(C);
    // basis capituli (rectangular, with posterior cornua + porose areas ♀)
    const basis=new THREE.Mesh(new THREE.BoxGeometry(20,9,10), M.dark);
    basis.castShadow=true; C.add(basis);
    for(const s of [-1,1]){
      const cornu=new THREE.Mesh(new THREE.ConeGeometry(1.6,4.5,6), M.dark);
      cornu.rotation.x=-2.2; cornu.position.set(s*7,3.5,-5.5); C.add(cornu);
      const pore=ball(1.4,M.joint,8,6); pore.position.set(s*5,4.7,-1); C.add(pore);
    }
    // palps: 4 articles, articulate + splay when feeding
    this.palps=[];
    for(const s of [-1,1]){
      const P=new THREE.Group(); P.position.set(s*9,0,4); C.add(P);
      const lens=[5,6,5,4]; let parent=P; const segs=[];
      lens.forEach((L,i)=>{
        const g=new THREE.Group(); if(i>0) g.position.z=lens[i-1];
        const geo=new THREE.CylinderGeometry(2.0-0.25*i,2.3-0.25*i,L,8);
        geo.rotateX(Math.PI/2); geo.translate(0,0,L/2);
        const mesh=new THREE.Mesh(geo,M.leg); mesh.castShadow=true;
        g.add(mesh);
        const tuft=new THREE.Mesh(new THREE.ConeGeometry(0.5,3,4),M.hair);
        tuft.position.set(s*1.5,1.5,L*0.6); tuft.rotation.z=s*1.2; g.add(tuft);
        parent.add(g); parent=g; segs.push(g);
      });
      P.rotation.y=s*0.12;
      this.palps.push({root:P,segs,side:s});
    }
    // chelicerae: sheaths + hooked digits (saw during insertion)
    this.chelicerae=[];
    for(const s of [-1,1]){
      const g=new THREE.Group(); g.position.set(s*3.2,0.5,4); C.add(g);
      const geo=new THREE.CylinderGeometry(1.3,1.6,14,8);
      geo.rotateX(Math.PI/2); geo.translate(0,0,7);
      const sh=new THREE.Mesh(geo,M.dark); sh.castShadow=true; g.add(sh);
      const dg=new THREE.Group(); dg.position.z=14; g.add(dg);
      const hook=new THREE.Mesh(new THREE.ConeGeometry(0.9,4,6),M.hypo);
      hook.rotation.x=Math.PI/2+0.7; hook.position.z=1.5; dg.add(hook);
      this.chelicerae.push({root:g,digit:dg,side:s});
    }
    // hypostome: THE harpoon — tapered shaft + retrorse barbs
    const H=this.hypostome=new THREE.Group(); C.add(H);
    {
      const geo=new THREE.CylinderGeometry(0.8,2.2,20,8);
      geo.rotateX(Math.PI/2); geo.translate(0,0,10);
      const shaft=new THREE.Mesh(geo,M.hypo); shaft.castShadow=true; H.add(shaft);
      for(let row=0;row<7;row++)for(const s of [-1,1]){
        const barb=new THREE.Mesh(new THREE.ConeGeometry(0.75,3.2,5),M.hypo);
        barb.position.set(s*1.5,-0.4,4+row*2.3);
        barb.rotation.z=s*2.25; barb.rotation.y=s*0.2;  // swept BACK (retrorse)
        H.add(barb);
      }
      const tip=new THREE.Mesh(new THREE.ConeGeometry(0.8,3,6),M.hypo);
      tip.rotation.x=Math.PI/2; tip.position.z=21; H.add(tip);
    }
    // cement cone (grows around hypostome during attachment)
    this.cementMesh=new THREE.Mesh(new THREE.ConeGeometry(5,7,12,1,true),M.cement);
    this.cementMesh.rotation.x=Math.PI/2; this.cementMesh.position.z=4;
    this.cementMesh.scale.setScalar(0.001); C.add(this.cementMesh);
    this.hypoSlide=0;
  }
  // ── 8 legs ───────────────────────────────────────────────────────
  _buildLegs(){
    const M=this.M;
    // pairs I..IV front→back; lengths grow rearward
    const defs=[
      {F:20,T:24,hip:[14,-6,26],yaw:0.85},
      {F:22,T:26,hip:[19,-8,8], yaw:1.35},
      {F:24,T:28,hip:[19,-8,-12],yaw:1.95},
      {F:26,T:30,hip:[15,-7,-28],yaw:2.45},
    ];
    this.legs=[];
    for(const s of [-1,1]) for(let pi=0;pi<4;pi++){
      const d=defs[pi];
      const root=new THREE.Group();
      root.position.set(s*d.hip[0],d.hip[1],d.hip[2]);
      this.body.add(root);
      const yawG=new THREE.Group(); root.add(yawG);
      // coxa with ventral spur (pair I spur longest — Ixodes trait)
      const coxa=segMesh(3.2,2.8,7,M.leg); yawG.add(coxa);
      const spur=new THREE.Mesh(new THREE.ConeGeometry(1.1,pi===0?7:4,6),M.dark);
      spur.rotation.x=Math.PI; spur.position.set(0,-6,s*1.5); yawG.add(spur);
      const troch=ball(2.6,M.joint,8,6); troch.position.y=-7; yawG.add(troch);
      const femurG=new THREE.Group(); femurG.position.y=-8; yawG.add(femurG);
      femurG.add(segMesh(2.6,2.0,d.F,M.leg));
      const genu=ball(2.3,M.joint,8,6); genu.position.y=-d.F; femurG.add(genu);
      const tibiaG=new THREE.Group(); tibiaG.position.y=-d.F; femurG.add(tibiaG);
      tibiaG.add(segMesh(1.9,1.3,d.T,M.leg));
      // tibial setae
      {
        const g=new THREE.ConeGeometry(0.3,3.4,4); g.translate(0,1.7,0);
        const inst=new THREE.InstancedMesh(g,M.hair,5);
        const o=new THREE.Object3D();
        for(let i=0;i<5;i++){
          o.position.set(0,-4-i*(d.T-8)/4,0);
          o.rotation.set(0,i*2.4,0); o.rotateX(2.0);
          o.updateMatrix(); inst.setMatrixAt(i,o.matrix);
        }
        inst.instanceMatrix.needsUpdate=true; tibiaG.add(inst);
      }
      // tarsus + Haller's organ (pair I) + claws/pulvillus
      const tarsusG=new THREE.Group(); tarsusG.position.y=-d.T; tibiaG.add(tarsusG);
      tarsusG.add(segMesh(1.3,0.8,12,M.leg,8));
      if(pi===0){ // Haller's organ capsule: the CO2/heat sensor pit
        const cap=ball(1.1,M.haller,8,6); cap.position.set(0,-4,1.2); tarsusG.add(cap);
        for(let h=0;h<4;h++){
          const st=new THREE.Mesh(new THREE.ConeGeometry(0.25,2.6,4),M.hair);
          st.position.set(Math.cos(h*1.7)*1.4,-4+((h%2)*1.4),1.2+Math.sin(h*1.7)*1.4);
          st.rotation.x=0.9; tarsusG.add(st);
        }
      }
      const pre=new THREE.Group(); pre.position.y=-12; tarsusG.add(pre);
      for(const cs of [-1,1]){
        const cg=new THREE.Group(); cg.rotation.z=cs*0.45; pre.add(cg);
        cg.add(segMesh(0.7,0.4,4.5,M.dark,6));
        const tip=new THREE.Group(); tip.position.y=-4.5; tip.rotation.x=1.0; cg.add(tip);
        tip.add(segMesh(0.4,0.05,3.5,M.dark,6));
      }
      const pulv=ball(1.6,M.pale,8,6); pulv.scale.set(1,0.6,1.3);
      pulv.position.set(0,-1.5,0.5); pre.add(pulv);
      yawG.rotation.y=s*d.yaw;
      this.legs.push({root,yawG,femurG,tibiaG,tarsusG,pre,pulv,
        side:s,pair:pi,F:d.F,T:d.T,coxaLen:8,tarsusLen:14,
        neutralYaw:s*d.yaw,
        neutral:V3(s*(pi===0?46:pi===3?52:58),-24,pi===0?44:(pi===1?16:(pi===2?-16:-44))),
        wsR:new THREE.Vector3(13,0,12)});
    }
  }
  // ── venter: spiracles + anal groove ──────────────────────────────
  _buildVenter(){
    const M=this.M;
    this.spiracles=[];
    for(const s of [-1,1]){ // spiracular plate behind coxa IV
      const p=ball(4,M.spiracle,12,8); p.scale.set(1,0.35,1.4);
      p.position.set(s*24,-9,-34); this.body.add(p);
      const rim=new THREE.Mesh(new THREE.TorusGeometry(3.6,0.7,8,18),M.dark);
      rim.rotation.x=Math.PI/2; rim.scale.set(1,1.4,1);
      rim.position.set(s*24,-9.6,-34); this.body.add(rim);
      this.spiracles.push(p);
    }
    const groove=new THREE.Mesh(new THREE.TorusGeometry(7,0.8,8,20,Math.PI*1.2),M.dark);
    groove.rotation.set(Math.PI/2,0,Math.PI*0.9); // anterior anal groove (Ixodes key)
    groove.position.set(0,-11,-38); this.body.add(groove);
  }
  // ── engorgement: sac balloons, scutum does NOT ───────────────────
  setFill(f){
    this.fill=THREE.MathUtils.clamp(f,0,1);
    const F=this.fill;
    this.alloG.scale.set(1+0.85*F, 1+1.5*F, 1+1.25*F);
    const M=this.M;
    M.allo.color.lerpColors(new THREE.Color(0xffffff), new THREE.Color(0x9aa4b5), F*0.85);
    M.allo.roughness=0.5-0.25*F;
    M.allo.clearcoat=0.5+0.5*F;                 // stretched cuticle shines
    M.allo.bumpScale=0.9-0.7*F;                 // wrinkles smooth out
  }
  setCement(c){
    this.cement=c;
    this.cementMesh.scale.setScalar(Math.max(0.001,c));
    this.cementMesh.visible=c>0.02;
  }
  // ── micro-life ───────────────────────────────────────────────────
  updateMicro(dt,t){
    // spiracle pulse (respiration) + palp sway + setae shimmer
    const br=1+Math.sin(t*2.1)*0.06;
    for(const p of this.spiracles) p.scale.set(1*br,0.35,1.4);
    for(const p of this.palps){
      if(this.hypo<0.1){
        p.root.rotation.y=p.side*(0.12+Math.sin(t*0.8+p.side)*0.03);
        p.segs[3].rotation.x=Math.sin(t*1.3+p.side*2)*0.05;
      }
    }
  }
  anchors(out={}){
    const o=new THREE.Object3D();
    const at=(obj,x=0,y=0,z=0)=>{
      o.position.set(x,y,z); obj.updateWorldMatrix(true,false);
      return obj.localToWorld(o.position.clone());
    };
    out.scutum=at(this.body,0,16,16);
    out.alloscutum=at(this.alloG,0,20,-30);
    out.capitulum=at(this.capitulum,0,4,2);
    out.hypostome=at(this.hypostome,0,0,18*this.hypoSlide+4);
    out.chelicera=at(this.chelicerae[1].root,0,0,14);
    out.palp=at(this.palps[1].segs[2],0,0,5);
    out.haller=at(this.legs[4].tarsusG,0,-4,1.2);
    out.festoon=at(this.festoonG,20,0.5,-52);
    out.spiracle=at(this.body,24,-9,-34);
    out.claw=at(this.legs[6].pre,0,-4,0);
    out.cement=at(this.capitulum,0,-2,6);
    out.coxa=at(this.legs[4].root,0,-6,0);
    return out;
  }
}

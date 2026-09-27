// ═══════════════════════════════════════════════════════════════════
// world.js — terrain, deck, player fuel tank, blood dish, wall rig
// Any collidable surface can be located & attached to (smooth or rough).
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

// ── seeded value noise ─────────────────────────────────────────────
function makeNoise(seed=7){
  const p=new Uint8Array(512);
  let s=seed>>>0 || 1;
  const rnd=()=> (s=(s*1664525+1013904223)>>>0, s/4294967296);
  const perm=[...Array(256).keys()];
  for(let i=255;i>0;i--){ const j=(rnd()*(i+1))|0; [perm[i],perm[j]]=[perm[j],perm[i]]; }
  for(let i=0;i<512;i++) p[i]=perm[i&255];
  const fade=t=>t*t*(3-2*t);
  function n2(x,y){
    const xi=Math.floor(x)&255, yi=Math.floor(y)&255;
    const xf=x-Math.floor(x), yf=y-Math.floor(y);
    const h=(X,Y)=>p[(p[(X)&255]+(Y))&255]/255;
    const u=fade(xf), v=fade(yf);
    return h(xi,yi)*(1-u)*(1-v)+h(xi+1,yi)*u*(1-v)+h(xi,yi+1)*(1-u)*v+h(xi+1,yi+1)*u*v;
  }
  return (x,y)=>n2(x,y)*0.55+n2(x*2.3+9,y*2.3+3)*0.27+n2(x*5.1+31,y*5.1+17)*0.18;
}
function ctex(w,h,draw){
  const c=document.createElement('canvas'); c.width=w; c.height=h;
  draw(c.getContext('2d'),w,h);
  const t=new THREE.CanvasTexture(c); t.colorSpace=THREE.SRGBColorSpace; t.anisotropy=4;
  return t;
}

export class World{
  constructor(scene, renderer){
    this.scene=scene; this.renderer=renderer;
    this.collidables=[];
    this.tankLevel=1; this.dishLevel=1;
    this.noise=makeNoise(1337);
    this._buildEnv();
    this._buildLights();
    this._buildTerrain();
    this._buildDeck();
    this._buildTank();
    this._buildDish();
    this._buildWallRig();
    this._buildScatter();
    this._buildDust();
  }
  addCollider(mesh, surf, rough){
    mesh.userData.surf=surf; mesh.userData.rough=rough;
    mesh.receiveShadow=true;
    this.collidables.push(mesh);
    return mesh;
  }
  // ── environment reflections (procedural studio equirect) ─────────
  _buildEnv(){
    const tex=ctex(512,256,(x,w,h)=>{
      const g=x.createLinearGradient(0,0,0,h);
      g.addColorStop(0,'#0a1226'); g.addColorStop(0.45,'#101725');
      g.addColorStop(0.55,'#05070a'); g.addColorStop(1,'#020304');
      x.fillStyle=g; x.fillRect(0,0,w,h);
      // warm key blob + cool rim blob
      const blob=(cx,cy,r,col)=>{
        const rg=x.createRadialGradient(cx,cy,0,cx,cy,r);
        rg.addColorStop(0,col); rg.addColorStop(1,'rgba(0,0,0,0)');
        x.fillStyle=rg; x.fillRect(0,0,w,h);
      };
      blob(w*0.68,h*0.30,90,'rgba(255,214,170,0.95)');
      blob(w*0.20,h*0.35,70,'rgba(111,151,255,0.8)');
      blob(w*0.45,h*0.52,140,'rgba(60,70,90,0.35)');
    });
    tex.mapping=THREE.EquirectangularReflectionMapping;
    const pm=new THREE.PMREMGenerator(this.renderer);
    this.scene.environment=pm.fromEquirectangular(tex).texture;
    this.scene.background=new THREE.Color(0x05070a);
    this.scene.fog=new THREE.FogExp2(0x05070a, 0.0042);
    tex.dispose(); pm.dispose();
  }
  _buildLights(){
    this.scene.add(new THREE.HemisphereLight(0x8fb0ff, 0x1a1410, 0.5));
    this.key=new THREE.DirectionalLight(0xffe0b8, 2.4);
    this.key.position.set(60,95,45);
    this.key.castShadow=true;
    this.key.shadow.mapSize.set(2048,2048);
    const c=this.key.shadow.camera;
    c.left=-45; c.right=45; c.top=45; c.bottom=-45; c.near=20; c.far=260;
    this.key.shadow.bias=-0.0004; this.key.shadow.normalBias=0.02;
    this.scene.add(this.key); this.scene.add(this.key.target);
    this.rim=new THREE.DirectionalLight(0x6f97ff, 1.5);
    this.rim.position.set(-70,40,-60); this.scene.add(this.rim);
    this.tankGlow=new THREE.PointLight(0xffb454, 60, 90, 2);
    this.tankGlow.position.set(18,26,-8); this.scene.add(this.tankGlow);
  }
  groundH(x,z){
    const n=this.noise(x*0.02+7, z*0.02+3);
    let h=(n-0.42)*26-3.2;
    // flatten under deck
    const dx=Math.max(0,Math.abs(x)-38), dz=Math.max(0,Math.abs(z)-28);
    const d=Math.hypot(dx,dz), k=THREE.MathUtils.smoothstep(d,0,26);
    return h*k + (-2.0)*(1-k);
  }
  _buildTerrain(){
    const S=280, N=130;
    const g=new THREE.PlaneGeometry(S,S,N,N);
    g.rotateX(-Math.PI/2);
    const pos=g.attributes.position, col=[];
    const cSoil=new THREE.Color(0x2b2117), cMoss=new THREE.Color(0x3d4c26),
          cRock=new THREE.Color(0x4a4a4e), cSand=new THREE.Color(0x5a4c33), tmp=new THREE.Color();
    for(let i=0;i<pos.count;i++){
      const x=pos.getX(i), z=pos.getZ(i);
      const h=this.groundH(x,z);
      pos.setY(i,h);
      const m=this.noise(x*0.05+40,z*0.05+9);
      const r=this.noise(x*0.012+90,z*0.012+50);
      tmp.copy(cSoil);
      if(m>0.52) tmp.lerp(cMoss, Math.min(1,(m-0.52)*4));
      if(r<0.38) tmp.lerp(cRock, Math.min(1,(0.38-r)*5));
      if(h<-6) tmp.lerp(cSand, 0.4);
      tmp.offsetHSL(0,0,(this.noise(x*0.4,z*0.4)-0.5)*0.05);
      col.push(tmp.r,tmp.g,tmp.b);
    }
    g.setAttribute('color', new THREE.Float32BufferAttribute(col,3));
    g.computeVertexNormals();
    const m=new THREE.Mesh(g, new THREE.MeshStandardMaterial({vertexColors:true, roughness:1, metalness:0}));
    m.castShadow=true;
    this.scene.add(m); this.addCollider(m,'soil',0.95);
    this.terrain=m;
  }
  _buildDeck(){
    const grp=new THREE.Group(); this.scene.add(grp);
    const plate=new THREE.Mesh(new THREE.BoxGeometry(76,4,56),
      new THREE.MeshStandardMaterial({color:0x23262c, roughness:0.5, metalness:0.75}));
    plate.position.set(0,-2,0); plate.castShadow=true; grp.add(plate);
    this.addCollider(plate,'deck',0.55);
    // edge glow strips
    const stripMat=new THREE.MeshBasicMaterial({color:0x2a4a8a});
    for(const [w,d,x,z] of [[76,0.5,0,28.2],[76,0.5,0,-28.2],[0.5,56,38.2,0],[0.5,56,-38.2,0]]){
      const s=new THREE.Mesh(new THREE.BoxGeometry(w,0.25,d), stripMat);
      s.position.set(x,0.02,z); grp.add(s);
    }
    // anti-slip dots
    const dotG=new THREE.CylinderGeometry(0.32,0.32,0.12,8);
    const dotM=new THREE.MeshStandardMaterial({color:0x2e3238, roughness:0.7, metalness:0.6});
    const dots=new THREE.InstancedMesh(dotG,dotM,120);
    const o=new THREE.Object3D(); let k=0;
    for(let ix=0;ix<12&&k<120;ix++)for(let iz=0;iz<10&&k<120;iz++){
      o.position.set(-33+ix*6, 0.02, -25+iz*5.5);
      if(Math.hypot(o.position.x-18,o.position.z+8)<16) continue; // under tank
      if(Math.hypot(o.position.x+16,o.position.z-10)<10) continue; // under dish
      o.updateMatrix(); dots.setMatrixAt(k++,o.matrix);
    }
    dots.count=k; dots.instanceMatrix.needsUpdate=true; grp.add(dots);
  }
  _buildTank(){
    const G=this.tankG=new THREE.Group(); G.position.set(20,0,-9); this.scene.add(G);
    const labelTex=ctex(512,256,(x,w,h)=>{
      x.fillStyle='#2b2e33'; x.fillRect(0,0,w,h);
      // brushed streaks
      for(let i=0;i<400;i++){ x.fillStyle=`rgba(255,255,255,${Math.random()*0.03})`;
        x.fillRect(0,Math.random()*h,w,1); }
      x.fillStyle='#c8921e'; x.fillRect(0,30,w,54);
      x.fillStyle='#111'; x.font='bold 34px system-ui,sans-serif'; x.textAlign='center';
      x.fillText('FRONTIER · FUEL 100LL', w/2, 68);
      x.fillStyle='#dfe6f2'; x.font='bold 26px system-ui,sans-serif';
      x.fillText('PLAYER TANK — 01', w/2, 140);
      x.fillStyle='#8a8f98'; x.font='20px system-ui,sans-serif';
      x.fillText('AVGAS · 42 L · NO STEP', w/2, 175);
      // hazard chevrons
      for(let i=0;i<16;i++){ x.fillStyle=i%2?'#c8921e':'#15161a';
        x.save(); x.translate(i*32,210); x.rotate(0); x.fillRect(0,0,32,46); x.restore(); }
    });
    const paintM=new THREE.MeshPhysicalMaterial({map:labelTex, roughness:0.28, metalness:0.35,
      clearcoat:0.8, clearcoatRoughness:0.25});
    const steelM=new THREE.MeshStandardMaterial({color:0x8f959e, roughness:0.3, metalness:0.9});
    const body=new THREE.Mesh(new THREE.CylinderGeometry(13,13,30,40), paintM);
    body.position.y=15; body.castShadow=true; G.add(body);
    this.addCollider(body,'tank',0.12);
    const dome=new THREE.Mesh(new THREE.SphereGeometry(13,32,12,0,Math.PI*2,0,Math.PI/2), steelM);
    dome.position.y=30; dome.castShadow=true; G.add(dome);
    this.addCollider(dome,'tank',0.12);
    const base=new THREE.Mesh(new THREE.CylinderGeometry(14,15,2.4,40),
      new THREE.MeshStandardMaterial({color:0x1c1e22,roughness:0.6,metalness:0.6}));
    base.position.y=1.2; base.castShadow=true; G.add(base);
    // top valve + beacon
    const valve=new THREE.Mesh(new THREE.CylinderGeometry(1.6,2.0,3.4,12), steelM);
    valve.position.y=44.2; valve.castShadow=true; G.add(valve);
    this.beacon=new THREE.Mesh(new THREE.SphereGeometry(0.8,12,10),
      new THREE.MeshBasicMaterial({color:0xff2222}));
    this.beacon.position.y=46.6; G.add(this.beacon);
    // sight glass with live fuel column
    const glass=new THREE.Mesh(new THREE.CylinderGeometry(1.1,1.1,20,14,1,true),
      new THREE.MeshPhysicalMaterial({color:0xbcd2ff, transparent:true, opacity:0.22,
        roughness:0.05, metalness:0, clearcoat:1}));
    glass.position.set(-13.4,15,4); G.add(glass);
    this.fuelCol=new THREE.Mesh(new THREE.CylinderGeometry(0.8,0.8,1,12),
      new THREE.MeshStandardMaterial({color:0xff9a1e, emissive:0xcc5e00,
        emissiveIntensity:0.9, roughness:0.3}));
    this.fuelCol.position.set(-13.4,6,4); G.add(this.fuelCol);
    this._layoutFuel();
    // feed port (hatch) facing deck centre
    const dir=new THREE.Vector3(-20,0,9).normalize();
    this.portDir=dir;
    const port=new THREE.Group();
    port.position.copy(dir).multiplyScalar(13.05); port.position.y=9;
    port.lookAt(port.position.clone().add(dir));
    const ring=new THREE.Mesh(new THREE.TorusGeometry(2.2,0.5,10,24), steelM);
    ring.castShadow=true; port.add(ring);
    const hatch=new THREE.Mesh(new THREE.CircleGeometry(2.0,24),
      new THREE.MeshStandardMaterial({color:0x14161a, roughness:0.4, metalness:0.7}));
    port.add(hatch);
    const seam=new THREE.Mesh(new THREE.TorusGeometry(0.55,0.14,8,20),
      new THREE.MeshStandardMaterial({color:0xc8921e, roughness:0.4, metalness:0.5}));
    seam.position.z=0.1; port.add(seam);
    G.add(port);
    const wp=new THREE.Vector3(); port.getWorldPosition(wp);
    this.feedFuel={point:wp, normal:dir.clone()};
  }
  _layoutFuel(){
    const h=18*this.tankLevel;
    this.fuelCol.scale.y=Math.max(0.001,h);
    this.fuelCol.position.y=6+h/2;
    this.fuelCol.visible=h>0.05;
  }
  setTank(v){ this.tankLevel=THREE.MathUtils.clamp(v,0,1); this._layoutFuel(); }
  _buildDish(){
    const G=new THREE.Group(); G.position.set(-17,0,11); this.scene.add(G);
    const glassM=new THREE.MeshPhysicalMaterial({color:0xd8e4f2, transparent:true, opacity:0.3,
      roughness:0.05, clearcoat:1});
    const base=new THREE.Mesh(new THREE.CylinderGeometry(8,8,1.6,32), glassM);
    base.position.y=0.8; base.castShadow=true; G.add(base);
    this.bloodM=new THREE.MeshPhysicalMaterial({color:0x8a0f0f, roughness:0.12,
      clearcoat:1, clearcoatRoughness:0.1, emissive:0x400404, emissiveIntensity:0.7});
    this.blood=new THREE.Mesh(new THREE.CylinderGeometry(7.1,7.1,0.5,32), this.bloodM);
    this.blood.position.y=1.5; G.add(this.blood);
    this.addCollider(this.blood,'blood',0.05);
    const rim=new THREE.Mesh(new THREE.TorusGeometry(8,0.5,10,40),
      new THREE.MeshStandardMaterial({color:0xb9c2cc, roughness:0.3, metalness:0.85}));
    rim.rotation.x=Math.PI/2; rim.position.y=1.6; rim.castShadow=true; G.add(rim);
    const wp=new THREE.Vector3(); this.blood.getWorldPosition(wp); wp.y=1.75;
    this.feedBlood={point:wp, normal:new THREE.Vector3(0,1,0)};
  }
  setDish(v){
    this.dishLevel=THREE.MathUtils.clamp(v,0,1);
    this.blood.scale.set(Math.max(0.15,Math.sqrt(this.dishLevel)),1,Math.max(0.15,Math.sqrt(this.dishLevel)));
    this.blood.position.y=1.5-0.3*(1-this.dishLevel);
  }
  _buildWallRig(){
    const wallTex=ctex(512,512,(x,w,h)=>{
      x.fillStyle='#26292f'; x.fillRect(0,0,w,h);
      for(let i=0;i<500;i++){ x.fillStyle=`rgba(255,255,255,${Math.random()*0.025})`;
        x.fillRect(Math.random()*w,0,1,h); }
      x.fillStyle='#c8921e'; x.font='bold 44px system-ui,sans-serif'; x.textAlign='center';
      x.fillText('SECTOR 7', w/2, 120);
      x.fillStyle='rgba(200,146,30,0.5)'; x.fillRect(0,h-40,w,12);
      x.fillStyle='#101215';
      for(let rx=30;rx<w;rx+=90)for(let ry=30;ry<h;ry+=90){
        x.beginPath(); x.arc(rx,ry,7,0,7); x.fill(); }
    });
    const wallM=new THREE.MeshStandardMaterial({map:wallTex, roughness:0.55, metalness:0.6});
    const wall=new THREE.Mesh(new THREE.BoxGeometry(84,58,2.5), wallM);
    wall.position.set(0,29,-40); wall.castShadow=true; this.scene.add(wall);
    this.addCollider(wall,'wall',0.5);
    // ceiling slab jutting from wall top (walk underneath ✓)
    const ceil=new THREE.Mesh(new THREE.BoxGeometry(84,2.5,26), wallM);
    ceil.position.set(0,58,-27); ceil.castShadow=true; this.scene.add(ceil);
    this.addCollider(ceil,'ceiling',0.5);
    // support columns
    const colM=new THREE.MeshStandardMaterial({color:0x1c1e22, roughness:0.6, metalness:0.6});
    for(const sx of [-30,30]){
      const c=new THREE.Mesh(new THREE.BoxGeometry(4,58,4), colM);
      c.position.set(sx,29,-15); c.castShadow=true; this.scene.add(c);
      this.addCollider(c,'wall',0.5);
    }
    // uneven rock spire (rough attach demo)
    const rockG=new THREE.DodecahedronGeometry(11,2);
    const rp=rockG.attributes.position;
    for(let i=0;i<rp.count;i++){
      const f=1+(this.noise(rp.getX(i)*0.3,rp.getZ(i)*0.3+rp.getY(i))-0.5)*0.9;
      rp.setXYZ(i, rp.getX(i)*f, rp.getY(i)*1.5*f, rp.getZ(i)*f);
    }
    rockG.computeVertexNormals();
    this.spire=new THREE.Mesh(rockG, new THREE.MeshStandardMaterial({color:0x4c4a45, roughness:1}));
    this.spire.position.set(-52,4,26); this.spire.castShadow=true; this.scene.add(this.spire);
    this.addCollider(this.spire,'rock',1.0);
  }
  _buildScatter(){
    const rockM=new THREE.MeshStandardMaterial({color:0x55524b, roughness:1});
    const rocks=new THREE.InstancedMesh(new THREE.DodecahedronGeometry(1,0), rockM, 90);
    const o=new THREE.Object3D(); let k=0;
    for(let i=0;i<400&&k<90;i++){
      const x=(Math.random()-0.5)*250, z=(Math.random()-0.5)*250;
      if(Math.abs(x)<42&&Math.abs(z)<32) continue;
      o.position.set(x,this.groundH(x,z)+0.2,z);
      o.rotation.set(Math.random()*3,Math.random()*3,Math.random()*3);
      const s=0.5+Math.random()*2.4; o.scale.set(s,s*(0.6+Math.random()*0.5),s);
      o.updateMatrix(); rocks.setMatrixAt(k++,o.matrix);
    }
    rocks.count=k; rocks.instanceMatrix.needsUpdate=true;
    rocks.castShadow=true; rocks.receiveShadow=true; this.scene.add(rocks);
    // grass tufts
    const bladeG=new THREE.ConeGeometry(0.16,2.6,4); bladeG.translate(0,1.3,0);
    const bladeM=new THREE.MeshStandardMaterial({color:0x44552a, roughness:1});
    const grass=new THREE.InstancedMesh(bladeG,bladeM,420);
    k=0;
    for(let i=0;i<2000&&k<420;i++){
      const x=(Math.random()-0.5)*240, z=(Math.random()-0.5)*240;
      if(Math.abs(x)<42&&Math.abs(z)<32) continue;
      const m=this.noise(x*0.05+40,z*0.05+9);
      if(m<0.54) continue;
      o.position.set(x,this.groundH(x,z),z);
      o.rotation.set((Math.random()-0.5)*0.5,Math.random()*3,(Math.random()-0.5)*0.5);
      const s=0.7+Math.random()*1.3; o.scale.set(s,s,s);
      o.updateMatrix(); grass.setMatrixAt(k++,o.matrix);
    }
    grass.count=k; grass.instanceMatrix.needsUpdate=true; this.scene.add(grass);
  }
  _buildDust(){
    const N=260, pos=new Float32Array(N*3);
    for(let i=0;i<N;i++){
      pos[i*3]=(Math.random()-0.5)*160;
      pos[i*3+1]=Math.random()*60+1;
      pos[i*3+2]=(Math.random()-0.5)*160;
    }
    const g=new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos,3));
    this.dust=new THREE.Points(g, new THREE.PointsMaterial({color:0x8fa5c8, size:0.35,
      transparent:true, opacity:0.4, depthWrite:false, blending:THREE.AdditiveBlending}));
    this.scene.add(this.dust);
  }
  followShadow(target){
    this.key.position.set(target.x+60, target.y+95, target.z+45);
    this.key.target.position.copy(target);
  }
  update(dt,t){
    // dust drift
    this.dust.rotation.y+=dt*0.008;
    // beacon blink
    this.beacon.material.color.setHex((t%1.4<0.15)?0xff2222:0x440a0a);
    // blood shimmer
    this.bloodM.emissiveIntensity=0.65+Math.sin(t*2.2)*0.1;
  }
}

// ═══════════════════════════════════════════════════════════════════
// world.js — pit-lane diorama: GT rear clip, duct tunnel (cutaway),
// live battery cells, quest perch. Units: 1 = 1 mm.
// ═══════════════════════════════════════════════════════════════════
import * as THREE from '../vendor/three.module.js';

const V3=(x,y,z)=>new THREE.Vector3(x,y,z);
function ctex(w,h,draw){
  const c=document.createElement('canvas'); c.width=w; c.height=h;
  draw(c.getContext('2d'),w,h);
  const t=new THREE.CanvasTexture(c); t.colorSpace=THREE.SRGBColorSpace; t.anisotropy=8;
  return t;
}
function box(w,h,d,mat){
  const m=new THREE.Mesh(new THREE.BoxGeometry(w,h,d),mat);
  m.castShadow=true; m.receiveShadow=true; return m;
}

export class World{
  constructor(scene,renderer){
    this.scene=scene; this.renderer=renderer;
    this.collidables=[];
    this.battery=1; this.idle=false;
    this._buildEnv();
    this._buildLights();
    this._buildGround();
    this._buildCar();
    this._buildDuct();
    this._buildPerch();
    this._buildDust();
    this.puffs=[];
  }
  addCollider(mesh,surf,rough){
    mesh.userData.surf=surf; mesh.userData.rough=rough;
    mesh.receiveShadow=true; this.collidables.push(mesh); return mesh;
  }
  _buildEnv(){
    const tex=ctex(512,256,(x,w,h)=>{
      const g=x.createLinearGradient(0,0,0,h);
      g.addColorStop(0,'#0c1428'); g.addColorStop(0.45,'#101725');
      g.addColorStop(0.55,'#05070a'); g.addColorStop(1,'#020304');
      x.fillStyle=g; x.fillRect(0,0,w,h);
      const blob=(cx,cy,r,col)=>{
        const rg=x.createRadialGradient(cx,cy,0,cx,cy,r);
        rg.addColorStop(0,col); rg.addColorStop(1,'rgba(0,0,0,0)');
        x.fillStyle=rg; x.fillRect(0,0,w,h);
      };
      blob(w*0.68,h*0.30,90,'rgba(255,214,170,0.95)');
      blob(w*0.20,h*0.35,70,'rgba(111,151,255,0.8)');
    });
    tex.mapping=THREE.EquirectangularReflectionMapping;
    const pm=new THREE.PMREMGenerator(this.renderer);
    this.scene.environment=pm.fromEquirectangular(tex).texture;
    this.scene.background=new THREE.Color(0x05070a);
    this.scene.fog=new THREE.FogExp2(0x05070a,0.00045);
    tex.dispose(); pm.dispose();
  }
  _buildLights(){
    this.scene.add(new THREE.HemisphereLight(0x8fb0ff,0x1a1410,0.55));
    this.key=new THREE.DirectionalLight(0xffe0b8,2.4);
    this.key.position.set(600,950,450);
    this.key.castShadow=true;
    this.key.shadow.mapSize.set(2048,2048);
    const c=this.key.shadow.camera;
    c.left=-400; c.right=400; c.top=400; c.bottom=-400; c.near=200; c.far=2600;
    this.key.shadow.bias=-0.0004; this.key.shadow.normalBias=0.5;
    this.scene.add(this.key); this.scene.add(this.key.target);
    this.rim=new THREE.DirectionalLight(0x6f97ff,1.4);
    this.rim.position.set(-700,400,-600); this.scene.add(this.rim);
    this.ductLight=new THREE.PointLight(0x66e0ff,900,700,2);
    this.ductLight.position.set(560,330,-120); this.scene.add(this.ductLight);
    this.underglow=new THREE.PointLight(0x2266ff,2500,1400,2);
    this.underglow.position.set(0,60,-500); this.scene.add(this.underglow);
  }
  groundH(x,z){ return 0; }  // flat pad (camera collision)
  _buildGround(){
    // asphalt pit pad with painted box
    const padTex=ctex(1024,1024,(x,w,h)=>{
      x.fillStyle='#232428'; x.fillRect(0,0,w,h);
      for(let i=0;i<9000;i++){ const g=28+Math.random()*22|0;
        x.fillStyle=`rgb(${g},${g},${g+3})`; x.fillRect(Math.random()*w,Math.random()*h,2,2); }
      x.strokeStyle='rgba(230,235,245,0.85)'; x.lineWidth=10;
      x.strokeRect(90,90,w-180,h-180);
      x.fillStyle='rgba(200,146,30,0.9)'; x.font='bold 72px system-ui,sans-serif';
      x.textAlign='center'; x.fillText('PIT 07',w/2,h-140);
      x.fillStyle='rgba(200,146,30,0.55)';
      for(let i=0;i<6;i++) x.fillRect(120+i*130,150,70,26);
    });
    const pad=new THREE.Mesh(new THREE.PlaneGeometry(2400,1700),
      new THREE.MeshStandardMaterial({map:padTex,roughness:0.94,metalness:0}));
    pad.rotation.x=-Math.PI/2; pad.position.set(0,0,-100);
    pad.receiveShadow=true; this.scene.add(pad);
    this.addCollider(pad,'asphalt',0.9);
    // surrounding grass
    const grassTex=ctex(512,512,(x,w,h)=>{
      x.fillStyle='#2c3a1e'; x.fillRect(0,0,w,h);
      for(let i=0;i<6000;i++){ const g=40+Math.random()*40|0;
        x.fillStyle=`rgb(${g*0.6|0},${g},${g*0.4|0})`;
        x.fillRect(Math.random()*w,Math.random()*h,2,3); }
    });
    grassTex.wrapS=grassTex.wrapT=THREE.RepeatWrapping; grassTex.repeat.set(10,10);
    const grass=new THREE.Mesh(new THREE.PlaneGeometry(7000,7000),
      new THREE.MeshStandardMaterial({map:grassTex,roughness:1}));
    grass.rotation.x=-Math.PI/2; grass.position.y=-2; grass.receiveShadow=true;
    this.scene.add(grass);
  }
  _buildCar(){
    this.carMats={
      paint:new THREE.MeshPhysicalMaterial({color:0x16307a,roughness:0.3,metalness:0.55,
        clearcoat:1,clearcoatRoughness:0.15}),
      trim:new THREE.MeshStandardMaterial({color:0x14161a,roughness:0.6,metalness:0.4}),
      carbon:new THREE.MeshStandardMaterial({color:0x0c0d10,roughness:0.45,metalness:0.6}),
      rubber:new THREE.MeshStandardMaterial({color:0x0a0a0b,roughness:0.95}),
      rim:new THREE.MeshStandardMaterial({color:0xb9c2cc,roughness:0.25,metalness:0.95}),
      steel:new THREE.MeshStandardMaterial({color:0x8f959e,roughness:0.3,metalness:0.9}),
    };
    const M=this.carMats, G=this.carG=new THREE.Group(); this.scene.add(G);
    // rear bumper + valance
    const bumper=box(1700,300,230,M.paint); bumper.position.set(0,300,-115); G.add(bumper);
    this.addCollider(bumper,'body',0.3);
    const valance=box(1500,90,200,M.carbon); valance.position.set(0,105,-100); G.add(valance);
    // tail panel + light bar (dims with battery)
    const tail=box(1600,150,60,M.paint); tail.position.set(0,490,-60); G.add(tail);
    this.tailM=new THREE.MeshStandardMaterial({color:0x550000,emissive:0xff1a1a,
      emissiveIntensity:2.2,roughness:0.2});
    const bar=box(1480,52,20,this.tailM); bar.position.set(0,500,-22); G.add(bar);
    const plate=box(220,55,8,new THREE.MeshStandardMaterial({color:0xdfe6f2,roughness:0.4}));
    plate.position.set(0,330,2); G.add(plate);
    // diffuser + fins
    const diff=box(900,70,190,M.carbon); diff.position.set(0,75,-40); G.add(diff);
    for(let i=-2;i<=2;i++){
      const fin=box(14,90,200,M.carbon); fin.position.set(i*150,80,-30); G.add(fin);
    }
    // exhausts
    this.exhausts=[];
    for(const s of [-1,1]){
      const tip=new THREE.Mesh(new THREE.CylinderGeometry(34,38,90,20),M.steel);
      tip.rotation.x=Math.PI/2; tip.position.set(s*230,150,-10);
      tip.castShadow=true; G.add(tip); this.exhausts.push(tip);
      const dark=new THREE.Mesh(new THREE.CircleGeometry(30,20),
        new THREE.MeshBasicMaterial({color:0x000000}));
      dark.position.set(s*230,150,36); G.add(dark);
    }
    // rear fenders + deck + wing
    for(const s of [-1,1]){
      const fender=box(420,260,640,M.paint); fender.position.set(s*640,470,-420); G.add(fender);
      this.addCollider(fender,'body',0.3);
      const skirt=box(360,120,700,M.carbon); skirt.position.set(s*660,120,-420); G.add(skirt);
    }
    const deck=box(1100,90,620,M.paint); deck.position.set(0,600,-420); G.add(deck);
    for(const s of [-1,1]){
      const py=box(40,150,200,M.carbon); py.position.set(s*380,700,-620); G.add(py);
    }
    const wing=box(1250,36,260,M.carbon); wing.position.set(0,790,-620); G.add(wing);
    for(const s of [-1,1]){
      const ep=box(20,120,280,M.carbon); ep.position.set(s*625,790,-620); G.add(ep);
    }
    // rear wheels
    const treadTex=ctex(256,64,(x,w,h)=>{
      x.fillStyle='#0a0a0b'; x.fillRect(0,0,w,h);
      x.fillStyle='#1c1d20';
      for(let i=0;i<16;i++){ x.save(); x.translate(i*16,0); x.rotate(0.4); x.fillRect(0,-10,7,90); x.restore(); }
    });
    treadTex.wrapS=treadTex.wrapT=THREE.RepeatWrapping; treadTex.repeat.set(6,1);
    const tireM=new THREE.MeshStandardMaterial({map:treadTex,roughness:0.95});
    for(const s of [-1,1]){
      const W=new THREE.Group(); W.position.set(s*780,330,-420); G.add(W);
      const tire=new THREE.Mesh(new THREE.CylinderGeometry(330,330,300,36),tireM);
      tire.rotation.z=Math.PI/2; tire.castShadow=true; W.add(tire);
      this.addCollider(tire,'tire',0.85);
      const rim=new THREE.Mesh(new THREE.CylinderGeometry(190,190,310,24),M.rim);
      rim.rotation.z=Math.PI/2; rim.castShadow=true; W.add(rim);
      for(let sp=0;sp<5;sp++){
        const spoke=box(60,360,320,M.rim);
        spoke.rotation.x=sp/5*Math.PI*2; W.add(spoke);
      }
      const cap=new THREE.Mesh(new THREE.CylinderGeometry(50,50,330,16),M.carbon);
      cap.rotation.z=Math.PI/2; W.add(cap);
      const arch=new THREE.Mesh(new THREE.TorusGeometry(360,60,10,20,Math.PI),M.paint);
      arch.position.set(s*780,330,-420); arch.rotation.y=Math.PI/2; G.add(arch);
    }
    // FRONTIER badge
    const badgeTex=ctex(512,64,(x,w,h)=>{
      x.fillStyle='rgba(0,0,0,0)'; x.fillRect(0,0,w,h);
      x.fillStyle='#dfe6f2'; x.font='bold 44px system-ui,sans-serif'; x.textAlign='center';
      x.fillText('F R O N T I E R   G T',w/2,46);
    });
    const badge=new THREE.Mesh(new THREE.PlaneGeometry(500,62),
      new THREE.MeshBasicMaterial({map:badgeTex,transparent:true}));
    badge.position.set(0,420,2); G.add(badge);
  }
  _buildDuct(){
    const M=this.carMats;
    const DX=560, FLoorY=205, mouthZ=0, endZ=-260;
    // intake frame (both sides cosmetic; right side is the breach)
    for(const s of [-1,1]){
      const frame=new THREE.Mesh(new THREE.TorusGeometry(62,12,10,4),M.carbon);
      frame.position.set(s*DX,260,4); frame.rotation.z=Math.PI/4;
      frame.scale.set(1.2,0.9,1); frame.castShadow=true;
      this.carG.add(frame);
    }
    // dark recess left
    const recess=box(140,100,60,new THREE.MeshBasicMaterial({color:0x020203}));
    recess.position.set(-DX,260,-32); this.carG.add(recess);
    // RIGHT tunnel: floor + walls + partial roof (cutaway — camera sees in)
    const tunM=new THREE.MeshStandardMaterial({color:0x1a1d22,roughness:0.7,metalness:0.3});
    const floor=box(150,14,endZ*-1+20,tunM);
    floor.position.set(DX,FLoorY-7,-120); this.carG.add(floor);
    this.addCollider(floor,'duct',0.4);
    for(const s of [-1,1]){
      const wall=box(12,110,280,tunM);
      wall.position.set(DX+s*75,FLoorY+55,-120); this.carG.add(wall);
    }
    const roof=box(150,12,120,tunM);           // roof only over dark section
    roof.position.set(DX,FLoorY+116,-200); this.carG.add(roof);
    // glowing guide ribs in tunnel
    const ribM=new THREE.MeshBasicMaterial({color:0x1a5a78});
    for(let i=0;i<4;i++){
      const rib=box(150,6,8,ribM);
      rib.position.set(DX,FLoorY+2,-30-i*40); this.carG.add(rib);
    }
    // battery tray + 4 cells (the prize)
    const tray=box(130,16,120,M.carbon);
    tray.position.set(DX,FLoorY+8,-120); this.carG.add(tray);
    this.cellM=new THREE.MeshStandardMaterial({color:0x0a2a33,emissive:0x22d3ee,
      emissiveIntensity:2.4,roughness:0.25});
    this.cells=[];
    for(let i=0;i<4;i++){
      const cell=new THREE.Mesh(new THREE.CylinderGeometry(20,20,62,16),this.cellM);
      cell.position.set(DX-39+i*26,FLoorY+47,-120);
      cell.castShadow=true; this.carG.add(cell); this.cells.push(cell);
    }
    this.addCollider(this.cells[1],'cell',0.2);
    const ct=new THREE.Vector3(); this.cells[1].getWorldPosition(ct); ct.y+=31;
    this.feedCell={point:ct,normal:V3(0,1,0)};
    this.duct={mouth:V3(DX,FLoorY+24,6), mouthN:V3(0,0,1), DX, FLoorY};
  }
  setBattery(v){
    this.battery=THREE.MathUtils.clamp(v,0,1);
    const b=this.battery;
    this.cellM.emissiveIntensity=0.05+2.35*b;
    this.tailM.emissiveIntensity=0.02+2.2*b;
    this.ductLight.intensity=50+850*b;
    this.underglow.intensity=100+2400*b;
  }
  setIdle(on){ this.idle=on; }
  _buildPerch(){
    // quest rock + tall grass at trackside
    const rockM=new THREE.MeshStandardMaterial({color:0x4c4a45,roughness:1});
    const rock=new THREE.Mesh(new THREE.DodecahedronGeometry(46,1),rockM);
    rock.position.set(250,14,430); rock.scale.set(1,0.55,1);
    rock.castShadow=true; rock.receiveShadow=true; this.scene.add(rock);
    this.addCollider(rock,'rock',1.0);
    this.perchTop=V3(250,42,430);
    const bladeG=new THREE.ConeGeometry(2.2,90,5); bladeG.translate(0,45,0);
    const bladeM=new THREE.MeshStandardMaterial({color:0x44552a,roughness:1});
    const grass=new THREE.InstancedMesh(bladeG,bladeM,60);
    const o=new THREE.Object3D();
    for(let i=0;i<60;i++){
      const a=Math.random()*6.28, r=20+Math.random()*90;
      o.position.set(250+Math.cos(a)*r,0,430+Math.sin(a)*r);
      o.rotation.set((Math.random()-0.5)*0.4,Math.random()*3,(Math.random()-0.5)*0.4);
      const s=0.6+Math.random()*0.9; o.scale.set(s,s,s);
      o.updateMatrix(); grass.setMatrixAt(i,o.matrix);
    }
    grass.instanceMatrix.needsUpdate=true; this.scene.add(grass);
  }
  _buildDust(){
    const N=200,pos=new Float32Array(N*3);
    for(let i=0;i<N;i++){
      pos[i*3]=(Math.random()-0.5)*2600;
      pos[i*3+1]=Math.random()*500+5;
      pos[i*3+2]=(Math.random()-0.5)*2600-100;
    }
    const g=new THREE.BufferGeometry();
    g.setAttribute('position',new THREE.BufferAttribute(pos,3));
    this.dust=new THREE.Points(g,new THREE.PointsMaterial({color:0x8fa5c8,size:6,
      transparent:true,opacity:0.35,depthWrite:false,blending:THREE.AdditiveBlending}));
    this.scene.add(this.dust);
    // spark pool (feeding arcs)
    const SN=120,sp=new Float32Array(SN*3);
    const sg=new THREE.BufferGeometry();
    sg.setAttribute('position',new THREE.BufferAttribute(sp,3));
    this.sparks=new THREE.Points(sg,new THREE.PointsMaterial({color:0x9be8ff,size:5,
      transparent:true,opacity:0.9,depthWrite:false,blending:THREE.AdditiveBlending}));
    this.sparks.frustumCulled=false; this.scene.add(this.sparks);
    this.sparkData=[];
  }
  burst(p,n=14,spread=26,speed=160){
    for(let i=0;i<n;i++){
      if(this.sparkData.length>110) this.sparkData.shift();
      this.sparkData.push({p:p.clone().add(V3((Math.random()-0.5)*spread,
        (Math.random()-0.5)*spread,(Math.random()-0.5)*spread)),
        v:V3((Math.random()-0.5)*speed,Math.random()*speed*0.8,(Math.random()-0.5)*speed),
        life:0.5+Math.random()*0.4});
    }
  }
  puff(p){
    if(this.sparkData.length>110) this.sparkData.shift();
    this.sparkData.push({p:p.clone(),v:V3((Math.random()-0.5)*30,40+Math.random()*30,60),
      life:1.4,exhaust:true});
  }
  followShadow(t){
    this.key.position.set(t.x+600,t.y+950,t.z+450);
    this.key.target.position.copy(t);
  }
  update(dt,t){
    this.dust.rotation.y+=dt*0.004;
    if(this.idle&&Math.random()<dt*22){
      const e=this.exhausts[Math.random()<0.5?0:1];
      e.getWorldPosition(_puff); _puff.z+=45;
      this.puff(_puff);
    }
    // sparks
    const attr=this.sparks.geometry.attributes.position;
    for(let i=this.sparkData.length-1;i>=0;i--){
      const s=this.sparkData[i];
      s.life-=dt;
      if(s.life<=0){ this.sparkData.splice(i,1); continue; }
      if(!s.exhaust) s.v.y-=500*dt;
      s.p.addScaledVector(s.v,dt);
    }
    for(let i=0;i<120;i++){
      const s=this.sparkData[i];
      if(s) attr.setXYZ(i,s.p.x,s.p.y,s.p.z);
      else attr.setXYZ(i,0,-9999,0);
    }
    attr.needsUpdate=true;
  }
}
const _puff=new THREE.Vector3();

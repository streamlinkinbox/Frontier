// Actual celestial 3D gizmo geometry, materials and picking ported from the pinned source.
// Adaptations: stage-relative viewport, local Three.js import, explicit scale capability.
export async function CreateCelestialGizmo(Stage){
 const cv=document.createElement('canvas');cv.className='celestial-gizmo-canvas';cv.setAttribute('aria-hidden','true');
 Object.assign(cv.style,{position:'absolute',inset:'0',width:'100%',height:'100%',pointerEvents:'none',zIndex:12,display:'none'});
 if(typeof cv.getContext!=='function'||typeof WebGLRenderingContext==='undefined')return null; // Node validation has no WebGL canvas.
 const THREE=await import('./ThirdParty/three/three.module.js');
 let width=1,height=1;
  const COLORS={x:0xe01414,y:0x12d40a,z:0x1560e0,magenta:0xc81ec8,cyan:0x1fc7c7,yellow:0xe0cd12,ring:0xffffff};
  const renderer=new THREE.WebGLRenderer({canvas:cv,antialias:true,alpha:true,premultipliedAlpha:true}); renderer.setClearColor(0,0);
  const scene=new THREE.Scene(); const camera=new THREE.PerspectiveCamera(45,1,0.05,1e6);
  scene.add(new THREE.AmbientLight(0xffffff,.75)); const dir=new THREE.DirectionalLight(0xffffff,.9); dir.position.set(4,6,5); scene.add(dir);
  const gizmo=new THREE.Group(); scene.add(gizmo);
  const mat=c=>new THREE.MeshStandardMaterial({color:c,roughness:.45,metalness:.1,depthTest:false,depthWrite:false,transparent:true});
  const AXES=[{name:'x',color:COLORS.x,dir:new THREE.Vector3(1,0,0)},{name:'y',color:COLORS.y,dir:new THREE.Vector3(0,1,0)},{name:'z',color:COLORS.z,dir:new THREE.Vector3(0,0,1)}];
  const L=1.0, TIP=L*.95;
  const orientY=(m,d)=>{ const q=new THREE.Quaternion(); q.setFromUnitVectors(new THREE.Vector3(0,1,0),d.clone().normalize()); m.quaternion.copy(q); };
  const OTHERS={x:{u:new THREE.Vector3(0,1,0),v:new THREE.Vector3(0,0,1),plane:COLORS.cyan},y:{u:new THREE.Vector3(1,0,0),v:new THREE.Vector3(0,0,1),plane:COLORS.magenta},z:{u:new THREE.Vector3(1,0,0),v:new THREE.Vector3(0,1,0),plane:COLORS.yellow}};
  function arcBar(u,v,innerR,outerR,sweep,segments){ const center=Math.PI/4, start=center-sweep/2; const positions=[],indices=[]; const p=new THREE.Vector3();
    for(let i=0;i<=segments;i++){ const a=start+(sweep*i)/segments; const c=Math.cos(a),s=Math.sin(a); p.copy(u).multiplyScalar(innerR*c).addScaledVector(v,innerR*s); positions.push(p.x,p.y,p.z); p.copy(u).multiplyScalar(outerR*c).addScaledVector(v,outerR*s); positions.push(p.x,p.y,p.z); }
    for(let i=0;i<segments;i++){ const b=i*2; indices.push(b,b+1,b+2); indices.push(b+1,b+3,b+2); }
    const geo=new THREE.BufferGeometry(); geo.setAttribute('position',new THREE.Float32BufferAttribute(positions,3)); geo.setIndex(indices); geo.computeVertexNormals(); return geo; }
  const handles=[]; const groups={g:[],s:[],r:[]};
  function reg(mesh,info,mode){ mesh.userData={...info}; mesh.renderOrder=10; handles.push(mesh); groups[mode].push(mesh); }
  for(const ax of AXES){ const m=mat(ax.color);
    const cone=new THREE.Mesh(new THREE.ConeGeometry(.06,.18,24),m.clone()); cone.position.copy(ax.dir.clone().multiplyScalar(TIP)); orientY(cone,ax.dir); gizmo.add(cone); reg(cone,{type:'translate',axis:ax.name},'g');
    const scale=new THREE.Mesh(new THREE.CylinderGeometry(.06,.06,.14,24),m.clone()); scale.position.copy(ax.dir.clone().multiplyScalar(TIP-.28)); orientY(scale,ax.dir); gizmo.add(scale); reg(scale,{type:'scale',axis:ax.name},'s');
    const other=OTHERS[ax.name]; const half=.08; const corner=other.u.clone().add(other.v).multiplyScalar(TIP-half);
    const fill=new THREE.Mesh(new THREE.PlaneGeometry(half*2,half*2),new THREE.MeshBasicMaterial({color:other.plane,side:THREE.DoubleSide,transparent:true,opacity:.28,depthTest:false,depthWrite:false})); fill.position.copy(corner); orientY(fill,ax.dir); fill.rotateX(Math.PI/2); gizmo.add(fill); reg(fill,{type:'plane',axis:ax.name},'g');
    const outer=corner.clone().addScaledVector(other.u,half).addScaledVector(other.v,half); const backU=outer.clone().addScaledVector(other.u,-half*2), backV=outer.clone().addScaledVector(other.v,-half*2);
    const edges=new THREE.Line(new THREE.BufferGeometry().setFromPoints([backU,outer,backV]),new THREE.LineBasicMaterial({color:other.plane,depthTest:false,transparent:true})); edges.renderOrder=10; gizmo.add(edges); groups.g.push(edges);
    const arcRadius=TIP*.62, arcSweep=THREE.MathUtils.degToRad(31), arcBand=.038;
    const arc=new THREE.Mesh(arcBar(other.u,other.v,arcRadius-arcBand,arcRadius+arcBand,arcSweep,24),new THREE.MeshStandardMaterial({color:ax.color,roughness:.45,metalness:.1,side:THREE.DoubleSide,depthTest:false,depthWrite:false,transparent:true})); gizmo.add(arc); reg(arc,{type:'rotate',axis:ax.name},'r'); }
  const ring=new THREE.Mesh(new THREE.TorusGeometry(.16,.008,12,48),new THREE.MeshBasicMaterial({color:COLORS.ring,depthTest:false,transparent:true})); ring.renderOrder=11; gizmo.add(ring);
  // axis-constraint line (world space, long) shown while a modal transform is locked to an axis
  const axisLine=new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(),new THREE.Vector3()]),new THREE.LineBasicMaterial({color:0xffffff,depthTest:false,transparent:true,opacity:.9})); axisLine.renderOrder=12; axisLine.visible=false; scene.add(axisLine);
  const ray=new THREE.Raycaster(); const ptr=new THREE.Vector2(); let hovered=null,active=false;
  const basisMatrix=new THREE.Matrix4(),basisVectors=[new THREE.Vector3(),new THREE.Vector3(),new THREE.Vector3()];
  const hl=(m,on)=>{if(!m)return;const mt=m.material;mt.userData.baseColour??=mt.color.clone();if(mt.emissive)mt.emissive.setHex(on?0x665522:0);mt.color.copy(mt.userData.baseColour);if(on)mt.color.lerp(new THREE.Color(0xffed9b),.65);if(m.userData.type==='plane')mt.opacity=on?.75:.28;};
  const API={
    render(cam,origin,scale,mode,modal,canRotate,canScale=true,basis=[[1,0,0],[0,1,0],[0,0,1]]){ active=true;cv.style.display="block"; const bounds=Stage.getBoundingClientRect(); width=bounds.width; height=bounds.height; const dpr=Math.min(devicePixelRatio||1,2); if(cv.width!==Math.floor(width*dpr)||cv.height!==Math.floor(height*dpr)){ renderer.setPixelRatio(dpr); renderer.setSize(width,height,false); }
      camera.fov=cam.fov; camera.aspect=width/height; camera.updateProjectionMatrix(); camera.position.set(...cam.pos); camera.up.set(...cam.up); camera.lookAt(cam.pos[0]+cam.fwd[0],cam.pos[1]+cam.fwd[1],cam.pos[2]+cam.fwd[2]); camera.updateMatrixWorld();
      gizmo.position.set(...origin); gizmo.scale.setScalar(scale);basisVectors.forEach((V,I)=>V.set(...basis[I]).normalize());basisMatrix.makeBasis(...basisVectors);gizmo.quaternion.setFromRotationMatrix(basisMatrix).normalize(); const locked=modal&&modal.axis;
      groups.g.forEach(o=>o.visible=!modal&&mode==='g'); groups.s.forEach(o=>o.visible=!modal&&mode==='s'&&canScale); groups.r.forEach(o=>o.visible=!modal&&mode==='r'&&canRotate); ring.visible=!locked; ring.quaternion.copy(gizmo.quaternion).invert().multiply(camera.quaternion);
      if(locked){ const d=basis[{x:0,y:1,z:2}[modal.axis]]; const R=scale*400; axisLine.geometry.setFromPoints([new THREE.Vector3(origin[0]-d[0]*R,origin[1]-d[1]*R,origin[2]-d[2]*R),new THREE.Vector3(origin[0]+d[0]*R,origin[1]+d[1]*R,origin[2]+d[2]*R)]); axisLine.material.color.setHex(COLORS[modal.axis]); axisLine.visible=true; } else axisLine.visible=false;
      if(hovered&&!hovered.visible){hl(hovered,false);hovered=null;}renderer.render(scene,camera); },
    clear(){ active=false;hl(hovered,false);hovered=null;cv.style.display="none";renderer.clear(); },
    pick(x,y){ if(!active)return null;const bounds=Stage.getBoundingClientRect(); x-=bounds.left; y-=bounds.top; ptr.x=(x/width)*2-1; ptr.y=-(y/height)*2+1; ray.setFromCamera(ptr,camera); const hits=ray.intersectObjects(handles.filter(h=>h.visible),false); return hits.length?hits[0].object.userData:null; },
    hover(x,y){ const info=this.pick(x,y); const next=info?handles.find(h=>h.userData===info):null; if(next!==hovered){ hl(hovered,false); hl(next,true); hovered=next;if(active)renderer.render(scene,camera); } return !!next; }
  };
 Stage.appendChild(cv);
 return {...API,get Hovered(){return hovered?.userData??null;},Canvas:cv,Scene:scene,Gizmo:gizmo,Handles:handles,Groups:groups,Camera:camera,Renderer:renderer};
}

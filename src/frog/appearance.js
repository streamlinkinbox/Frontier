import * as THREE from 'three';
const clamp=THREE.MathUtils.clamp,lerp=THREE.MathUtils.lerp;
const smooth=(a,b,v)=>{const t=clamp((v-a)/(b-a),0,1);return t*t*(3-2*t);};
const hash=(x,y,z)=>{let n=Math.imul(x,374761393)^Math.imul(y,668265263)^Math.imul(z,2147483647);n=Math.imul(n^(n>>>13),1274126177);return ((n^(n>>>16))>>>0)/4294967295;};
export function noise(x,y,z){
  const ix=Math.floor(x),iy=Math.floor(y),iz=Math.floor(z);x-=ix;y-=iy;z-=iz;
  x=x*x*(3-2*x);y=y*y*(3-2*y);z=z*z*(3-2*z);
  return lerp(lerp(lerp(hash(ix,iy,iz),hash(ix+1,iy,iz),x),lerp(hash(ix,iy+1,iz),hash(ix+1,iy+1,iz),x),y),lerp(lerp(hash(ix,iy,iz+1),hash(ix+1,iy,iz+1),x),lerp(hash(ix,iy+1,iz+1),hash(ix+1,iy+1,iz+1),x),y),z);
}
const dorsal=new THREE.Color('#756e5a'),light=new THREE.Color('#a49878'),belly=new THREE.Color('#c9c1a5'),mark=new THREE.Color('#413d32');
export function skinColor(x,y,z,region='body',normal=null){
  const n=noise(x*1.35,y*1.4,z*1.35)*.65+noise(x*4.1,y*4,z*4.1)*.35;
  let c=dorsal.clone().lerp(light,smooth(.25,.72,n)*.46);
  const underside=region==='body'||!normal?1-smooth(.62,1.5,y):1-smooth(-.7,.1,normal.y);c.lerp(belly,underside*.94);
  const patch=smooth(.62,.79,noise(x*3.7,y*3.7,z*3.7));c.lerp(mark,patch*.65*smooth(.55,1.5,y));
  const mask=smooth(.82,1.22,Math.abs(x))*Math.exp(-Math.pow((z-1.75)/.68,2)-Math.pow((y-2.15)/.41,2));
  c.lerp(mark,mask*.83);
  const chevron=Math.exp(-Math.pow((z-(.83-Math.abs(x)*.5))/.13,2))*smooth(1.9,2.8,y)*(1-smooth(.7,1.4,Math.abs(x)));
  c.lerp(mark,chevron*.38);
  if(region==='thigh'||region==='shin'||region==='arm'){
    const stripe=smooth(.38,.86,Math.sin(z*8.5+Math.abs(x)*2.1+noise(x*3,y*3,z*3)*1.8));
    c.lerp(mark,stripe*.26);
  }
  c.multiplyScalar(.95+noise(x*27,y*27,z*27)*.095);
  return c;
}
function tex(c,color=true){const t=new THREE.CanvasTexture(c);t.colorSpace=color?THREE.SRGBColorSpace:THREE.NoColorSpace;t.wrapS=t.wrapT=THREE.RepeatWrapping;t.anisotropy=8;return t;}
function canvas(w,h=w){const c=document.createElement('canvas');c.width=w;c.height=h;return c;}
export function frogMaterials(){
  const bump=canvas(512),rough=canvas(512),b=bump.getContext('2d'),r=rough.getContext('2d'),bi=b.createImageData(512,512),ri=r.createImageData(512,512);
  const h=(x,y)=>noise(x*.09,y*.09,9)*.7+noise(x*.32,y*.32,3)*.3;
  for(let y=0;y<512;y++)for(let x=0;x<512;x++){
    const i=(y*512+x)*4,n=h(x,y),dx=h(x+1,y)-h(x-1,y),dy=h(x,y+1)-h(x,y-1);
    bi.data.set([128-dx*65,128-dy*65,253,255],i);const value=122+n*90;ri.data.set([value,value,value,255],i);
  }
  b.putImageData(bi,0,0);r.putImageData(ri,0,0);
  const normal=tex(bump,false),roughness=tex(rough,false);normal.repeat.set(3,3);roughness.repeat.set(3,3);
  const materials={};
  const add=(name,props)=>{const m=new THREE.MeshPhysicalMaterial({vertexColors:true,metalness:0,roughness:.5,...props});m.name=name;materials[name]=m;return m;};
  add('Skin',{normalMap:normal,normalScale:new THREE.Vector2(.26,.26),roughnessMap:roughness,roughness:.75,clearcoat:.30,clearcoatRoughness:.31,sheen:.08,sheenColor:new THREE.Color('#b3ac92')});
  add('Dorsolateral folds',{normalMap:normal,normalScale:new THREE.Vector2(.18,.18),roughness:.54,clearcoat:.22});
  add('Ventral skin',{normalMap:normal,normalScale:new THREE.Vector2(.13,.13),roughness:.62,clearcoat:.16});
  add('Webbing',{roughness:.55,side:THREE.DoubleSide,clearcoat:.17});
  add('Mouth and nostrils',{color:'#423b31',roughness:.52});
  add('Tympanum',{color:'#8a8067',roughness:.5,clearcoat:.25});
  add('Eye globe',{color:'#ffffff',roughness:.30,clearcoat:.40,clearcoatRoughness:.18});
  add('Iris',{roughness:.31,clearcoat:.55,clearcoatRoughness:.15});
  add('Pupil',{color:'#111410',roughness:.12,clearcoat:1,clearcoatRoughness:.06});
  add('Eyelids',{normalMap:normal,normalScale:new THREE.Vector2(.17,.17),roughness:.47,clearcoat:.28});
  return materials;
}

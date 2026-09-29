import * as THREE from 'three';
import {noise} from '../frog/appearance.js';
import {smooth} from './anatomy.js';
const C=x=>new THREE.Color(x);
export function shellColor(x,y,z,ventral=false){
  const coarse=noise(x*.8+12,y*.8,z*.8+4),mid=noise(x*2.8,y*2.8,z*2.8),fine=noise(x*15,y*15,z*15);
  const c=C('#4b5238').lerp(C('#93864f'),smooth(.40,.66,coarse)*.75);
  c.lerp(C('#303a2c'),smooth(.53,.76,mid)*.76);
  c.multiplyScalar(.90+fine*.18);
  if(ventral)c.lerp(C('#c2b58c'),.87);
  return c;
}
export function limbColor(t,v,part=0){
  const c=C('#7a8052').lerp(C('#b5a56f'),.2+.2*Math.sin(t*5));
  const n=noise(t*9,Math.cos(v*Math.PI*2)*2+part,Math.sin(v*Math.PI*2)*2);
  c.lerp(C('#374330'),smooth(.45,.76,n)*.68);
  const underside=smooth(.1,.85,Math.sin(v*Math.PI*2));
  c.lerp(C('#c6b991'),underside*.10);
  if(t<.04||t>.95)c.lerp(C('#bca57b'),.32);
  return c;
}
const canvas=(n)=>{const c=document.createElement('canvas');c.width=c.height=n;return c;};
const texture=(c,color)=>{const t=new THREE.CanvasTexture(c);t.colorSpace=color?THREE.SRGBColorSpace:THREE.NoColorSpace;t.wrapS=t.wrapT=THREE.RepeatWrapping;t.anisotropy=8;return t;};
export function crabMaterials(){
  const n=512,normal=canvas(n),rough=canvas(n),albedo=canvas(n),nc=normal.getContext('2d'),rc=rough.getContext('2d'),ac=albedo.getContext('2d');
  const ni=nc.createImageData(n,n),ri=rc.createImageData(n,n),ai=ac.createImageData(n,n);
  const h=(x,y)=>noise(x*.13,y*.13,7)*.65+noise(x*.44,y*.44,2)*.35;
  for(let y=0;y<n;y++)for(let x=0;x<n;x++){
    const i=(y*n+x)*4,v=h(x,y),dx=h(x+1,y)-h(x-1,y),dy=h(x,y+1)-h(x,y-1);
    ni.data.set([128-dx*110,128-dy*110,251,255],i);const r=130+v*85;ri.data.set([r,r,r,255],i);
    const a=235+v*20;ai.data.set([a,a,a,255],i);
  }
  nc.putImageData(ni,0,0);rc.putImageData(ri,0,0);ac.putImageData(ai,0,0);
  const maps={normal:texture(normal,false),rough:texture(rough,false),albedo:texture(albedo,true)};maps.normal.repeat.set(4,4);maps.rough.repeat.set(4,4);maps.albedo.repeat.set(4,4);
  const out={};function mat(name,props){const m=new THREE.MeshPhysicalMaterial({vertexColors:true,roughness:.5,metalness:0,...props});m.name=name;out[name]=m;}
  mat('Carapace',{map:maps.albedo,normalMap:maps.normal,normalScale:new THREE.Vector2(.28,.28),roughnessMap:maps.rough,roughness:.77,clearcoat:.25,clearcoatRoughness:.32});
  mat('Limb cuticle',{map:maps.albedo,normalMap:maps.normal,normalScale:new THREE.Vector2(.19,.19),roughnessMap:maps.rough,roughness:.77,clearcoat:.18});
  mat('Ventral plates',{normalMap:maps.normal,normalScale:new THREE.Vector2(.15,.15),roughness:.66});
  mat('Joint membranes',{color:'#7b7856',roughness:.75});
  mat('Chelal fingers',{normalMap:maps.normal,normalScale:new THREE.Vector2(.12,.12),roughness:.47,clearcoat:.21});
  mat('Denticles',{color:'#bdad7e',roughness:.52});
  mat('Eyes',{color:'#24291d',roughness:.19,clearcoat:.7,clearcoatRoughness:.15});
  mat('Setae',{color:'#a49d70',roughness:.7});
  return out;
}

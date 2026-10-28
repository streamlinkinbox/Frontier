import {ProjectGizmo} from './TransformGizmo.js';
const NS='http://www.w3.org/2000/svg';
export class ClusterDebugOverlay{
 constructor(H){this.Host=H;const Stage=document.getElementById('stage');if(!Stage||!document.createElementNS)return;this.SVG=document.createElementNS(NS,'svg');Object.assign(this.SVG.style,{position:'absolute',inset:'0',width:'100%',height:'100%',pointerEvents:'none',zIndex:11});Stage.appendChild(this.SVG);}
 Draw(){const S=this.SVG,H=this.Host,G=H.Surface.Clusters;if(!S)return;if(!G.DebugEnabled){if(this.Visible)S.replaceChildren();this.Visible=false;return;}this.Visible=true;S.replaceChildren();const B=S.getBoundingClientRect(),C=H.CameraState;if(!C)return;
  for(const [I,R]of (G.DebugRecords??[]).slice(0,2000).entries()){
   const M=H.Scene.Occupants[R.Occupant]?.Transform;if(!M)continue;const Points=Array.from({length:8},(_,K)=>{const P=R.Min.map((V,A)=>K&(1<<A)?R.Max[A]:V);return ProjectGizmo([0,1,2].map(A=>M[A]*P[0]+M[4+A]*P[1]+M[8+A]*P[2]+M[12+A]),C,B.width,B.height);});
   let Path='';for(let K=0;K<8;K++)for(let A=0;A<3;A++){const J=K^(1<<A);if(J<K||!Points[K]||!Points[J])continue;const P=Points[K],Q=Points[J];Path+=`M${P[0]},${P[1]}L${Q[0]},${Q[1]}`;}
   if(!Path)continue;const E=document.createElementNS(NS,'path');E.setAttribute('d',Path);E.setAttribute('fill','none');E.setAttribute('stroke',H.Settings.ClusteredGeometryEnabled===false||G.DebugValues?.[I]===undefined?'#a3aab5':G.DebugValues[I]?'#61eaa0':'#ff6f83');E.setAttribute('stroke-width','1.3');E.setAttribute('opacity','.8');S.appendChild(E);
  }
 }
}

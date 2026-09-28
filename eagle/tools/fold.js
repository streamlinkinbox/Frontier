(()=>{const g=__game,T=g.THREE,r=g.eagle.rig;g.step(3);
const pel=r.pelvis;const P=(o)=>{const v=new T.Vector3();o.getWorldPosition(v);return pel.worldToLocal(v)};
const tg={e:new T.Vector3(0.095,-0.005,-0.04),w:new T.Vector3(0.1,0.075,0.17),t:new T.Vector3(0.1,0.06,-0.02)};
const cost=(x)=>{r.setWing(0,{elev:x[0],sweep:x[1],twist:x[2],elbow:x[3],wrist:x[4],handTwist:x[5]});r.root.updateMatrixWorld(true);
 const W=r.wings[0];let c=P(W.ulna).distanceToSquared(tg.e)+P(W.hand).distanceToSquared(tg.w)+P(W.tip).distanceToSquared(tg.t);
 // hand dorsal axis should face outward (+x in the body frame), slightly up
 const q=new T.Quaternion();W.hand.getWorldQuaternion(q);const pq=new T.Quaternion();pel.getWorldQuaternion(pq);q.premultiply(pq.invert());
 const y=new T.Vector3(0,1,0).applyQuaternion(q);c+=0.01*(1-y.dot(new T.Vector3(0.95,0.3,0).normalize()));
 const yu=new T.Vector3(0,1,0).applyQuaternion((()=>{const a=new T.Quaternion();W.ulna.getWorldQuaternion(a);return a.premultiply(pq)})());c+=0.01*(1-yu.dot(new T.Vector3(0.95,0.3,0).normalize()));
 return c};
let x=[-0.62,-1.38,1.42,2.72,2.78,0],best=cost(x);
for(let it=0;it<4000;it++){const s=0.3*Math.pow(0.999,it);const y=x.map(v=>v+(Math.random()-0.5)*s);y[3]=Math.min(y[3],2.95);y[4]=Math.min(y[4],2.95);const c=cost(y);if(c<best){best=c;x=y;}}
cost(x);const W=r.wings[0];
return {x:x.map(v=>+v.toFixed(3)),best,e:P(W.ulna).toArray().map(v=>+v.toFixed(3)),w:P(W.hand).toArray().map(v=>+v.toFixed(3)),t:P(W.tip).toArray().map(v=>+v.toFixed(3)),sh:P(W.shoulder).toArray().map(v=>+v.toFixed(3))}})()

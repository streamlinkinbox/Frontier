(()=>{const g=__game,T=g.THREE,r=g.eagle.rig;g.step(5);
const w=(o)=>{const v=new T.Vector3();o.getWorldPosition(v);return v.toArray().map(x=>+x.toFixed(3))};
const P=g.anim.pose;
return {pelvis:w(r.pelvis),thorax:w(r.thorax),neck0:w(r.neck[0]),neck7:w(r.neck[7]),head:w(r.head),headTarget:P.head.toArray(),neck:g.anim.neckState,
 hip:w(r.legs[0].hip),knee:w(r.legs[0].tibia),ankle:w(r.legs[0].tarsus),foot:w(r.legs[0].foot),sh:w(r.wings[0].shoulder),elbow:w(r.wings[0].ulna),wrist:w(r.wings[0].hand),tip:w(r.wings[0].tip),tail:w(r.tail)}})()

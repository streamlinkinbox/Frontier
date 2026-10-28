export const LightningDefaults=Object.freeze({LightningEnabled:false,LightningAuto:false,LightningDistance:1800,LightningIntervalMin:6,LightningIntervalMax:14,LightningRoughness:2,LightningDetail:6,LightningBranches:.35,LightningWidth:.11,LightningBrightness:1,LightningCloudGlow:1,LightningRadius:1800,LightningSpread:4200,LightningMode:0,LightningCloudTarget:0});
export const LightningRanges={LightningDistance:[50,6000],LightningIntervalMin:[2,60],LightningIntervalMax:[2,60],LightningRoughness:[.2,5],LightningDetail:[3,7],LightningBranches:[0,1],LightningWidth:[.02,.35],LightningBrightness:[0,3],LightningCloudGlow:[0,3],LightningRadius:[200,6000],LightningSpread:[400,10000],LightningMode:[0,1],LightningCloudTarget:[0,2]};
const Mapping={auto:'LightningAuto',distance:'LightningDistance',intervalMin:'LightningIntervalMin',intervalMax:'LightningIntervalMax',amp:'LightningRoughness',levels:'LightningDetail',branchChance:'LightningBranches',width:'LightningWidth',brightness:'LightningBrightness',cloudGlow:'LightningCloudGlow'};
export function LightningEditorSettings(S){return Object.fromEntries(Object.entries(Mapping).map(([K,V])=>[K,S[V]??LightningDefaults[V]]));}
export function ApplyLightningEditor(S,Doc){
 if(!Doc||typeof Doc!=='object'||Array.isArray(Doc))return false;
 for(const [K,V]of Object.entries(Mapping)){
  const X=Doc[K];if(typeof LightningDefaults[V]==='boolean'){if(typeof X==='boolean')S[V]=X;}
  else if(typeof X==='number'&&Number.isFinite(X)){const [A,B]=LightningRanges[V];S[V]=Math.max(A,Math.min(B,X));}
 }
 S.LightningDetail=Math.round(S.LightningDetail??6);S.LightningIntervalMax=Math.max(S.LightningIntervalMin??6,S.LightningIntervalMax??14);return true;
}
export function LightningEnvelope(Age){
 if(Age<0||Age>.36)return 0;
 // Dim leader, then three return strokes along the SAME channel, not random new bolts every frame.
 let E=Age<.014?.04:0;
 for(const [At,Power]of [[.014,1],[.085,.65],[.17,.35]])if(Age>=At)E+=Power*Math.exp(-(Age-At)/.019);
 return E;
}

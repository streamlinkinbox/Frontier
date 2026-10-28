// Physics constants and spawn laws adapted from the actual deployed celestial demo.
// Repo SultanAladin/Frontier-, Pages branch arena/01a08c57-frontier,
// commit da3720b353dc0e4d2421f064f65cbfeb41cdb5f2, docs/celestial/index.html.
// Original prStep: Y-up CPU/Canvas. Runtime port: Z-up WebGPU compute/raster.
export const CelestialTypes=Object.freeze([
 {Name:'Rain',vt:8.5,d:2.2,bounce:0,drag:.25,len:1,melt:0,shape:0},
 {Name:'Drizzle',vt:3.2,d:.6,bounce:0,drag:.6,len:.5,melt:0,shape:0},
 {Name:'Hail',vt:22,d:9,bounce:.6,drag:.08,len:0,melt:0,shape:1},
 {Name:'Snow',vt:1.4,d:5,bounce:0,drag:1.2,len:0,melt:1,shape:2},
 {Name:'Sleet',vt:5.5,d:3,bounce:.15,drag:.4,len:.4,melt:.6,shape:1},
]);
export const CelestialBudgets=Object.freeze({Low:4000,Medium:12000,High:25000});
export function CelestialEmissionRate(S,AltitudeFade=1){const PerM2=S.Category===3?S.RateMillimetresPerHour*.9:S.Category===1?S.RateMillimetresPerHour*2.2:S.RateMillimetresPerHour*.55;return Math.min(S.Budget*1.5,PerM2*Math.PI*45**2*S.Density)*Math.max(0,Math.min(1,AltitudeFade));}
export function CelestialTypeIndex(Value){return Math.max(0,Math.min(4,Math.round(Number.isFinite(Value)?Value:3)));}

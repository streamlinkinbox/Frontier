// Clustered, photometric local-light authoring. This deliberately replaces the old
// particle-only proxy implementation: one record format drives screen direct light,
// surface-card relighting (GI) and mirrors.
export const ClusteredLightCapacity = 24;
export const ClusteredLightFloats = ClusteredLightCapacity * 16;
export const AutomotiveLightModes = Object.freeze(["Off", "Low beam", "High beam", "Fog"]);

const PoleDefinitions = [[-45,10,-90],[-15,10,-90],[15,10,-90],[45,10,-90],[-30,-10,90],[0,-10,90],[30,-10,90],[-48,30,0],[-12,44,180],[-56,-24,0],[20,-38,90],[8,30,0],[90,-36,180],[90,8,180],[90,46,180],[110,-36,0],[110,8,0],[128,30,0],[154,-32,180],[128,-4,-45]];
const Luma = C => C[0]*.2126+C[1]*.7152+C[2]*.0722;
const TxPoint = (M,P) => [M[0]*P[0]+M[4]*P[1]+M[8]*P[2]+M[12], M[1]*P[0]+M[5]*P[1]+M[9]*P[2]+M[13], M[2]*P[0]+M[6]*P[1]+M[10]*P[2]+M[14]];
const TxDirection = (M,D) => { const V=[M[0]*D[0]+M[4]*D[1]+M[8]*D[2],M[1]*D[0]+M[5]*D[1]+M[9]*D[2],M[2]*D[0]+M[6]*D[1]+M[10]*D[2]]; const L=Math.hypot(...V)||1; return V.map(X=>X/L); };
const Spot = (Position, Direction, Reach, Colour, Inner, Outer, Ies=1) => ({Position,Direction,Reach,Colour,Inner:Math.cos(Inner),Outer:Math.cos(Outer),Type:Ies});
const Point = (Position, Reach, Colour) => ({Position,Direction:[0,0,-1],Reach,Colour,Inner:-1,Outer:-1,Type:0});

export class ClusteredLighting {
    constructor() { this.Mode=1; this.SceneEnabled=true; }
    CycleAutomotive() { this.Mode=(this.Mode+1)%AutomotiveLightModes.length; return AutomotiveLightModes[this.Mode]; }
    Build(CameraPosition, VehicleTransform, Sparks, Effects) {
        const Lights=[];
        // Camera-local CPU cluster: only nearby authored luminaires enter the fixed GPU cluster.
        if(this.SceneEnabled) for(const [X,Y,Facing] of PoleDefinitions.map(P=>[...P]).sort((A,B)=>(A[0]-CameraPosition[0])**2+(A[1]-CameraPosition[1])**2-(B[0]-CameraPosition[0])**2-(B[1]-CameraPosition[1])**2).slice(0,12)) {
            const A=Facing*Math.PI/180, P=[X+.7*Math.cos(A),Y+.7*Math.sin(A),10.62];
            Lights.push(Spot(P,[.18*Math.cos(A),.18*Math.sin(A),-.984],24,[620,455,265],24*Math.PI/180,58*Math.PI/180,2));
        }
        if(this.Mode && VehicleTransform) {
            const profile=this.Mode===1?{reach:58,power:980,inner:7,outer:18,dip:-.075}:this.Mode===2?{reach:105,power:1450,inner:5,outer:12,dip:-.018}:{reach:28,power:600,inner:18,outer:38,dip:-.15};
            for(const side of [-1,1]) {
                const P=TxPoint(VehicleTransform,[2.05,side*.66,.28]);
                Lights.push(Spot(P,TxDirection(VehicleTransform,[1,side*.025,profile.dip]),profile.reach,[profile.power,profile.power*.91,profile.power*.72],profile.inner*Math.PI/180,profile.outer*Math.PI/180,3));
            }
            // Roof-mounted sealed LED flood pod: wide SAE work-light profile.
            const Roof=TxPoint(VehicleTransform,[.15,0,1.14]);
            Lights.push(Spot(Roof,TxDirection(VehicleTransform,[1,0,-.06]),46,[900,970,1100],16*Math.PI/180,42*Math.PI/180,4));
        }
        // Keep effect lights bounded and subordinate to functional lighting.
        if(Sparks?.Settings?.Enabled) for(const S of Sparks.Particles.slice(0,4)) Lights.push(Point(S.Position,3.5,[12,5,1]));
        if(Effects) for(const E of Effects.Emitters.slice(0,2)) if(E && Effects.Clock>=E.Start && Effects.Clock<E.End) Lights.push(Point([E.Transform[12],E.Transform[13],E.Transform[14]+1],7,[18,8,3]));
        const Selected=Lights.sort((A,B)=>Luma(B.Colour)-Luma(A.Colour)).slice(0,ClusteredLightCapacity);
        const Data=new Float32Array(ClusteredLightFloats);
        Selected.forEach((L,I)=>Data.set([...L.Position,L.Reach,...L.Colour,L.Type,...L.Direction,L.Outer,L.Inner],I*16));
        return Data;
    }
}

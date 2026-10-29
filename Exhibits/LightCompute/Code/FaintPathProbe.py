# Extract the CURRENT production block rather than reimplementing the algorithm in a test shader.
from pathlib import Path
import hashlib
Root = Path(__file__).resolve().parents[3]
Source = Root / 'Frontier/Engine/Rendering/Shaders/SolidDielectricTransport.slang'
Text = Source.read_text()
Start, End = Text.index('        Faint = Faint ||'), Text.index('        // Offset along the geometric normal:')
Block = Text[Start:End]
Template = (Root / 'Exhibits/LightCompute/Code/DielectricBoundary.slang').read_text()
Template = Template[Template.index('struct BoundaryExtent'):Template.index('[shader("compute")')]
Prefix = '''float RandomUnit(inout uint Bits) { return asfloat(Bits); }
float3 Probe(float3 Throughput,uint Interaction,inout bool Faint,inout uint SurvivalBits) {
'''
Suffix = '''return Throughput;
}
[shader("compute")][numthreads(64,1,1)] void EvaluateFaintPath(uint3 Dispatch : SV_DispatchThreadID) {
 uint I = Dispatch.x; if (I >= Boundary.Dimensions.x) return;
 uint P = I*8;
 bool Faint = BoundaryCoefficients[P+4] != 0;
 uint Bits = asuint(BoundaryCoefficients[P+5]);
 float3 W = Probe(float3(BoundaryCoefficients[P],BoundaryCoefficients[P+1],BoundaryCoefficients[P+2]),
                  uint(BoundaryCoefficients[P+3]),Faint,Bits);
 BoundaryOutput[I] = float4(W,0);
 BoundaryOutput[16*Boundary.Dimensions.x+I] = float4(Faint ? 1 : 0,0,0,0);
}
'''
Build = Root / '_ClaudeScratch/build/faint-path'
Build.mkdir(parents=True,exist_ok=True)
(Build / 'FaintPath.slang').write_text(Template + Prefix + Block + Suffix)
(Build / 'ProductionBlock.sha256').write_text(hashlib.sha256(Block.encode()).hexdigest()+'\n')

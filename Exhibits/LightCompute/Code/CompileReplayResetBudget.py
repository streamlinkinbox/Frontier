#============================================================================================================================================
#                                                   COMPILEREPLAYRESETBUDGET.PY
#============================================================================================================================================
# 🧩 Derive a metadata-selected Fresh8 reset branch without changing the bounded non-reset estimator.

from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
Destination.parent.mkdir(parents=True, exist_ok=True)
Previous = Destination.parent / 'previous'
Previous.mkdir(exist_ok=True)
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileReplayBoundedTemporal.py',
                str(Previous / 'ReplayBoundedTemporal.slang')], check=True)
Source = (Previous / 'ReplayBoundedTemporal.slang').read_text()
Generation = '''    if ((Extent.Primary.w & 1048576u) != 0 && (Extent.Primary.w & 524288u) != 0)
    {
        if (R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 && R.ViewMetallic.w == 0)
        {
            uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
            uint FirstSeed = NextRandom(SeedBits);
            float3 Total = EvaluateReplayPath(R, FirstSeed);
            for (uint Index = 1; Index < 8; ++Index)
                Total += EvaluateReplayPath(R, NextRandom(SeedBits));
            float3 Mean = Total / 8.f;
            // The persisted proposal is the first uniform seed, not the eight-path mean.
            Packed = float4(EncodeReplaySeed(FirstSeed), 1, 1);
            SelectedRadiance = float4(Mean, ReplayTarget(Mean));
        }
        OutputColour[ReplaySlot(2, Pixel)] = Packed;
        OutputColour[ReplaySlot(3, Pixel)] = SelectedRadiance;
        return;
    }
'''
Resolution = '''    if ((Extent.Primary.w & 1048576u) != 0 && (Extent.Primary.w & 524288u) != 0)
    {
        // Consume the fresh mean once; do not spatially reweight this output.
        OutputColour[Pixel] = float4(OutputColour[ReplaySlot(3, Pixel)].xyz + R.ResidualSpecular.xyz, 1);
        OutputColour[ReplaySlot(0, Pixel)] = Local;
        OutputColour[ReplaySlot(1, Pixel)] = float4(R.PositionHit.xyz, R.PositionHit.w != 0 ? R.MotionPositionToken.w : 0);
        OutputColour[ReplaySlot(4, Pixel)] = float4(R.NormalRoughness.xyz, 0);
        return;
    }
'''
for PreviousText, Replacement in (
    ('    float4 Packed = 0, SelectedRadiance = 0;\n', '    float4 Packed = 0, SelectedRadiance = 0;\n' + Generation),
    ('    float4 Local = OutputColour[ReplaySlot(2, Pixel)];\n',
     '    float4 Local = OutputColour[ReplaySlot(2, Pixel)];\n' + Resolution),
):
    if Source.count(PreviousText) != 1:
        raise RuntimeError('Review reset-budget substitution: ' + PreviousText)
    Source = Source.replace(PreviousText, Replacement)
Destination.write_text(Source)
print('Derived metadata-only reset Fresh8; prior non-reset shader text unchanged')

#!/usr/bin/env python3
"""Generate one test-only shader recording temporal-history path visibility and weights."""
import sys
from pathlib import Path

SOURCE = Path("Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise ValueError(f"expected one {label} anchor, found {count}")
    return text.replace(old, new, 1)


def generate(destination):
    path = Path(destination)
    if path.resolve() == SOURCE.resolve():
        raise ValueError("Refusing to overwrite the Engine shader")
    shader = SOURCE.read_text(encoding="utf-8")
    shader = replace_once(
        shader,
        "bool SampleReplayFacetDirect(SurfacePoint Receiver, float3 Origin, uint RemainingBounces, inout uint Bits,\n"
        "                             out float3 OutDirection, out float3 OutFactor)",
        "bool SampleReplayFacetDirect(SurfacePoint Receiver, float3 Origin, uint RemainingBounces, inout uint Bits,\n"
        "                             out float3 OutDirection, out float3 OutFactor, out float4 VisibilityAudit)",
        "direct-light sampler signature",
    )
    shader = replace_once(
        shader,
        "    OutDirection = 0;\n    OutFactor = 0;\n    uint FacetCount = Extent.Reconstruction.z;",
        "    OutDirection = 0;\n    OutFactor = 0;\n    VisibilityAudit = 0;\n    uint FacetCount = Extent.Reconstruction.z;",
        "direct-light audit initialization",
    )
    shader = replace_once(
        shader,
        "    float3 Transmittance = ReplaySegmentTransmittance(Origin, Direction, Distance, FacetIndex,\n"
        "                                                       TransmissionProbability, PaneCount);\n"
        "    if (all(Transmittance <= 0)) return false;",
        "    float3 Transmittance = ReplaySegmentTransmittance(Origin, Direction, Distance, FacetIndex,\n"
        "                                                       TransmissionProbability, PaneCount);\n"
        "    VisibilityAudit = float4(1.0, any(Transmittance > 0.0) ? 1.0 : 0.0, float(PaneCount),\n"
        "                            Power(max(Transmittance, 0.0)));\n"
        "    if (all(Transmittance <= 0)) return false;",
        "direct-segment visibility readback",
    )
    shader = replace_once(
        shader,
        "float3 EvaluateReplayPath(SurfacePoint Receiver, uint Seed)\n{\n    uint Bits = Seed;",
        "float3 EvaluateReplayPath(SurfacePoint Receiver, uint Seed, out float4 VisibilityAudit,\n"
        "                          out float4 ContributionAudit)\n{\n"
        "    VisibilityAudit = 0;\n"
        "    ContributionAudit = 0;\n"
        "    uint Bits = Seed;",
        "path evaluator audit outputs",
    )
    shader = replace_once(
        shader,
        "        bool HasNee = SampleFacets && !ThinSheet &&\n"
        "                      SampleReplayFacetDirect(Current, Origin, Extent.Primary.z - Bounce, Bits, NeeDirection, NeeFactor);",
        "        float4 SegmentAudit = 0;\n"
        "        bool HasNee = SampleFacets && !ThinSheet &&\n"
        "                      SampleReplayFacetDirect(Current, Origin, Extent.Primary.z - Bounce, Bits,\n"
        "                                             NeeDirection, NeeFactor, SegmentAudit);\n"
        "        VisibilityAudit.xy += SegmentAudit.xy;",
        "visibility-audited direct-light invocation",
    )
    shader = replace_once(
        shader,
        "            if (HasNee) Radiance += Throughput * NeeBrdf * NeeFactor;",
        "            if (HasNee)\n"
        "            {\n"
        "                float3 NeeContribution = Throughput * NeeBrdf * NeeFactor;\n"
        "                Radiance += NeeContribution;\n"
        "                ContributionAudit.x += Power(max(NeeContribution, 0.0));\n"
        "            }",
        "direct-light contribution accumulation",
    )
    shader = replace_once(
        shader,
        "        if (Index == 0xffffffffu)\n"
        "        {\n"
        "            Radiance += Throughput * max(Extent.Minimum.xyz, 0); // Uniform environment, not an importance map.",
        "        if (Index == 0xffffffffu)\n"
        "        {\n"
        "            if (Bounce == 0u) VisibilityAudit.z += 1.0;\n"
        "            Radiance += Throughput * max(Extent.Minimum.xyz, 0); // Uniform environment, not an importance map.",
        "first-bounce environment escape",
    )
    shader = replace_once(
        shader,
        "        FacetCorners F = Facets[Index];\n"
        "        if (!SourceProposalAvailable || (Extent.Primary.w & 256u) != 0)",
        "        FacetCorners F = Facets[Index];\n"
        "        if (Bounce == 0u && !any(F.Emissive.xyz > 0.0)) VisibilityAudit.w += 1.0;\n"
        "        if (!SourceProposalAvailable || (Extent.Primary.w & 256u) != 0)",
        "first-bounce opaque/nonemissive hit",
    )
    shader = replace_once(
        shader,
        "        if (!SourceProposalAvailable || (Extent.Primary.w & 256u) != 0)\n"
        "            Radiance += Throughput * max(F.Emissive.xyz, 0);",
        "        float3 RadianceBeforeEmitter = Radiance;\n"
        "        if (!SourceProposalAvailable || (Extent.Primary.w & 256u) != 0)\n"
        "            Radiance += Throughput * max(F.Emissive.xyz, 0);",
        "emitter contribution baseline",
    )
    shader = replace_once(
        shader,
        "        float3 GeometricNormal =\n",
        "        if (any(F.Emissive.xyz > 0))\n"
        "        {\n"
        "            ContributionAudit.y += max(0.0, Power(Radiance) - Power(RadianceBeforeEmitter));\n"
        "            ContributionAudit.w += 1.0;\n"
        "        }\n"
        "        float3 GeometricNormal =\n",
        "emitter-hit contribution audit",
    )
    shader = replace_once(
        shader,
        "    return Radiance;\n}\nfloat ReplayEffectiveSpecularRoughness",
        "    ContributionAudit.z = Power(max(Radiance, 0.0));\n"
        "    return Radiance;\n}\nfloat ReplayEffectiveSpecularRoughness",
        "path contribution audit completion",
    )
    # Keep the other reservoir proposals on the same evaluator; their diagnostics are discarded.
    shader = replace_once(shader,
        "        float3 Radiance = EvaluateReplayPath(R, Selected);",
        "        float4 IgnoredVisibility, IgnoredContribution;\n"
        "        float3 Radiance = EvaluateReplayPath(R, Selected, IgnoredVisibility, IgnoredContribution);",
        "first fresh proposal evaluator call")
    shader = replace_once(shader,
        "            float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);",
        "            float4 IgnoredVisibility, IgnoredContribution;\n"
        "            float3 CandidateRadiance = EvaluateReplayPath(R, Candidate, IgnoredVisibility, IgnoredContribution);",
        "additional fresh proposal evaluator call")
    shader = replace_once(shader,
        "                    float3 Replayed = EvaluateReplayPath(R, Candidate);\n"
        "                    float ReplayedTarget = ReplayTarget(Replayed);",
        "                    float4 HistoryVisibility, HistoryContribution;\n"
        "                    float3 Replayed = EvaluateReplayPath(R, Candidate, HistoryVisibility, HistoryContribution);\n"
        "                    float4 PreviousPathRecord = OutputColour[ReplaySlot(9, Previous)];\n"
        "                    float PreviousPathRadiance = DecodeReplaySeed(PreviousPathRecord.zw) == Candidate\n"
        "                        ? PreviousPathRecord.x : -1.0;\n"
        "                    float ReplayedTarget = ReplayTarget(Replayed);",
        "temporal history evaluator call")
    shader = replace_once(shader,
        "                    if (Weight > 0.0 && isfinite(Weight) && isfinite(Sum + Weight))\n"
        "                    {\n"
        "                        Sum += Weight;\n"
        "                        Count += Retained;\n"
        "                        if (RandomUnit(SelectionBits) * Sum < Weight)\n"
        "                        {\n"
        "                            Selected = Candidate;",
        "                    if (Weight > 0.0 && isfinite(Weight) && isfinite(Sum + Weight))\n"
        "                    {\n"
        "                        float HistoryFreshSum = Sum;\n"
        "                        float HistorySelectionProbability = Weight / (HistoryFreshSum + Weight);\n"
        "                        float HistorySelected = 0.0;\n"
        "                        Sum += Weight;\n"
        "                        Count += Retained;\n"
        "                        if (RandomUnit(SelectionBits) * Sum < Weight)\n"
        "                        {\n"
        "                            HistorySelected = 1.0;\n"
        "                            Selected = Candidate;",
        "capture temporal reservoir choice and probability")
    shader = replace_once(
        shader,
        "                        TemporalReplayed = true;\n"
        "                        TemporalSeed = Candidate;\n"
        "                        TemporalRadiance = Replayed;\n"
        "                        InterlockedAdd(Counters[20], 1);",
        "                        OutputColour[ReplaySlot(5, Pixel)] = HistoryVisibility;\n"
        "                        OutputColour[ReplaySlot(6, Pixel)] = HistoryContribution;\n"
        "                        OutputColour[ReplaySlot(7, Pixel)] = float4(EncodeReplaySeed(Candidate), Source.z, Source.w);\n"
        "                        OutputColour[ReplaySlot(8, Pixel)] = float4(Weight, HistorySelectionProbability, PreviousPathRadiance, HistorySelected);\n"
        "                        TemporalReplayed = true;\n"
        "                        TemporalSeed = Candidate;\n"
        "                        TemporalRadiance = Replayed;\n"
        "                        InterlockedAdd(Counters[20], 1);",
        "per-pixel temporal provenance outputs",
    )
    shader = replace_once(shader,
        "                    float3 CandidateRadiance = TemporalReplayed && Candidate == TemporalSeed\n"
        "                        ? TemporalRadiance : EvaluateReplayPath(R, Candidate);",
        "                    float4 IgnoredVisibility, IgnoredContribution;\n"
        "                    float3 CandidateRadiance = TemporalReplayed && Candidate == TemporalSeed\n"
        "                        ? TemporalRadiance : EvaluateReplayPath(R, Candidate, IgnoredVisibility, IgnoredContribution);",
        "retained-bank evaluator call")
    shader = replace_once(shader,
        "            float3 Radiance = I == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :\n"
        "                                       EvaluateReplayPath(R, DecodeReplaySeed(Source.xy));",
        "            float4 IgnoredVisibility, IgnoredContribution;\n"
        "            float3 Radiance = I == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :\n"
        "                EvaluateReplayPath(R, DecodeReplaySeed(Source.xy), IgnoredVisibility, IgnoredContribution);",
        "spatial evaluator call")
    shader = replace_once(
        shader,
        "    OutputColour[ReplaySlot(4, Pixel)] = float4(R.NormalRoughness.xyz, 0);\n"
        "    FlushSecondaryTraversalCounters(Pixel);",
        "    OutputColour[ReplaySlot(4, Pixel)] = float4(R.NormalRoughness.xyz, 0);\n"
        "    OutputColour[ReplaySlot(9, Pixel)] = float4(Power(max(OutputColour[ReplaySlot(3, Pixel)].xyz, 0.0)),\n"
        "        OutputColour[ReplaySlot(3, Pixel)].w, Local.xy);\n"
        "    FlushSecondaryTraversalCounters(Pixel);",
        "persist prior-frame selected path radiance and seed",
    )
    shader = replace_once(
        shader,
        "    SurfacePoint R = Receivers[Pixel];\n    float4 Packed = 0, SelectedRadiance = 0;",
        "    SurfacePoint R = Receivers[Pixel];\n"
        "    OutputColour[ReplaySlot(5, Pixel)] = 0;\n"
        "    OutputColour[ReplaySlot(6, Pixel)] = 0;\n"
        "    OutputColour[ReplaySlot(7, Pixel)] = 0;\n"
        "    OutputColour[ReplaySlot(8, Pixel)] = 0;\n"
        "    float4 Packed = 0, SelectedRadiance = 0;",
        "per-pixel audit initialization",
    )
    if shader.count("EvaluateReplayPath(R,") != 5 or "SampleReplayFacetDirect(Current" not in shader:
        raise ValueError("Unexpected shader call graph; refusing an incomplete audit variant")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(shader, encoding="utf-8")
    print("GENERATED test-only per-temporal-sample visibility/contribution shader; estimator math shared")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} OUTPUT.slang")
    try:
        generate(sys.argv[1])
    except (OSError, ValueError) as error:
        raise SystemExit(f"FAIL ReplayTemporalVisibilityCandidate: {error}")

#!/usr/bin/env python3
"""Generate isolated test-only shader variants with temporal-reservoir provenance readback."""
import sys
from pathlib import Path

SOURCE = Path("Frontier/Engine/Rendering/Shaders/PathReplayReservoir.slang")


def replace_once(text, old, new, description):
    count = text.count(old)
    if count != 1:
        raise ValueError(f"expected one {description} anchor, found {count}")
    return text.replace(old, new, 1)


def generate(destinations):
    if len(set(destinations)) != 3 or any(Path(p).resolve() == SOURCE.resolve() for p in destinations):
        raise ValueError("Refusing duplicate destinations or Engine source overwrite")
    original = SOURCE.read_text(encoding="utf-8")
    provenance = original
    provenance = replace_once(
        provenance,
        "    SurfacePoint R = Receivers[Pixel];\n    float4 Packed = 0, SelectedRadiance = 0;",
        "    SurfacePoint R = Receivers[Pixel];\n"
        "    // Test-only output slabs 5-8 record proposal and temporal-roulette provenance.\n"
        "    OutputColour[ReplaySlot(5, Pixel)] = 0;\n"
        "    OutputColour[ReplaySlot(6, Pixel)] = 0;\n"
        "    OutputColour[ReplaySlot(7, Pixel)] = 0;\n"
        "    OutputColour[ReplaySlot(8, Pixel)] = 0;\n"
        "    float AuditFreshSum = 0, AuditFreshCount = 0;\n"
        "    float AuditHistoryWeight = 0, AuditHistoryTarget = 0, AuditHistoryRetained = 0;\n"
        "    uint AuditHistoryAccepted = 0, AuditHistorySelected = 0;\n"
        "    float4 Packed = 0, SelectedRadiance = 0;",
        "provenance output initialization",
    )
    provenance = replace_once(
        provenance,
        "        uint Selected = NextRandom(SeedBits);\n        float3 Radiance = EvaluateReplayPath(R, Selected);",
        "        OutputColour[ReplaySlot(6, Pixel)] = float4(EncodeReplaySeed(SelectionBits), 0, 0);\n"
        "        uint Selected = NextRandom(SeedBits);\n"
        "        OutputColour[ReplaySlot(5, Pixel)] = float4(EncodeReplaySeed(Selected), 0, 0);\n"
        "        float3 Radiance = EvaluateReplayPath(R, Selected);",
        "first path/selection seed capture",
    )
    provenance = replace_once(
        provenance,
        "        bool TemporalReplayed = false;",
        "        AuditFreshSum = Sum;\n        AuditFreshCount = Count;\n        bool TemporalReplayed = false;",
        "fresh reservoir summary",
    )
    provenance = replace_once(
        provenance,
        "                    float Weight = ReplayedTarget * Source.z * Retained;\n                    if (Weight > 0.0 && isfinite(Weight) && isfinite(Sum + Weight))",
        "                    float Weight = ReplayedTarget * Source.z * Retained;\n"
        "                    AuditHistoryWeight = Weight;\n"
        "                    AuditHistoryTarget = ReplayedTarget;\n"
        "                    AuditHistoryRetained = Retained;\n"
        "                    if (Weight > 0.0 && isfinite(Weight) && isfinite(Sum + Weight))",
        "temporal re-evaluation weight",
    )
    provenance = replace_once(
        provenance,
        "                    {\n                        Sum += Weight;\n                        Count += Retained;\n                        if (RandomUnit(SelectionBits) * Sum < Weight)\n                        {\n                            Selected = Candidate;",
        "                    {\n"
        "                        AuditHistoryAccepted = 1u;\n"
        "                        Sum += Weight;\n"
        "                        Count += Retained;\n"
        "                        if (RandomUnit(SelectionBits) * Sum < Weight)\n"
        "                        {\n"
        "                            AuditHistorySelected = 1u;\n"
        "                            Selected = Candidate;",
        "temporal acceptance/roulette decision",
    )
    provenance = replace_once(
        provenance,
        "        Packed = float4(EncodeReplaySeed(Selected), Sum / (Count * Target), Count);",
        "        OutputColour[ReplaySlot(7, Pixel)] = float4(AuditFreshSum, AuditFreshCount,\n"
        "                                                        AuditHistoryWeight, AuditHistoryTarget);\n"
        "        float SelectionProbability = AuditFreshSum + AuditHistoryWeight > 0\n"
        "            ? AuditHistoryWeight / (AuditFreshSum + AuditHistoryWeight) : 0;\n"
        "        OutputColour[ReplaySlot(8, Pixel)] = float4(AuditHistoryRetained,\n"
        "            float(AuditHistoryAccepted), float(AuditHistorySelected), SelectionProbability);\n"
        "        Packed = float4(EncodeReplaySeed(Selected), Sum / (Count * Target), Count);",
        "final per-pixel provenance slabs",
    )
    path_variant = replace_once(
        provenance,
        "        uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);",
        "        uint SeedBits = Extent.Sampling.x ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;\n"
        "        SeedBits ^= SeedBits >> 16u;\n"
        "        SeedBits *= 0x7feb352du;\n"
        "        SeedBits ^= SeedBits >> 15u;\n"
        "        SeedBits *= 0x846ca68bu;\n"
        "        SeedBits ^= SeedBits >> 16u;",
        "path-hash stream mapping",
    )
    selection_variant = replace_once(
        provenance,
        "        uint SelectionBits = (Extent.Sampling.x * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);",
        "        uint SelectionBits = Extent.Sampling.x ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;\n"
        "        SelectionBits ^= SelectionBits >> 16u;\n"
        "        SelectionBits *= 0x7feb352du;\n"
        "        SelectionBits ^= SelectionBits >> 15u;\n"
        "        SelectionBits *= 0x846ca68bu;\n"
        "        SelectionBits ^= SelectionBits >> 16u;",
        "selection-hash stream mapping",
    )
    # Guard the experiment: instrumentation must leave estimator logic shared across all variants.
    if path_variant.replace(
        "        uint SeedBits = Extent.Sampling.x ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;\n"
        "        SeedBits ^= SeedBits >> 16u;\n"
        "        SeedBits *= 0x7feb352du;\n"
        "        SeedBits ^= SeedBits >> 15u;\n"
        "        SeedBits *= 0x846ca68bu;\n"
        "        SeedBits ^= SeedBits >> 16u;",
        "        uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);",
        1,
    ) != provenance:
        raise ValueError("Path-hash instrumentation changed more than the path-seed mapping")
    if selection_variant.replace(
        "        uint SelectionBits = Extent.Sampling.x ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;\n"
        "        SelectionBits ^= SelectionBits >> 16u;\n"
        "        SelectionBits *= 0x7feb352du;\n"
        "        SelectionBits ^= SelectionBits >> 15u;\n"
        "        SelectionBits *= 0x846ca68bu;\n"
        "        SelectionBits ^= SelectionBits >> 16u;",
        "        uint SelectionBits = (Extent.Sampling.x * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);",
        1,
    ) != provenance:
        raise ValueError("Selection-hash instrumentation changed more than the selection mapping")
    for destination, text in zip(destinations, (provenance, path_variant, selection_variant)):
        path = Path(destination)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print("GENERATED baseline/path-hash/selection-hash provenance shaders; estimator logic is shared")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} BASELINE_PATH PATH_HASH_PATH SELECTION_HASH_PATH")
    try:
        generate(sys.argv[1:])
    except (OSError, ValueError) as error:
        raise SystemExit(f"FAIL ReplayTemporalProvenanceCandidate: {error}")

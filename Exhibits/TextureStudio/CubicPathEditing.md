# Retained cubic-path editing

Shared production geometry is in `Engine/ContentInterchange/CubicPathEditing.{h,cpp}`.
Native Point/Pen integration and current evidence are documented in [NativePatternWorkspace.md](NativePatternWorkspace.md).

## Supported source operations

- Decode/construct open or closed paths; move anchors and mirrored/independent tangents; corner/smooth controls.
- Shape-preserving de Casteljau subdivision, anchor removal and explicit primitive conversion.
- Retained endpoint continuation and direction reversal; compatible adjacent open-path joining in four orientations.
- `ProjectCubicSegment`: bounded approximate interior projection across all open/closed segments.
- `BendCubicSegment`: fixed anchors, minimum squared control displacement at the requested interior parameter.
  Only the two controls belonging to that segment change; adjacent tangent continuity is not forced.
- Shared atomic `TranslateCubicAnchors` and `PartitionCubicAnchors` validate unique, nonempty anchor batches.
- `ConfigureAutomaticCubicTangents` derives aligned controls from neighbouring anchor positions and adjacent
  chord lengths; open endpoints are one-sided and closed neighbours wrap. Unselected anchors remain unchanged.
- Atomic copy-on-write source replacement preserves independent aliases, masks, paint and channel descriptors.
  Unreachable cubic storage is reclaimed. No CPU authored-pixel rasterization or schema changes.

## Current verification

| Verification | Result |
|---|---|
| Strict cubic validation | 27,708 assertions passed |
| ASan/UBSan and leak detection | 27,708 assertions passed |
| Windows production cubic translation unit | Compiled; not linked/executed |

Tests include rotated/open/closed geometry, repeated-edit storage bounds, shared aliases and masks, source history,
all four join orientations, closed seams, projected parameters, fixed endpoints, unrelated tangent preservation,
nonfinite/invalid parameter refusal and exact sampled cubic subdivision.

```bash
python3 Exhibits/TextureStudio/Code/VerifyCubicPath.py --windows-tools _ClaudeScratch/tmp/PatternPython
```

Toolchain, commands and source/header fingerprints: `Results/CubicPathChecks.json`.

## Boundaries

- Projection uses 128 samples per segment and bounded local refinement; it is not an exact closest-point solver.
- Bending excludes the outer 2% at each endpoint and does not automatically smooth adjoining segments.
- Compound subpaths are refused rather than silently flattened; ellipse conversion is a four-cubic approximation.
- Persistent automatic-anchor modes, cross-path/marquee point selection, broader join semantics and full Pattern parity remain open.
- Full Windows application linking/execution is not established. Stamp remains deferred.

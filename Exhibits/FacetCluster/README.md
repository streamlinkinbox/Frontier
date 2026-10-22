# FacetCluster — K1 geometry-build evidence

Regenerate everything on this page from a clean checkout:

```bash
bash Executables/Validation/FacetClusterValidation/Build.sh --dump /tmp/native-level0.json
```

## Result

`validation-report.txt` — the gate's own transcript. Exit 0, all five checks pass.

| Corpus | Facets | Welded vertices | Clusters | Largest cluster | Violations | Time |
|---|---:|---:|---:|---|---:|---:|
| sphere | 5120 | 2562 | 48 | 128 facets / 87 vertices | 0 | 7.3 ms |
| heightfield | 18432 | 9409 | 159 | 128 facets / 100 vertices | 0 | 21.6 ms |
| seam-cylinder | 1920 | 1008 | 16 | 128 facets / 108 vertices | 0 | 2.1 ms |

Caps are 128 facets / 256 welded vertices. Boundary edges on the heightfield are 384 (four perimeter runs of 96); the seam
cylinder reports 144 sharp and 20 UV-seam edges.

## Reference parity

The reference is not a description here — it is executed. `reference-parity.txt` holds both steps:

1. **Replay.** `Reference/ReplayReference.mjs` runs the HTML builder
   (`Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js`, commit `c4ef131`) over the same two reference corpora
   in Node and dumps each level-0 cluster's membership, enclosure box, sphere and cone.
2. **Compare.** `Reference/CompareLevel0.py` compares that dump with the native `--dump` output, cluster by cluster.

| Corpus | Clusters | Membership mismatches | Max deviation: centre | radius | cone axis | cutoff | box |
|---|---:|---:|---|---|---|---|---|
| sphere | 48 | **0** | 1.11e-16 | 1.11e-16 | 6.66e-16 | 6.66e-16 | 1.11e-16 |
| heightfield | 159 | **0** | 4.44e-16 | 8.88e-16 | 1.06e-15 | 6.66e-16 | 1.78e-15 |

Deviation is in the units of the values compared (metres for positions and radii, none for axis components and cutoff).
Every figure is double round-off; nothing is fitted.

This is the property the earlier attempt could not claim. `c4ef131` reported 63 clusters for the same sphere where the
reference builds 48, and its validation program contained the only expectations it was judged against.

### Reproducing the replay

The reference sources live on the HTML line, not this one:

```bash
git show c4ef131:Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js       > /tmp/ref/GeometryHierarchy.mjs
git show c4ef131:Experimental/VehicleHmiPrototype/Tools/GeometryHierarchyCorpus.mjs > /tmp/ref/GeometryHierarchyCorpus.mjs
cp Exhibits/FacetCluster/Reference/ReplayReference.mjs /tmp/ref/
node /tmp/ref/ReplayReference.mjs /tmp/ref/level0-reference.json
python3 Exhibits/FacetCluster/Reference/CompareLevel0.py /tmp/ref/level0-reference.json /tmp/native-level0.json
```

## Condition of these measurements

- Compiler `g++ 12.2.0`, `-std=c++20 -O2 -Wall -Wextra`; also compiles clean under `-Wpedantic -Wshadow -Wconversion`.
- Node `v22.22.3` for the reference replay.
- Platform: Linux x86-64 sandbox, 2 cores. No GPU, no Vulkan, no CMake, no MSVC — none are needed by this gate.
- Tested revision: this branch (`arena/36ca0063-frontier`), `Engine/Geometry/FacetClusterBuild.{h,cpp}` as committed.
- Timings are single-run wall clock on a shared sandbox core. They are indicative only; nothing in this exhibit depends
  on them.

## What this does not establish

- Level 1 and coarser tiers. K1 builds tier 0 only; the quadric simplification that produces the reference's
  5120 → 2558 → 1280 → 640 → 320 → 160 → 80 progression is K1b and is not implemented.
- GPU behaviour of any kind. No cluster has been uploaded, culled or drawn.
- Windows compilation. This code has been built with g++ only; the MSVC gate (`Build.bat`) is written but has not run
  here.

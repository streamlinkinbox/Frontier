# SolidArc renderings

🖼 What SolidArc draws, rendered by its own software raster — no GPU, no window, no display. These
are the pictures behind "build SolidArc, I want to see its UI".

## How to make them

```bash
S=Editor/AuthoringTools/Modelling/SolidArc
g++ -std=c++20 -O2 -w -DSOLIDARC_PROOF_FOLDER='"Exhibits/Gallery/SolidArcNative"' \
    -I$S -I$S/Presentation $S/Kernel/*.cpp $S/Presentation/*.cpp $S/Interaction/*.cpp \
    $S/Document/*.cpp $S/Console/*.cpp -o .cache/solidarcbuild/SolidArc
.cache/solidarcbuild/SolidArc --proofs Exhibits/Gallery/SolidArcNative $S/Samples/ToyCar.arc
```

Any `.arc` journal replays the same way. The twelve scripts under `Scripts/` and the three models
under `Samples/` replay with **zero refusals** and produce 71 images between them; the folder here
carries the readable subset and CI publishes the rest as the `solidarc-ui-captures` artifact.

## What is in the folder

| Picture | What it shows |
| --- | --- |
| `Phase10_ContactSheet.png` | Primitives, sketch overlay, matcap studios and the ground grid in four panes |
| `Phase12_ContactSheet.png` | Arrays and bridges |
| `Phase13_ContactSheet.png`, `Phase13b_ContactSheet.png` | Dimensions and their redo path |
| `Proof_06a_Primitives.png` | The primitive roster in isometric |
| `Proof_03b_Sketch_Iso.png` | Sketching on a plane, with the live preview |
| `Phase8_Skins_Iso.png` | Lofted skins |
| `Phase9_Booleans_Iso.png` | Boolean union, difference and intersection |
| `Edge_Top_Fillet.png`, `Spanner_Fillet.png`, `Root_Concave_Fillet_Detail.png` | Fillets, including the concave root the kernel is easiest to get wrong on |
| `ToyCar_*`, `Biplane_*`, `Boat_*` | Three models from first sketch to final views: `_Hero` is the dimensioned beauty shot, `_A`/`_B`/`_C` are the construction, feature and final-view sheets, and the numbered frames are each modelling stage |

## Where it is checked

- Linux, every push: the `Frontier, Project-Zero, Project-Drive, Project-Dyno, SolidArc` job builds
  `SolidArc` plus `KernelVerification`, `RasterVerification`, `InteractionVerification`,
  `SelectionVerification` and `TopologyVerification`, runs all five, then renders the contact sheets.
- Windows, every push: `Tools/Build/CheckMsvcSolidArc.py` compiles the whole kernel under
  `cl.exe /std:c++20 /MD /W4 /WX` and runs six verifications. The Liger replay it also knows how to
  do is skipped here, because `Projects/Project-Drive/Content/Vehicles/Liger` is not in this
  checkout.

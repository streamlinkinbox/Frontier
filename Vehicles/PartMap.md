# Vehicle part map (from the .blend files, read headlessly with bpy 5.0.1)

All three files are Blender 5.2.26-alpha; bpy 5.0.1 opens them and evaluates the
full modifier stacks (mirror, subsurf, shrinkwrap, mask, geometry nodes).
Units: Blender units ≈ cm. Axes: +X = front, +Z = up, Y = across (mirror plane Y≈0,
except Liger whose mirror plane is at Y ≈ -48).

Scope legend: **BODY** = exterior main body (in scope) · AERO = spoilers/splitters/
diffusers (out) · WHEEL · INT = interior · GUIDE/JUNK = empty or helper meshes.

## Quicksilver  (47 objects → 32 real meshes; 466 k verts evaluated)
Overall: L 4.65 m (X −174…+291), W 2.40 m, H 1.47 m to wing / 1.22 m roof.

| Blender name | → New name | Class | What it is |
|---|---|---|---|
| TrailingBody.Guide (255k v) | Body_Main_Shell | BODY | Whole mid/rear shell: roof, rear quarters, rear fascia, sills. Same shape as `TrailingBody` (53k) which is its masked/shrink-wrapped working copy – keep ONE. |
| TrailingBody | Body_Main_Shell (dup) | BODY | Working copy of above (MASK cut). |
| Lead-Body (78k) | Body_Front_Clip | BODY | Nose: bonnet, front wings, front fascia (X 141…291). |
| Final Body Guide.004 (16k) | Body_Door_Pillar_Frame | BODY | Door opening + A/B pillar + roof-rail frame, and the inner sill/floor strip. |
| TrailingBody.001 (13k) | Body_Door_Skin | BODY | Door outer panel (X −2…140, Z 35…112). |
| Final Body Guide.005 (3.6k) | Body_Side_Intake_Scoop | BODY | Side intake pocket behind door (X 15…147, mirrored). |
| TrailingBody.002 (6.4k) | Body_Rear_Deck | BODY | Rear engine-cover deck + rear header (X −162…−73, Z 69…106). |
| TrailingBody.006 (6.4k) | Body_Rear_Deck_Louvres | BODY (detail) | Louvred slots in rear deck, U-shaped surround. |
| TrailingBody.007 / .008 | Body_Rear_Deck_Lip / Body_Rear_Header_Lip | BODY (detail) | Thin lip strips at the rear deck edges. |
| TrailingBody.003 (4.9k) | Body_Door_Window_Surround + Body_Front_Splitter_Edge | BODY | Window surround along the door top and a small strip at the nose tip (one object, two features – split it). |
| TrailingBody.004 (0.9k) | Glass_Roof_Scoop | BODY (glass) | Small roof-scoop/skylight at X 78…119. |
| TrailingBody.005 (4k) | Glass_Windscreen | BODY (glass) | Windscreen (X 76…186, Z 83…112). |
| TopGlass.001 | – | GUIDE | empty mesh. |
| LeadEdge.002 (4.3k) | Body_Front_Bumper_Lower | BODY | Lower nose/bumper under Lead-Body, includes the under-nose lip (X 153…271). |
| LeadEdge / LeadEdge.001 | Body_Sill_Rail / Body_Front_Lip | BODY (lower) | Flat under-sill rail (X −41…154, Z 13…36) and small front lip plate. |
| Front Header (126 v) | Body_Front_Grille_Frame | BODY (detail) | Front grille/header opening frame. |
| Exterior02.002 / .004 (5k / 3.8k) | Body_Rear_Diffuser_Fence / Body_Front_Splitter_End | AERO | Low plates at rear corners and under the nose corners. |
| Exterior02.003 | Body_Rear_Deck_Vent | BODY (detail) | Tiny vent at rear deck centre. |
| FuelPanel | Body_Fuel_Door | BODY (detail) | Fuel filler door on rear quarter (X −38…−20, Z 77…95). |
| RearSpoilers, RearSpoilers.005, Aero-Mount, Cylinder | – | AERO | Rear wing, end-plates, swan-neck mounts, pivot. |
| StandardTyre.001 / .004 | Wheel_RR / Wheel_FR (mirrored) | WHEEL | Tyres (Ø ≈ 84, width ≈ 30). |
| G3, G3.002, G3.003 | Chassis_Points | JUNK | Scattered verts / geometry-node instancer sources. |
| Cube.001, Circle.004, Trail-Body.001, FrontBody.002, all other `Final Body Guide.*` | – | GUIDE | Empty or 1-face helper meshes. |

**Body for CAD = Body_Main_Shell + Body_Front_Clip + Body_Door_Skin + Body_Door_Pillar_Frame
+ Body_Side_Intake_Scoop + Body_Rear_Deck + Body_Front_Bumper_Lower + Body_Sill_Rail (+ glass).**

## Egoist  (52 objects → 46 meshes; 125 k verts)
Overall: L 4.56 m (X −222…+234), W 2.40 m, roof Z 122, wing to 140.

| Blender name | → New name | Class | What it is |
|---|---|---|---|
| GuideSurface.005 (57k) | Body_Main_Shell | BODY | One-piece exterior: nose, wings, roof, rear deck, tail (X −216…234). |
| GuideSurface.004 (5.4k) | Body_Door_Pillar_Frame | BODY | Door aperture / A-pillar / sill frame + rear quarter return. |
| GuideSurface.007 (4.4k) | Body_Door_Window_Frame | BODY | Door window frame/surround (X −16…156, Z 71…111). |
| GuideSurface.008 / .010 | Body_Door_Skin / Body_Door_Skin_Lower | BODY | Door panel surfaces (X −18…97). |
| GuideSurface.009 | Body_Side_Intake | BODY | Side intake behind door (X 89…103). |
| GuideSurface.001 (6.2k) | Glass_Roof_Panel | BODY (glass) | Roof glass/panel (X 9…88, Z 108…121). |
| Guide.011 (86 v) / Guide.001 | Glass_Windscreen / Body_Cowl | BODY | Windscreen patch and cowl under it. |
| Guide.016 | Body_Rear_Deck | BODY | Rear engine cover (X −204…−65). |
| Guide.002 | Body_Rear_Window | BODY (glass) | Rear screen (X −90…11, Z 96…122). |
| Guide.005 / .012 / .014, Circle.003/.004/.007 | Body_Bonnet_Vent, Body_Scoop_* | BODY (detail) | Small vents/scoops on bonnet & roof. |
| Guide.013 (24k) | Interior_Tub | INT | Cabin tub/dash/seat block (X −53…122). |
| Circle.010, Cube.004/.005/.006/.011, Plane.030/.032, SteeringWheel.002, SpectatorCam* | – | INT | Steering wheel, dash, mirrors, seats, cameras. |
| FrontAero, RearAero.001/.002/.003, SideSkirt, Spoiler, Fins, Circle.050, Circle.047 | – | AERO | Splitter, diffuser, skirts, wing, fins. |
| Cylinder.007/.008/.017, StandardTyre.002/.007/.008, FF | – | WHEEL | Rims, tyres (one tyre is a spare lying in the engine bay), arch liner. |
| Cube.001, Text.*, Camera, WindowFrame.001 | – | JUNK | |

## Liger  (26 objects → 18 meshes; 137 k verts)
Overall: L 4.45 m (X −184…+260), W 2.32 m (Y −164…68), roof Z 116.

| Blender name | → New name | Class | What it is |
|---|---|---|---|
| Body.001 (71k) | Body_Main_Shell | BODY | Entire exterior shell incl. nose, roof, tail. |
| Body.004 (1.3k) | Body_Front_Cowl | BODY | Cowl/scuttle strip ahead of screen (X 102…137). |
| ExteriorAssembly.001 | Body_Roof_Glass_Frame | BODY | Roof + screen frame (X −13…144, Z 81…107). |
| Plane.030 (37k) / Plane.004 (10k) | Interior_Seats / Interior_Tub | INT | Seats and cabin floor/tub. |
| Final Body Guide.001/.003/.005, Guide.004, Plane.010/.013 | – | INT | Dash, centre console, mirror, helpers. |
| Cylinder.003 | Wheel_Rims | WHEEL | All four rims in one object. |
| StandardTyre.001/.002 | Wheel_F / Wheel_R | WHEEL | Tyres. |
| RearFenders, Sideskirts, Spoilers, Circle.001/.017/.018 | – | AERO | Rear fender flares, skirts, wing, splitter. |

Per-part thumbnail sheets: `*_parts_p*.png` (red = that object, grey = whole car).

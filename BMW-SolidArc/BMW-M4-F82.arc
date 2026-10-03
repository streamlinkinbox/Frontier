echo ════ SolidArc · BMW M4 Coupe F82 · blueprint trace → NURBS solids (1 unit = 1 m)
echo Source: the-blueprints.com BMW M4 Coupe F82 (2014) 2D CAD drawing, dims in mm
echo L=4.671  WB=2.812  H=1.383  W=1.870  ·  axles at x=1.048 / x=3.860 from rear bumper
gizmo off
show iso off

echo ── 1. side-elevation trace on XZ: closed interpolating spline through blueprint points
workplane xz --origin=(0,-1.0,0)
spline (4.639,0.149) (4.672,0.240) (4.678,0.330) (4.672,0.430) (4.655,0.470) (4.560,0.520) (4.300,0.610) (3.900,0.720) (3.550,0.800) (3.161,0.960) (2.742,1.320) (2.450,1.372) (2.231,1.380) (1.766,1.371) (1.301,1.255) (0.930,1.078) (0.511,1.023) (0.139,1.041) (0.009,0.930) (0.000,0.790) (0.000,0.660) (0.012,0.540) (0.037,0.428) (0.163,0.186) (0.511,0.158) (1.394,0.177) (2.200,0.180) (2.975,0.177) (4.137,0.158) --closed --degree=3 --name=Side
circle (3.860,0.335) 0.370 --name=ArchF
circle (1.048,0.335) 0.370 --name=ArchR
view front
view fit
render BMW_01_SideTrace
render sheet 0

echo ── 1b. 2D boolean: cut the wheel arches out of the silhouette
boolean subtract Side -- ArchF ArchR --name=Silhouette
profile Silhouette
view front
view fit
render BMW_02_Silhouette

echo ── 1c. 2D boolean: clip the silhouette at the beltline into lower body + greenhouse bands
rect (-1.0,-1.0) (6.0,0.820) --name=LowRect
boolean intersect Silhouette -- LowRect --keep --name=LowSil
rect (-1.0,0.780) (6.0,2.0) --name=GreenRect
boolean intersect Silhouette -- GreenRect --keep --name=GreenSil
hide LowRect
hide GreenRect
hide Silhouette
profile LowSil
profile GreenSil
view front
view fit
render BMW_02b_Bands

echo ── 2. extrude both bands across the full 1.870 m body width
extrude LowSil 2.0 --direction=(0,1,0) --name=LowSlab
extrude GreenSil 2.0 --direction=(0,1,0) --name=GreenSlab
hide LowSil
hide GreenSil
matcap LowSlab clay
matcap GreenSlab clay
view iso
view orbit 125 -12
view fit
render BMW_03_Slab
render sheet 1

echo ── 3. plan-view trace on XY (full-width body plan), extruded through the height
workplane xy --origin=(0,0,-0.5)
spline (0.000,-0.600) (0.300,-0.780) (1.050,-0.935) (1.600,-0.935) (2.200,-0.935) (2.800,-0.935) (3.300,-0.935) (3.900,-0.930) (4.400,-0.800) (4.620,-0.620) (4.671,-0.400) (4.671,0.000) (4.671,0.400) (4.620,0.620) (4.400,0.800) (3.900,0.930) (3.300,0.935) (2.800,0.935) (2.200,0.935) (1.600,0.935) (1.050,0.935) (0.300,0.780) (0.000,0.600) (0.000,0.300) (0.000,-0.300) --closed --degree=3 --name=Plan
extrude Plan 2.5 --direction=(0,0,1) --name=PlanBlock
hide Plan
view top
view fit
render BMW_03b_PlanSketch
render sheet 2

echo ── 4. three-view intersection: side bands ∩ plan ∩ front elevation
boolean intersect LowSlab -- PlanBlock --name=LowerRaw
topology LowerRaw
spline (1.100,-0.400) (1.500,-0.600) (2.200,-0.700) (2.800,-0.700) (3.100,-0.660) (3.520,-0.520) (3.520,0.000) (3.520,0.520) (3.100,0.660) (2.800,0.700) (2.200,0.700) (1.500,0.600) (1.100,0.400) (1.100,0.000) --closed --degree=3 --name=CabinPlan
extrude CabinPlan 2.5 --direction=(0,0,1) --name=CabinBlock
hide CabinPlan
boolean intersect GreenSlab -- CabinBlock --name=Cabin
topology Cabin
matcap LowerRaw clay
matcap Cabin glass
tint Cabin 0.10 0.12 0.14
view iso
view orbit 125 -12
view fit
render BMW_04_TwoBlocks
render sheet 3
render sheet finalize BMW_A_Construction

echo ── 5. wheels, rims at the blueprint axle stations
cylinder (3.860,0.680,0.335) 0.335 0.255 --axis=(0,1,0) --name=TyreFL
cylinder (3.860,-0.935,0.335) 0.335 0.255 --axis=(0,1,0) --name=TyreFR
cylinder (1.048,0.660,0.335) 0.335 0.275 --axis=(0,1,0) --name=TyreRL
cylinder (1.048,-0.935,0.335) 0.335 0.275 --axis=(0,1,0) --name=TyreRR
cylinder (3.860,0.855,0.335) 0.235 0.080 --axis=(0,1,0) --name=RimFL
cylinder (3.860,-0.935,0.335) 0.235 0.080 --axis=(0,1,0) --name=RimFR
cylinder (1.048,0.855,0.335) 0.235 0.080 --axis=(0,1,0) --name=RimRL
cylinder (1.048,-0.935,0.335) 0.235 0.080 --axis=(0,1,0) --name=RimRR
matcap TyreFL rubber
matcap TyreFR rubber
matcap TyreRL rubber
matcap TyreRR rubber
matcap RimFL chrome
matcap RimFR chrome
matcap RimRL chrome
matcap RimRR chrome
view iso
view orbit 125 -12
view fit
render BMW_05_Wheels
render sheet 0

echo ── 6. details: kidney grille recesses, door mirrors, exhaust tips
box (4.520,-0.280,0.300) (4.740,-0.080,0.400) --name=KidneyR
box (4.520,0.080,0.300) (4.740,0.280,0.400) --name=KidneyL
boolean subtract LowerRaw -- KidneyR --name=LowK1
boolean subtract LowK1 -- KidneyL --name=Lower
topology Lower
box (2.980,0.800,0.760) (3.220,0.960,0.900) --name=MirrorL
box (2.980,-0.960,0.760) (3.220,-0.800,0.900) --name=MirrorR
cylinder (-0.020,-0.380,0.200) 0.045 0.250 --axis=(1,0,0) --name=PipeR
cylinder (-0.020,0.290,0.200) 0.045 0.250 --axis=(1,0,0) --name=PipeL
matcap Lower plastic-white
tint Lower 0.13 0.27 0.50
matcap MirrorL pearl
tint MirrorL 0.13 0.27 0.50
matcap MirrorR pearl
tint MirrorR 0.13 0.27 0.50
matcap PipeR steel
matcap PipeL steel
list
view iso
view orbit 125 -12
view fit
render BMW_06_Details
render sheet 1

echo ── 7. proof views: elevations + contact sheet + hero
view front
view fit
render BMW_07_SideElevation
render sheet 2
view top
view fit
render sheet 3
render sheet finalize BMW_B_Features
view top
view fit
render BMW_08_Top
view right
view fit
render BMW_09_FrontElevation
view iso
view orbit 115 -14
view fit
view dolly 0.8
render BMW_10_Hero --size=1600x1000
view iso
view orbit 245 -14
view fit
view dolly 0.8
render BMW_11_HeroRear --size=1600x1000

echo ── 8. deliverables: native .arc document + tessellated OBJ
save /home/user/Frontier/BMW-SolidArc/out/BMW-M4-F82.arc
export /home/user/Frontier/BMW-SolidArc/out/BMW-M4-F82.obj --chord=0.002 --weld
echo ════ done

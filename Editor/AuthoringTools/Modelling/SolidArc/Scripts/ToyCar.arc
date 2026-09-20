echo ════ SolidArc · wooden toy car · sketch → solid → booleans → parts (1 unit = 1 cm)
gizmo off
show iso off

echo ── 1. side-profile sketch on the XZ workplane: polyline, per-corner fillets, two arch circles
workplane xz --origin=(0,4,0)
polyline (0.3,0.7) (20.3,0.7) (20.3,2.6) (12.5,3.4) (9.8,5.3) (7.8,5.3) (0.3,3.1) --closed --name=Side
fillet Side 0.6 --corners=2
fillet Side 1.5 --corners=0
fillet Side 1.5 --corners=0
fillet Side 2.0 --corners=0
fillet Side 0.5 --corners=0
fillet Side 0.4 --corners=1
circle (4.4,1.2) 1.55 --name=ArchR
circle (15.2,1.2) 1.55 --name=ArchF
view front
view fit
render ToyCar_1_SideSketch
render sheet 0
echo ── 1b. 2D boolean: subtract the arch circles from the silhouette (exact profile algebra)
boolean subtract Side -- ArchR ArchF --name=Silhouette
profile Silhouette

echo ── 2. extrude the silhouette 8 cm across the car (rational arcs become exact extrusion faces)
extrude Silhouette 8 --name=Block
topology Block
hide Silhouette
matcap Block clay
view iso
view orbit 125 0
view fit
render ToyCar_2_Extruded
render sheet 1

echo ── 3. plan-view sketch on XY: tapered nose, rounded corners, extruded through the block height
workplane xy --origin=(0,0,-0.5)
polyline (10,-3.9) (14,-3.9) (20,-3.3) (20,3.3) (14,3.9) (0.6,3.9) (0.6,-3.9) --closed --name=Plan
fillet Plan 4.0 --corners=0
fillet Plan 1.0 --corners=0
fillet Plan 1.0 --corners=0
fillet Plan 4.0 --corners=0
fillet Plan 0.8 --corners=0
fillet Plan 0.8 --corners=0
hide Block
view top
view fit
render ToyCar_3_PlanSketch
render sheet 2
unhide Block
extrude Plan 7 --name=PlanBlock
hide Plan
matcap PlanBlock steel

echo ── 4. boolean intersect: the side view and the plan view carve one body
boolean intersect Block -- PlanBlock --name=Body
topology Body
matcap Body clay
view iso
view orbit 125 0
view fit
render ToyCar_4_Intersected
render sheet 3
render sheet finalize ToyCar_A_Construction

echo ── 5. boolean union: two axle bearing blocks close the arches between the wheels
box (2.3,-2.6,0.8) 4.2 5.2 2.05 --name=BearingR
box (13.1,-2.6,0.8) 4.0 5.2 2.05 --name=BearingF
boolean union Body -- BearingR --name=Hull1
boolean union Hull1 -- BearingF --name=Hull
topology Hull

echo ── 6. boolean subtract: axle bores through both bearing blocks
cylinder (4.4,-5,1.2) 0.32 10 --axis=(0,1,0) --name=BoreR
cylinder (15.2,-5,1.2) 0.32 10 --axis=(0,1,0) --name=BoreF
boolean subtract Hull -- BoreR --name=Bored1
boolean subtract Bored1 -- BoreF --name=Bored
topology Bored
matcap Bored clay
view iso
view orbit 160 -78
view fit
view dolly 1
render ToyCar_5_Underside
render sheet 0

echo ── 7. boolean subtract: six grille slots cut into the nose face
box (19.5,-2.15,1.0) 0.7 0.3 0.9 --name=G1
box (19.5,-1.65,1.0) 0.7 0.3 0.9 --name=G2
box (19.5,-1.15,1.0) 0.7 0.3 0.9 --name=G3
box (19.5,0.85,1.0) 0.7 0.3 0.9 --name=G4
box (19.5,1.35,1.0) 0.7 0.3 0.9 --name=G5
box (19.5,1.85,1.0) 0.7 0.3 0.9 --name=G6
boolean subtract Bored -- G1 --name=S1
boolean subtract S1 -- G2 --name=S2
boolean subtract S2 -- G3 --name=S3
boolean subtract S3 -- G4 --name=S4
boolean subtract S4 -- G5 --name=S5
boolean subtract S5 -- G6 --name=Shell
topology Shell
matcap Shell clay
view iso
view orbit 150 -20
view fit
view dolly 2
render ToyCar_6_Grille
render sheet 1

echo ── 8. parts: wheels, dowel hubs and axles, each set unioned into one solid
cylinder (4.4,-3.85,1.2) 1.35 1.0 --axis=(0,1,0) --name=WheelRL
cylinder (4.4,2.85,1.2) 1.35 1.0 --axis=(0,1,0) --name=WheelRR
cylinder (4.4,-4.0,1.2) 0.4 0.17 --axis=(0,1,0) --name=HubRL
cylinder (4.4,3.83,1.2) 0.4 0.17 --axis=(0,1,0) --name=HubRR
cylinder (4.4,-3.5,1.2) 0.3 7.0 --axis=(0,1,0) --name=AxleR
boolean union WheelRL -- AxleR --name=WR1
boolean union WR1 -- WheelRR --name=WR2
boolean union WR2 -- HubRL --name=WR3
boolean union WR3 -- HubRR --name=RearSet
topology RearSet
cylinder (15.2,-3.85,1.2) 1.35 1.0 --axis=(0,1,0) --name=WheelFL
cylinder (15.2,2.85,1.2) 1.35 1.0 --axis=(0,1,0) --name=WheelFR
cylinder (15.2,-4.0,1.2) 0.4 0.17 --axis=(0,1,0) --name=HubFL
cylinder (15.2,3.83,1.2) 0.4 0.17 --axis=(0,1,0) --name=HubFR
cylinder (15.2,-3.5,1.2) 0.3 7.0 --axis=(0,1,0) --name=AxleF
boolean union WheelFL -- AxleF --name=WF1
boolean union WF1 -- WheelFR --name=WF2
boolean union WF2 -- HubFL --name=WF3
boolean union WF3 -- HubFR --name=FrontSet
topology FrontSet
matcap RearSet clay
matcap FrontSet clay
tint RearSet 0.93 0.80 0.62
tint FrontSet 0.93 0.80 0.62
hide Shell
view iso
view fit
render ToyCar_7_Wheelsets
render sheet 2
unhide Shell

echo ── 9. final assembly
tint Shell 0.90 0.76 0.56
list
view iso
view orbit 95 -17
view fit
view dolly 0.7
render ToyCar_8_Final
render sheet 3
render sheet finalize ToyCar_B_Features

echo ── 10. final views contact sheet
view iso
view orbit 95 -17
view fit
view dolly 0.7
render sheet 0
view iso
view orbit 205 -17
view fit
view dolly 0.7
render sheet 1
view right
view fit
view dolly 1.5
render sheet 2
view top
view fit
view dolly 1
render sheet 3
render sheet finalize ToyCar_C_FinalViews
view iso
view orbit 100 -18
view fit
view dolly 1.2
render ToyCar_Hero --size=1920x1200
save build/ToyCar.model.arc

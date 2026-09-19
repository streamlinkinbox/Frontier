echo ── 1. side-profile sketch on the XZ workplane (1 unit = 1 cm)
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
boolean subtract Side -- ArchR ArchF --name=Silhouette
profile Silhouette
echo ── 2. extrude the silhouette across the car width
extrude Silhouette 8 --name=Block
topology Block
echo ── 3. plan-view sketch on XY and the second extrusion
workplane xy --origin=(0,0,-0.5)
polyline (10,-3.9) (14,-3.9) (20,-3.3) (20,3.3) (14,3.9) (0.6,3.9) (0.6,-3.9) --closed --name=Plan
fillet Plan 4.0 --corners=0
fillet Plan 1.0 --corners=0
fillet Plan 1.0 --corners=0
fillet Plan 4.0 --corners=0
fillet Plan 0.8 --corners=0
fillet Plan 0.8 --corners=0
extrude Plan 7 --name=PlanBlock
echo ── 4. boolean intersect: the two views carve the body
boolean intersect Block -- PlanBlock --name=Body
topology Body
echo ── 5. chassis block union: closes the arches between the wheels
box (2.5,-2.6,0.15) 14.5 5.2 2.65 --name=Chassis
boolean union Body -- Chassis --name=Hull
topology Hull
echo ── 6. axle bores through the chassis
cylinder (4.4,-5,1.2) 0.32 10 --axis=(0,1,0) --name=BoreR
boolean subtract Hull -- BoreR --name=Bored1
cylinder (15.2,-5,1.2) 0.32 10 --axis=(0,1,0) --name=BoreF
boolean subtract Bored1 -- BoreF --name=Bored
topology Bored
echo ── 7. grille: six slots cut into the nose face
box (19.5,-2.15,1.0) 0.7 0.3 0.9 --name=G1
boolean subtract Bored -- G1 --name=S1
box (19.5,-1.65,1.0) 0.7 0.3 0.9 --name=G2
boolean subtract S1 -- G2 --name=S2
box (19.5,-1.15,1.0) 0.7 0.3 0.9 --name=G3
boolean subtract S2 -- G3 --name=S3
box (19.5,0.85,1.0) 0.7 0.3 0.9 --name=G4
boolean subtract S3 -- G4 --name=S4
box (19.5,1.35,1.0) 0.7 0.3 0.9 --name=G5
boolean subtract S4 -- G5 --name=S5
box (19.5,1.85,1.0) 0.7 0.3 0.9 --name=G6
boolean subtract S5 -- G6 --name=Shell
topology Shell
list

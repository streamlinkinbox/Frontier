echo Kernel defect reproducer: this union order segfaults instead of refusing (wheels+hubs first, then axle, then the other wheel)
cylinder (4.4,-3.85,1.2) 1.35 1.0 --axis=(0,1,0) --name=WL
cylinder (4.4,2.85,1.2) 1.35 1.0 --axis=(0,1,0) --name=WR
cylinder (4.4,-4.0,1.2) 0.4 0.17 --axis=(0,1,0) --name=HL
cylinder (4.4,3.83,1.2) 0.4 0.17 --axis=(0,1,0) --name=HR
cylinder (4.4,-3.5,1.2) 0.3 7.0 --axis=(0,1,0) --name=AX
boolean union WL -- HL --name=WR1
boolean union WR -- HR --name=WR2
boolean union WR1 -- AX --name=WR3
boolean union WR3 -- WR2 --name=RearSet

# SolidArc native document v1
# High-bypass turbofan — modelled from CAD reference renders (FetchCFD P&W rotor assembly; turbofan cutaway).
# Right-handed, Z up. Engine axis = +X through (x, 0, 1.5). Flow runs -X (inlet) to +X (nozzle).
reset
# fan module: spinner, fan disc, 22 wide-chord swept fan blades
cone (0.45,0,1.5) 0.36 0.02 0.78 --axis=(-1,0,0) --name=Spinner
tint Spinner 0.86 0.87 0.90
cylinder (0.45,0,1.5) 0.44 0.3500 --axis=(1,0,0) --name=FanDisc
tint FanDisc 0.38 0.40 0.45
polyline (0.4814,0.0579,1.9200) (0.7669,-0.0349,1.9200) (0.7550,-0.0677,1.9200) (0.4767,0.0447,1.9200) --closed --name=S0
polyline (0.5017,0.1973,2.6800) (0.7508,-0.1895,2.6800) (0.7330,-0.2006,2.6800) (0.4946,0.1928,2.6800) --closed --name=S1
loft S0 S1 --name=FanBlade
delete S0 S1
tint FanBlade 0.62 0.66 0.72
radial FanBlade --count=22 --axis=(0,0,1.5),(1,0,0) --name=FanBlade
cone (0.80,0,1.5) 0.44 0.50 0.22 --axis=(1,0,0) --name=FanHubCone
tint FanHubCone 0.38 0.40 0.45
# low-pressure spool: shaft, 3 booster stages
cylinder (0.80,0,1.5) 0.11 3.55 --axis=(1,0,0) --name=LPShaft
tint LPShaft 0.62 0.66 0.72
cylinder (1.02,0,1.5) 0.30 0.52 --axis=(1,0,0) --name=BoosterDrum
tint BoosterDrum 0.38 0.40 0.45
cylinder (1.06,0,1.5) 0.5 0.0800 --axis=(1,0,0) --name=BoosterDisc1
tint BoosterDisc1 0.38 0.40 0.45
polyline (1.0711,0.0199,1.9900) (1.1338,-0.0114,1.9900) (1.1268,-0.0236,1.9900) (1.0683,0.0151,1.9900) --closed --name=S0
polyline (1.0788,0.0279,2.2200) (1.1257,-0.0241,2.2200) (1.1193,-0.0295,2.2200) (1.0762,0.0257,2.2200) --closed --name=S1
loft S0 S1 --name=BoosterBlade1
delete S0 S1
tint BoosterBlade1 0.62 0.66 0.72
radial BoosterBlade1 --count=28 --axis=(0,0,1.5),(1,0,0) --name=BoosterBlade1
cylinder (1.24,0,1.5) 0.5 0.0800 --axis=(1,0,0) --name=BoosterDisc2
tint BoosterDisc2 0.38 0.40 0.45
polyline (1.2511,0.0199,1.9900) (1.3138,-0.0114,1.9900) (1.3068,-0.0236,1.9900) (1.2483,0.0151,1.9900) --closed --name=S0
polyline (1.2588,0.0279,2.2000) (1.3057,-0.0241,2.2000) (1.2993,-0.0295,2.2000) (1.2562,0.0257,2.2000) --closed --name=S1
loft S0 S1 --name=BoosterBlade2
delete S0 S1
tint BoosterBlade2 0.62 0.66 0.72
radial BoosterBlade2 --count=28 --axis=(0,0,1.5),(1,0,0) --name=BoosterBlade2
cylinder (1.42,0,1.5) 0.5 0.0800 --axis=(1,0,0) --name=BoosterDisc3
tint BoosterDisc3 0.38 0.40 0.45
polyline (1.4311,0.0199,1.9900) (1.4938,-0.0114,1.9900) (1.4868,-0.0236,1.9900) (1.4283,0.0151,1.9900) --closed --name=S0
polyline (1.4388,0.0279,2.1800) (1.4857,-0.0241,2.1800) (1.4793,-0.0295,2.1800) (1.4362,0.0257,2.1800) --closed --name=S1
loft S0 S1 --name=BoosterBlade3
delete S0 S1
tint BoosterBlade3 0.62 0.66 0.72
radial BoosterBlade3 --count=28 --axis=(0,0,1.5),(1,0,0) --name=BoosterBlade3
# high-pressure compressor: 7 bladed stages on a tapering drum
cone (1.60,0,1.5) 0.40 0.32 1.00 --axis=(1,0,0) --name=HPCDrum
tint HPCDrum 0.38 0.40 0.45
cylinder (1.64,0,1.5) 0.42000000000000004 0.0600 --axis=(1,0,0) --name=HPCDisc1
tint HPCDisc1 0.55 0.58 0.66
polyline (1.6477,0.0163,1.9000) (1.6960,-0.0103,1.9000) (1.6907,-0.0188,1.9000) (1.6456,0.0129,1.9000) --closed --name=S0
polyline (1.6542,0.0194,2.1200) (1.6890,-0.0166,2.1200) (1.6845,-0.0206,2.1200) (1.6524,0.0178,2.1200) --closed --name=S1
loft S0 S1 --name=HPCBlade1
delete S0 S1
tint HPCBlade1 0.62 0.66 0.72
radial HPCBlade1 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade1
cylinder (1.775,0,1.5) 0.40800000000000003 0.0600 --axis=(1,0,0) --name=HPCDisc2
tint HPCDisc2 0.55 0.58 0.66
polyline (1.7827,0.0163,1.8880) (1.8310,-0.0103,1.8880) (1.8257,-0.0188,1.8880) (1.7806,0.0129,1.8880) --closed --name=S0
polyline (1.7892,0.0194,2.1020) (1.8240,-0.0166,2.1020) (1.8195,-0.0206,2.1020) (1.7874,0.0178,2.1020) --closed --name=S1
loft S0 S1 --name=HPCBlade2
delete S0 S1
tint HPCBlade2 0.62 0.66 0.72
radial HPCBlade2 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade2
cylinder (1.91,0,1.5) 0.396 0.0600 --axis=(1,0,0) --name=HPCDisc3
tint HPCDisc3 0.55 0.58 0.66
polyline (1.9177,0.0163,1.8760) (1.9660,-0.0103,1.8760) (1.9607,-0.0188,1.8760) (1.9156,0.0129,1.8760) --closed --name=S0
polyline (1.9242,0.0194,2.0840) (1.9590,-0.0166,2.0840) (1.9545,-0.0206,2.0840) (1.9224,0.0178,2.0840) --closed --name=S1
loft S0 S1 --name=HPCBlade3
delete S0 S1
tint HPCBlade3 0.62 0.66 0.72
radial HPCBlade3 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade3
cylinder (2.045,0,1.5) 0.384 0.0600 --axis=(1,0,0) --name=HPCDisc4
tint HPCDisc4 0.55 0.58 0.66
polyline (2.0527,0.0163,1.8640) (2.1010,-0.0103,1.8640) (2.0957,-0.0188,1.8640) (2.0506,0.0129,1.8640) --closed --name=S0
polyline (2.0592,0.0194,2.0660) (2.0940,-0.0166,2.0660) (2.0895,-0.0206,2.0660) (2.0574,0.0178,2.0660) --closed --name=S1
loft S0 S1 --name=HPCBlade4
delete S0 S1
tint HPCBlade4 0.62 0.66 0.72
radial HPCBlade4 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade4
cylinder (2.1799999999999997,0,1.5) 0.37200000000000005 0.0600 --axis=(1,0,0) --name=HPCDisc5
tint HPCDisc5 0.55 0.58 0.66
polyline (2.1877,0.0163,1.8520) (2.2360,-0.0103,1.8520) (2.2307,-0.0188,1.8520) (2.1856,0.0129,1.8520) --closed --name=S0
polyline (2.1942,0.0194,2.0480) (2.2290,-0.0166,2.0480) (2.2245,-0.0206,2.0480) (2.1924,0.0178,2.0480) --closed --name=S1
loft S0 S1 --name=HPCBlade5
delete S0 S1
tint HPCBlade5 0.62 0.66 0.72
radial HPCBlade5 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade5
cylinder (2.315,0,1.5) 0.36000000000000004 0.0600 --axis=(1,0,0) --name=HPCDisc6
tint HPCDisc6 0.55 0.58 0.66
polyline (2.3227,0.0163,1.8400) (2.3710,-0.0103,1.8400) (2.3657,-0.0188,1.8400) (2.3206,0.0129,1.8400) --closed --name=S0
polyline (2.3292,0.0194,2.0300) (2.3640,-0.0166,2.0300) (2.3595,-0.0206,2.0300) (2.3274,0.0178,2.0300) --closed --name=S1
loft S0 S1 --name=HPCBlade6
delete S0 S1
tint HPCBlade6 0.62 0.66 0.72
radial HPCBlade6 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade6
cylinder (2.45,0,1.5) 0.34800000000000003 0.0600 --axis=(1,0,0) --name=HPCDisc7
tint HPCDisc7 0.55 0.58 0.66
polyline (2.4577,0.0163,1.8280) (2.5060,-0.0103,1.8280) (2.5007,-0.0188,1.8280) (2.4556,0.0129,1.8280) --closed --name=S0
polyline (2.4642,0.0194,2.0120) (2.4990,-0.0166,2.0120) (2.4945,-0.0206,2.0120) (2.4624,0.0178,2.0120) --closed --name=S1
loft S0 S1 --name=HPCBlade7
delete S0 S1
tint HPCBlade7 0.62 0.66 0.72
radial HPCBlade7 --count=34 --axis=(0,0,1.5),(1,0,0) --name=HPCBlade7
# annular combustor with 14 fuel nozzle bosses
polyline (2.6200,0,1.8000) (2.6200,0,2.1600) (2.7200,0,2.2000) (3.0200,0,2.2000) (3.1000,0,2.1200) (3.1000,0,1.8400) (3.0000,0,1.7800) (2.7200,0,1.7800) --closed --name=CombustorProfile
revolve CombustorProfile 360 --origin=(0,0,1.5) --axis=(1,0,0) --name=Combustor
delete CombustorProfile
tint Combustor 0.80 0.50 0.32
cylinder (2.56,0,1.98) 0.045 0.10 --axis=(1,0,0) --name=FuelNozzle
tint FuelNozzle 0.62 0.66 0.72
radial FuelNozzle --count=14 --axis=(0,0,1.5),(1,0,0) --name=FuelNozzle
# high-pressure turbine (1 stage) and low-pressure turbine (3 stages, growing radius)
cylinder (3.12,0,1.5) 0.24 0.78 --axis=(1,0,0) --name=TurbineDrum
tint TurbineDrum 0.38 0.40 0.45
cylinder (3.16,0,1.5) 0.42 0.0800 --axis=(1,0,0) --name=HPTDisc
tint HPTDisc 0.55 0.58 0.66
polyline (3.1685,-0.0154,1.9100) (3.2273,0.0227,1.9100) (3.2333,0.0123,1.9100) (3.1709,-0.0196,1.9100) --closed --name=S0
polyline (3.1742,-0.0237,2.1000) (3.2222,0.0273,2.1000) (3.2273,0.0222,2.1000) (3.1763,-0.0258,2.1000) --closed --name=S1
loft S0 S1 --name=HPTBlade
delete S0 S1
tint HPTBlade 0.80 0.50 0.32
radial HPTBlade --count=46 --axis=(0,0,1.5),(1,0,0) --name=HPTBlade
cylinder (3.36,0,1.5) 0.38 0.0800 --axis=(1,0,0) --name=LPTDisc1
tint LPTDisc1 0.55 0.58 0.66
polyline (3.3636,-0.0167,1.8700) (3.4325,0.0241,1.8700) (3.4381,0.0135,1.8700) (3.3658,-0.0209,1.8700) --closed --name=S0
polyline (3.3656,-0.0290,2.1000) (3.4310,0.0328,2.1000) (3.4359,0.0274,2.1000) (3.3675,-0.0312,2.1000) --closed --name=S1
loft S0 S1 --name=LPTBlade1
delete S0 S1
tint LPTBlade1 0.62 0.66 0.72
radial LPTBlade1 --count=50 --axis=(0,0,1.5),(1,0,0) --name=LPTBlade1
cylinder (3.54,0,1.5) 0.38 0.0800 --axis=(1,0,0) --name=LPTDisc2
tint LPTDisc2 0.55 0.58 0.66
polyline (3.5436,-0.0167,1.8700) (3.6125,0.0241,1.8700) (3.6181,0.0135,1.8700) (3.5458,-0.0209,1.8700) --closed --name=S0
polyline (3.5456,-0.0290,2.1500) (3.6110,0.0328,2.1500) (3.6159,0.0274,2.1500) (3.5475,-0.0312,2.1500) --closed --name=S1
loft S0 S1 --name=LPTBlade2
delete S0 S1
tint LPTBlade2 0.62 0.66 0.72
radial LPTBlade2 --count=50 --axis=(0,0,1.5),(1,0,0) --name=LPTBlade2
cylinder (3.72,0,1.5) 0.38 0.0800 --axis=(1,0,0) --name=LPTDisc3
tint LPTDisc3 0.55 0.58 0.66
polyline (3.7236,-0.0167,1.8700) (3.7925,0.0241,1.8700) (3.7981,0.0135,1.8700) (3.7258,-0.0209,1.8700) --closed --name=S0
polyline (3.7256,-0.0290,2.2000) (3.7910,0.0328,2.2000) (3.7959,0.0274,2.2000) (3.7275,-0.0312,2.2000) --closed --name=S1
loft S0 S1 --name=LPTBlade3
delete S0 S1
tint LPTBlade3 0.62 0.66 0.72
radial LPTBlade3 --count=50 --axis=(0,0,1.5),(1,0,0) --name=LPTBlade3
# exhaust: plug cone and core nozzle
cone (3.84,0,1.5) 0.34 0.03 0.95 --axis=(1,0,0) --name=ExhaustPlug
tint ExhaustPlug 0.55 0.58 0.66
polyline (3.3000,0,2.2800) (3.9000,0,2.2600) (4.2500,0,2.1200) (4.2500,0,2.0800) (3.9000,0,2.2200) (3.3000,0,2.2400) --closed --name=CoreNozzleProfile
revolve CoreNozzleProfile 360 --origin=(0,0,1.5) --axis=(1,0,0) --name=CoreNozzle
delete CoreNozzleProfile
tint CoreNozzle 0.55 0.58 0.66
box (-2,-4.021,1.517) 9 4 4 --name=CutTool
boolean subtract CoreNozzle -- CutTool --name=CoreNozzle
# core cowl (cutaway), outlet guide vanes, nacelle with inlet lip (cutaway)
polyline (0.9800,0,2.2600) (1.6000,0,2.3400) (2.4000,0,2.3400) (3.0000,0,2.3000) (3.3200,0,2.3000) (3.3200,0,2.2600) (3.0000,0,2.2600) (2.4000,0,2.3000) (1.6000,0,2.3000) (0.9800,0,2.2200) --closed --name=CoreCowlProfile
revolve CoreCowlProfile 360 --origin=(0,0,1.5) --axis=(1,0,0) --name=CoreCowl
delete CoreCowlProfile
tint CoreCowl 0.72 0.74 0.78
box (-2,-4.021,1.517) 9 4 4 --name=CutTool
boolean subtract CoreCowl -- CutTool --name=CoreCowl
box (1.10,-0.006,2.34) 0.26 0.012 0.42 --name=OGV
tint OGV 0.62 0.66 0.72
radial OGV --count=36 --axis=(0,0,1.5),(1,0,0) --name=OGV
polyline (-0.6000,0,2.8000) (-0.7200,0,2.9200) (-0.6400,0,3.0600) (-0.3500,0,3.1600) (0.3000,0,3.2200) (1.5000,0,3.2200) (2.4000,0,3.1400) (3.0500,0,3.0000) (3.0500,0,2.9200) (2.4000,0,2.8600) (1.5000,0,2.8000) (0.3000,0,2.7800) (-0.2500,0,2.7800) (-0.5000,0,2.7800) --closed --name=NacelleProfile
revolve NacelleProfile 360 --origin=(0,0,1.5) --axis=(1,0,0) --name=Nacelle
delete NacelleProfile
tint Nacelle 0.86 0.87 0.90
box (-2,-4.021,1.517) 9 4 4 --name=CutTool
boolean subtract Nacelle -- CutTool --name=Nacelle
# pylon stub on top, accessory gearbox and two service pipes underneath
box (0.70,-0.22,3.1) 1.90 0.44 0.55 --name=Pylon
tint Pylon 0.72 0.74 0.78
box (1.50,-0.28,0.3799999999999999) 0.95 0.56 0.26 --name=Gearbox
tint Gearbox 0.38 0.40 0.45
cylinder (1.00,0.18,0.58) 0.03 1.90 --axis=(1,0,0) --name=PipeA
tint PipeA 0.62 0.66 0.72
cylinder (1.20,-0.20,0.54) 0.025 1.50 --axis=(1,0,0) --name=PipeB
tint PipeB 0.80 0.50 0.32
show shading plastic
view iso
view fit

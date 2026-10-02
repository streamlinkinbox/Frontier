# SolidArc native document v1
# Tyre tread "AllTerrain" — 46 pitches (pitch 53.27 mm), floor radius 390 mm, width 216 mm, depth 14.0 mm. Units: metres, wheel axis = Y.
# One half-pitch is designed on a warped grid (tools/tread_quads.py); blocks are traced outlines extruded radially;
# second half: mirror_stagger; then a radial array around Y. No height-maps / textures.
show shading plastic

polyline (0.38000,-0.10800) (0.38160,-0.09900) (0.38306,-0.09000) (0.38438,-0.08100) (0.38556,-0.07200) (0.38660,-0.06300) (0.38750,-0.05400) (0.38826,-0.04500) (0.38889,-0.03600) (0.38938,-0.02700) (0.38972,-0.01800) (0.38993,-0.00900) (0.39000,0.00000) (0.38993,0.00900) (0.38972,0.01800) (0.38938,0.02700) (0.38889,0.03600) (0.38826,0.04500) (0.38750,0.05400) (0.38660,0.06300) (0.38556,0.07200) (0.38438,0.08100) (0.38306,0.09000) (0.38160,0.09900) (0.38000,0.10800) (0.35500,0.10800) (0.35500,-0.10800) --closed --name=CrownProfile
revolve CrownProfile 360 --origin=(0,0,0) --axis=(0,1,0) --name=Crown
polyline (0.40400,0.00000) (0.40399,0.00250) (0.40398,0.00500) (0.38898,0.00500) (0.38900,0.00000) --closed --name=RibProfileR
revolve RibProfileR 360 --origin=(0,0,0) --axis=(0,1,0) --name=RibR
polyline (0.40400,0.00000) (0.40399,-0.00250) (0.40398,-0.00500) (0.38898,-0.00500) (0.38900,0.00000) --closed --name=RibProfileL
revolve RibProfileL 360 --origin=(0,0,0) --axis=(0,1,0) --name=RibL
tint Crown 0.16 0.16 0.17

# STEP block 1 (designed half): outline traced on the grid (8 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.026576,0.010000,0.387951) (0.026576,0.026000,0.387951) --name=s1_0
line (0.026576,0.026000,0.387951) (0.024577,0.026000,0.388023) --name=s1_1
line (0.024577,0.026000,0.388023) (0.024577,0.018000,0.388023) --name=s1_2
line (0.024577,0.018000,0.388023) (0.022578,0.018000,0.388095) --name=s1_3
line (0.022578,0.018000,0.388095) (0.022578,0.026000,0.388095) --name=s1_4
line (0.022578,0.026000,0.388095) (-0.017666,0.026000,0.389548) --name=s1_5
line (-0.017666,0.026000,0.389548) (-0.017666,0.010000,0.389548) --name=s1_6
line (-0.017666,0.010000,0.389548) (0.026576,0.010000,0.387951) --name=s1_7
join s1_0 s1_1 s1_2 s1_3 s1_4 s1_5 s1_6 s1_7 --name=Out1
extrude Out1 0.01500 --direction=(0.03606,0.00000,0.99935) --name=Blk1
tint Blk1 0.16 0.16 0.17

# STEP block 2 (designed half): outline traced on the grid (4 corners), height 3.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (-0.004339,0.026000,0.388205) (-0.002339,0.026000,0.388223) --name=s2_0
line (-0.002339,0.026000,0.388223) (-0.002339,0.034000,0.388223) --name=s2_1
line (-0.002339,0.034000,0.388223) (-0.004339,0.034000,0.388205) --name=s2_2
line (-0.004339,0.034000,0.388205) (-0.004339,0.026000,0.388205) --name=s2_3
join s2_0 s2_1 s2_2 s2_3 --name=Out2
extrude Out2 0.00400 --direction=(-0.00860,0.00000,0.99996) --name=Blk2
tint Blk2 0.16 0.16 0.17

# STEP block 3 (designed half): outline traced on the grid (6 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.023620,0.042000,0.386787) (0.023620,0.062000,0.386787) --name=s3_0
line (0.023620,0.062000,0.386787) (0.010942,0.062000,0.387044) --name=s3_1
line (0.010942,0.062000,0.387044) (0.010942,0.042000,0.387044) --name=s3_2
line (0.010942,0.042000,0.387044) (-0.017688,0.034000,0.387623) --name=s3_3
line (-0.017688,0.034000,0.387623) (-0.004409,0.034000,0.387355) --name=s3_4
line (-0.004409,0.034000,0.387355) (0.023620,0.042000,0.386787) --name=s3_5
join s3_0 s3_1 s3_2 s3_3 s3_4 s3_5 --name=Out3
extrude Out3 0.01500 --direction=(0.02024,0.00000,0.99980) --name=Blk3
tint Blk3 0.16 0.16 0.17

# STEP block 4 (designed half): outline traced on the grid (6 corners), height 12.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (-0.004453,0.034000,0.387661) (-0.002455,0.034000,0.387582) --name=s4_0
line (-0.002455,0.034000,0.387582) (0.025559,0.042000,0.386480) --name=s4_1
line (0.025559,0.042000,0.386480) (0.025559,0.062000,0.386480) --name=s4_2
line (0.025559,0.062000,0.386480) (0.023560,0.062000,0.386558) --name=s4_3
line (0.023560,0.062000,0.386558) (0.023560,0.042000,0.386558) --name=s4_4
line (0.023560,0.042000,0.386558) (-0.004453,0.034000,0.387661) --name=s4_5
join s4_0 s4_1 s4_2 s4_3 s4_4 s4_5 --name=Out4
extrude Out4 0.01300 --direction=(0.03931,0.00000,0.99923) --name=Blk4
tint Blk4 0.16 0.16 0.17

# STEP block 5 (designed half): outline traced on the grid (6 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.038159,0.062000,0.385616) (0.025499,0.062000,0.386356) --name=s5_0
line (0.025499,0.062000,0.386356) (0.025499,0.042000,0.386356) --name=s5_1
line (0.025499,0.042000,0.386356) (-0.002488,0.034000,0.387993) --name=s5_2
line (-0.002488,0.034000,0.387993) (0.010770,0.034000,0.387217) --name=s5_3
line (0.010770,0.034000,0.387217) (0.038159,0.042000,0.385616) --name=s5_4
line (0.038159,0.042000,0.385616) (0.038159,0.062000,0.385616) --name=s5_5
join s5_0 s5_1 s5_2 s5_3 s5_4 s5_5 --name=Out5
extrude Out5 0.01500 --direction=(0.05837,0.00000,0.99830) --name=Blk5
tint Blk5 0.16 0.16 0.17

# STEP block 6 (designed half): outline traced on the grid (6 corners), height 12.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.010737,0.034000,0.387517) (0.012731,0.034000,0.387363) --name=s6_0
line (0.012731,0.034000,0.387363) (0.040084,0.042000,0.385239) --name=s6_1
line (0.040084,0.042000,0.385239) (0.040084,0.062000,0.385239) --name=s6_2
line (0.040084,0.062000,0.385239) (0.038090,0.062000,0.385394) --name=s6_3
line (0.038090,0.062000,0.385394) (0.038090,0.042000,0.385394) --name=s6_4
line (0.038090,0.042000,0.385394) (0.010737,0.034000,0.387517) --name=s6_5
join s6_0 s6_1 s6_2 s6_3 s6_4 s6_5 --name=Out6
extrude Out6 0.01300 --direction=(0.07740,0.00000,0.99700) --name=Blk6
tint Blk6 0.16 0.16 0.17

# STEP block 7 (designed half): outline traced on the grid (10 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.012673,0.034000,0.387766) (0.026299,0.034000,0.386265) --name=s7_0
line (0.026299,0.034000,0.386265) (0.052774,0.042000,0.383349) --name=s7_1
line (0.052774,0.042000,0.383349) (0.052774,0.062000,0.383349) --name=s7_2
line (0.052774,0.062000,0.383349) (0.050786,0.062000,0.383568) --name=s7_3
line (0.050786,0.062000,0.383568) (0.050786,0.052000,0.383568) --name=s7_4
line (0.050786,0.052000,0.383568) (0.048798,0.052000,0.383787) --name=s7_5
line (0.048798,0.052000,0.383787) (0.048798,0.062000,0.383787) --name=s7_6
line (0.048798,0.062000,0.383787) (0.039943,0.062000,0.384762) --name=s7_7
line (0.039943,0.062000,0.384762) (0.039943,0.042000,0.384762) --name=s7_8
line (0.039943,0.042000,0.384762) (0.012673,0.034000,0.387766) --name=s7_9
join s7_0 s7_1 s7_2 s7_3 s7_4 s7_5 s7_6 s7_7 s7_8 s7_9 --name=Out7
extrude Out7 0.01500 --direction=(0.10950,0.00000,0.99399) --name=Blk7
tint Blk7 0.16 0.16 0.17

# STEP block 8 (designed half): outline traced on the grid (4 corners), height 3.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.023367,0.062000,0.384557) (0.025363,0.062000,0.384431) --name=s8_0
line (0.025363,0.062000,0.384431) (0.025363,0.070000,0.384431) --name=s8_1
line (0.025363,0.070000,0.384431) (0.023367,0.070000,0.384557) --name=s8_2
line (0.023367,0.070000,0.384557) (0.023367,0.062000,0.384557) --name=s8_3
join s8_0 s8_1 s8_2 s8_3 --name=Out8
extrude Out8 0.00400 --direction=(0.06324,0.00000,0.99800) --name=Blk8
tint Blk8 0.16 0.16 0.17

# STEP block 9 (designed half): outline traced on the grid (6 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.033830,0.108000,0.381533) (-0.006389,0.108000,0.383574) --name=s9_0
line (-0.006389,0.108000,0.383574) (-0.006389,0.078000,0.383574) --name=s9_1
line (-0.006389,0.078000,0.383574) (0.010636,0.070000,0.382710) --name=s9_2
line (0.010636,0.070000,0.382710) (0.050855,0.070000,0.380669) --name=s9_3
line (0.050855,0.070000,0.380669) (0.033830,0.078000,0.381533) --name=s9_4
line (0.033830,0.078000,0.381533) (0.033830,0.108000,0.381533) --name=s9_5
join s9_0 s9_1 s9_2 s9_3 s9_4 s9_5 --name=Out9
extrude Out9 0.01500 --direction=(0.05067,0.00000,0.99872) --name=Blk9
tint Blk9 0.16 0.16 0.17

# STEP block 10 (mirrored half, shifted half a pitch): outline traced on the grid (8 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.008959,-0.010000,0.389845) (0.008959,-0.026000,0.389845) --name=s10_0
line (0.008959,-0.026000,0.389845) (0.049010,-0.026000,0.385650) --name=s10_1
line (0.049010,-0.026000,0.385650) (0.049010,-0.018000,0.385650) --name=s10_2
line (0.049010,-0.018000,0.385650) (0.050999,-0.018000,0.385441) --name=s10_3
line (0.050999,-0.018000,0.385441) (0.050999,-0.026000,0.385441) --name=s10_4
line (0.050999,-0.026000,0.385441) (0.052988,-0.026000,0.385233) --name=s10_5
line (0.052988,-0.026000,0.385233) (0.052988,-0.010000,0.385233) --name=s10_6
line (0.052988,-0.010000,0.385233) (0.008959,-0.010000,0.389845) --name=s10_7
join s10_0 s10_1 s10_2 s10_3 s10_4 s10_5 s10_6 s10_7 --name=Out10
extrude Out10 0.01500 --direction=(0.10418,0.00000,0.99456) --name=Blk10
tint Blk10 0.16 0.16 0.17

# STEP block 11 (mirrored half, shifted half a pitch): outline traced on the grid (4 corners), height 3.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.022163,-0.034000,0.387597) (0.024160,-0.034000,0.387477) --name=s11_0
line (0.024160,-0.034000,0.387477) (0.024160,-0.026000,0.387477) --name=s11_1
line (0.024160,-0.026000,0.387477) (0.022163,-0.026000,0.387597) --name=s11_2
line (0.022163,-0.026000,0.387597) (0.022163,-0.034000,0.387597) --name=s11_3
join s11_0 s11_1 s11_2 s11_3 --name=Out11
extrude Out11 0.00400 --direction=(0.05966,0.00000,0.99822) --name=Blk11
tint Blk11 0.16 0.16 0.17

# STEP block 12 (mirrored half, shifted half a pitch): outline traced on the grid (6 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.022035,-0.034000,0.386752) (0.008806,-0.034000,0.387927) --name=s12_0
line (0.008806,-0.034000,0.387927) (0.037329,-0.042000,0.385395) --name=s12_1
line (0.037329,-0.042000,0.385395) (0.037329,-0.062000,0.385395) --name=s12_2
line (0.037329,-0.062000,0.385395) (0.049961,-0.062000,0.384273) --name=s12_3
line (0.049961,-0.062000,0.384273) (0.049961,-0.042000,0.384273) --name=s12_4
line (0.049961,-0.042000,0.384273) (0.022035,-0.034000,0.386752) --name=s12_5
join s12_0 s12_1 s12_2 s12_3 s12_4 s12_5 --name=Out12
extrude Out12 0.01500 --direction=(0.08842,0.00000,0.99608) --name=Blk12
tint Blk12 0.16 0.16 0.17

# STEP block 13 (mirrored half, shifted half a pitch): outline traced on the grid (6 corners), height 12.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.049885,-0.042000,0.384050) (0.049885,-0.062000,0.384050) --name=s13_0
line (0.049885,-0.062000,0.384050) (0.051873,-0.062000,0.383835) --name=s13_1
line (0.051873,-0.062000,0.383835) (0.051873,-0.042000,0.383835) --name=s13_2
line (0.051873,-0.042000,0.383835) (0.024000,-0.034000,0.386846) --name=s13_3
line (0.024000,-0.034000,0.386846) (0.022012,-0.034000,0.387061) --name=s13_4
line (0.022012,-0.034000,0.387061) (0.049885,-0.042000,0.384050) --name=s13_5
join s13_0 s13_1 s13_2 s13_3 s13_4 s13_5 --name=Out13
extrude Out13 0.01300 --direction=(0.10741,0.00000,0.99421) --name=Blk13
tint Blk13 0.16 0.16 0.17

# STEP block 14 (mirrored half, shifted half a pitch): outline traced on the grid (6 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.064385,-0.042000,0.382113) (0.037170,-0.034000,0.385580) --name=s14_0
line (0.037170,-0.034000,0.385580) (0.023995,-0.034000,0.387258) --name=s14_1
line (0.023995,-0.034000,0.387258) (0.051805,-0.042000,0.383715) --name=s14_2
line (0.051805,-0.042000,0.383715) (0.051805,-0.062000,0.383715) --name=s14_3
line (0.051805,-0.062000,0.383715) (0.064385,-0.062000,0.382113) --name=s14_4
line (0.064385,-0.062000,0.382113) (0.064385,-0.042000,0.382113) --name=s14_5
join s14_0 s14_1 s14_2 s14_3 s14_4 s14_5 --name=Out14
extrude Out14 0.01500 --direction=(0.12636,0.00000,0.99198) --name=Blk14
tint Blk14 0.16 0.16 0.17

# STEP block 15 (mirrored half, shifted half a pitch): outline traced on the grid (6 corners), height 12.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.064301,-0.042000,0.381896) (0.064301,-0.062000,0.381896) --name=s15_0
line (0.064301,-0.062000,0.381896) (0.066280,-0.062000,0.381606) --name=s15_1
line (0.066280,-0.062000,0.381606) (0.066280,-0.042000,0.381606) --name=s15_2
line (0.066280,-0.042000,0.381606) (0.039136,-0.034000,0.385591) --name=s15_3
line (0.039136,-0.034000,0.385591) (0.037157,-0.034000,0.385881) --name=s15_4
line (0.037157,-0.034000,0.385881) (0.064301,-0.042000,0.381896) --name=s15_5
join s15_0 s15_1 s15_2 s15_3 s15_4 s15_5 --name=Out15
extrude Out15 0.01300 --direction=(0.14526,0.00000,0.98939) --name=Blk15
tint Blk15 0.16 0.16 0.17

# STEP block 16 (mirrored half, shifted half a pitch): outline traced on the grid (10 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.066107,-0.042000,0.381139) (0.066107,-0.062000,0.381139) --name=s16_0
line (0.066107,-0.062000,0.381139) (0.074875,-0.062000,0.379562) --name=s16_1
line (0.074875,-0.062000,0.379562) (0.074875,-0.052000,0.379562) --name=s16_2
line (0.074875,-0.052000,0.379562) (0.076843,-0.052000,0.379208) --name=s16_3
line (0.076843,-0.052000,0.379208) (0.076843,-0.062000,0.379208) --name=s16_4
line (0.076843,-0.062000,0.379208) (0.078811,-0.062000,0.378854) --name=s16_5
line (0.078811,-0.062000,0.378854) (0.078811,-0.042000,0.378854) --name=s16_6
line (0.078811,-0.042000,0.378854) (0.052597,-0.034000,0.383570) --name=s16_7
line (0.052597,-0.034000,0.383570) (0.039105,-0.034000,0.385997) --name=s16_8
line (0.039105,-0.034000,0.385997) (0.066107,-0.042000,0.381139) --name=s16_9
join s16_0 s16_1 s16_2 s16_3 s16_4 s16_5 s16_6 s16_7 s16_8 s16_9 --name=Out16
extrude Out16 0.01500 --direction=(0.17708,0.00000,0.98420) --name=Blk16
tint Blk16 0.16 0.16 0.17

# STEP block 17 (mirrored half, shifted half a pitch): outline traced on the grid (4 corners), height 3.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.049556,-0.070000,0.382066) (0.051539,-0.070000,0.381804) --name=s17_0
line (0.051539,-0.070000,0.381804) (0.051539,-0.062000,0.381804) --name=s17_1
line (0.051539,-0.062000,0.381804) (0.049556,-0.062000,0.382066) --name=s17_2
line (0.049556,-0.062000,0.382066) (0.049556,-0.070000,0.382066) --name=s17_3
join s17_0 s17_1 s17_2 s17_3 --name=Out17
extrude Out17 0.00400 --direction=(0.13120,0.00000,0.99136) --name=Blk17
tint Blk17 0.16 0.16 0.17

# STEP block 18 (mirrored half, shifted half a pitch): outline traced on the grid (6 corners), height 14.0 mm, drawn on the tangent plane 1 mm under the floor, extruded radially
line (0.059788,-0.078000,0.378335) (0.076714,-0.070000,0.376311) --name=s18_0
line (0.076714,-0.070000,0.376311) (0.036728,-0.070000,0.381092) --name=s18_1
line (0.036728,-0.070000,0.381092) (0.019802,-0.078000,0.383115) --name=s18_2
line (0.019802,-0.078000,0.383115) (0.019802,-0.108000,0.383115) --name=s18_3
line (0.019802,-0.108000,0.383115) (0.059788,-0.108000,0.378335) --name=s18_4
line (0.059788,-0.108000,0.378335) (0.059788,-0.078000,0.378335) --name=s18_5
join s18_0 s18_1 s18_2 s18_3 s18_4 s18_5 --name=Out18
extrude Out18 0.01500 --direction=(0.11871,0.00000,0.99293) --name=Blk18
tint Blk18 0.16 0.16 0.17

# STEP array: one full pitch (both halves) x 46 around the Y axis
radial Blk1 Blk2 Blk3 Blk4 Blk5 Blk6 Blk7 Blk8 Blk9 Blk10 Blk11 Blk12 Blk13 Blk14 Blk15 Blk16 Blk17 Blk18 --count=46 --axis=(0,0,0),(0,1,0) --name=Pitch

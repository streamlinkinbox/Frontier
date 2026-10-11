# SolidArc · Phase 3 · modal sketch tools driven by pointer, keys and numeric entry, with snapping
# Top orthographic view of an empty scene frames ±5 m: distance 16.82, half-height 6.456 m → 1 m = 61.96 px,
# +X right, +Y up on screen (pixel Y grows downward), origin at pixel (640,400).
view top ; view fit
snap status

echo -- line with typed length and relative entry, Blender style
tool line ; point (0,0) ; type 4                      # L=4 along +X (default direction)
tool line ; point (4,0) ; type @0,3                   # relative Δ(0,3)
tool line ; point (4,3) ; type a180,4                 # polar: 180°, 4 m → (0,3)
tool polyline ; point (0,3) ; point (0,0) ; key enter  # closing edge by hand (polyline ends on Enter)

echo -- axis lock: X pressed, cursor off-axis, result stays on the anchor's X line
tool line ; point (6,1) ; key x ; point (9.7,2.9) ; hud

echo -- circle by centre + typed radius, polygon with n<k>, arc by centre/start/angle
tool circle ; point (-5,0) ; type r2
tool polygon ; type n5 ; point (-5,5) ; point (-3,5)
tool arc ; point (5,-4) ; point (7,-4) ; type a90

echo -- rectangle, centre rectangle, slot, ellipse, spline with degree, control curve
tool rect ; point (-9,-6) ; point (-6,-3)
tool centerrect ; point (-1,-6) ; point (0.5,-5)
tool slot ; point (2,-7) ; point (6,-7) ; type r0.6
tool ellipse ; point (8,3) ; point (10,3) ; type r1
tool spline ; type d3 ; point (-9,8) ; point (-7,9.5) ; point (-5,7.5) ; point (-3,9) ; key enter
tool cpcurve ; point (2,7) ; point (4,9.5) ; point (6,6.5) ; point (8,9) ; key enter

echo -- circle2 / circle3 / arc3
tool circle2 ; point (-8,-9) ; point (-6,-9)
tool circle3 ; point (0,-9.5) ; point (1,-8.5) ; point (2,-9.5)
tool arc3 ; point (4,-9.5) ; point (8,-9.5) ; point (6,-8.5)

echo -- snapping through the pointer (top view: 1 m = 61.96 px, origin at 640,400)
inspect 888 400          # (4.00, 0.00) → endpoint of Line #1 / Line.2
inspect 884 404          # 5.6 px away → still that endpoint
inspect 764 400          # (2.00, 0.00) → midpoint of Line #1
inspect 330 400          # (-5.00, 0.00) → centre of Circle #6
inspect 209 402          # (-6.96, -0.03) → quadrant of Circle #6 at (-7,0)
inspect 640 214          # (0, 3.0) → endpoint where Line.3 meets Polyline
inspect 700 300          # (0.97, 1.61) → nothing near: lattice (1,2)

echo -- pointer-driven tool with endpoint snap: start exactly at (4,3) even though the pixel is 5 px off
tool line ; pointer 892 218 ; hud ; click ; pointer 600 250 ; click

echo -- hotkeys: Shift+A starts line, Esc cancels, A selects all, Alt+A none, Numpad views
key shift+a ; key esc
key a ; key alt+a
key numpad1 ; key numpad7

list
view fit ; render Proof_03a_Sketch_Top
view iso ; view fit ; render Proof_03b_Sketch_Iso

echo -- move / rotate / scale through G / R / S with axis locks and typed values
select Line Line.2 Line.3 Polyline
key g ; key x ; type 12
key r ; type 30
key s ; type 0.5
view top ; view fit ; render Proof_03c_Transformed

echo -- live preview: leave a circle tool running with the pointer moved, so the rubber band and snap glyph render
view top ; view fit ; tool circle ; point (0,0) ; pointer 700 300 ; hud ; render Proof_03d_LivePreview ; key esc

#============================================================================================================================
# 🧩 VehiclePhysicsImages.py - renders the exhibit's CSV evidence to PNG with Pillow (no matplotlib in the toolchain).
# 📝 Inputs (written by VehiclePhysicsExhibit): pacejka.csv, lattice.csv, lattice_particles.csv, fem_outline.csv,
#    drive.csv, jolt_trace.csv. Run from the exhibit directory: python Code/VehiclePhysicsImages.py
#============================================================================================================================

import math
import os

from PIL import Image, ImageDraw

W, H = 1180, 760
BG = (16, 18, 24)
GRID = (48, 54, 68)
AXIS = (120, 130, 150)
TEXT = (205, 212, 226)
COLOURS = [(255, 120, 90), (110, 200, 255), (150, 240, 160), (255, 215, 120), (220, 150, 255)]


def canvas(title):
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    draw.text((24, 14), title, fill=TEXT)
    return img, draw


def plot_frame(draw, box, x_range, y_range, x_label, y_label):
    x0, y0, x1, y1 = box
    draw.rectangle(box, outline=AXIS)
    for i in range(1, 5):
        gx = x0 + (x1 - x0) * i / 5
        gy = y0 + (y1 - y0) * i / 5
        draw.line([(gx, y0), (gx, y1)], fill=GRID)
        draw.line([(x0, gy), (x1, gy)], fill=GRID)
        draw.text((gx - 14, y1 + 6), f"{x_range[0] + (x_range[1]-x_range[0]) * i / 5:.2g}", fill=AXIS)
        draw.text((x0 - 58, gy - 6), f"{y_range[1] - (y_range[1]-y_range[0]) * i / 5:.3g}", fill=AXIS)
    draw.text(((x0 + x1) / 2 - 40, y1 + 26), x_label, fill=TEXT)
    draw.text((x0 - 58, y0 - 22), y_label, fill=TEXT)

    def map_point(x, y):
        px = x0 + (x - x_range[0]) / (x_range[1] - x_range[0]) * (x1 - x0)
        py = y1 - (y - y_range[0]) / (y_range[1] - y_range[0]) * (y1 - y0)
        return px, py

    return map_point


def polyline(draw, mapper, points, colour, width=2):
    mapped = [mapper(x, y) for x, y in points]
    if len(mapped) > 1:
        draw.line(mapped, fill=colour, width=width)


def legend(draw, x, y, entries):
    for i, (colour, label) in enumerate(entries):
        draw.line([(x, y + i * 18 + 6), (x + 28, y + i * 18 + 6)], fill=colour, width=3)
        draw.text((x + 36, y + i * 18), label, fill=TEXT)


def rows(path):
    out = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append([float(v) for v in line.replace(",", " ").split()])
    return out


# ---------------------------------------------------------------- Pacejka curves
def pacejka_image():
    data = rows("pacejka.csv")
    img, draw = canvas("Pacejka MF6.1 (ported TyreSlipSolver) - Fx vs slip ratio, Fy vs slip angle")
    fx_map = plot_frame(draw, (90, 70, 560, 620), (-0.95, 1.55), (-16000, 16000), "slip ratio kappa [-]", "Fx [N]")
    fy_map = plot_frame(draw, (660, 70, 1130, 620), (-1.35, 1.35), (-22000, 22000), "slip angle alpha [rad]", "Fy [N]")
    for i, load in enumerate((2000, 4000, 8000)):
        polyline(draw, fx_map, [(r[0], r[1 + i]) for r in data], COLOURS[i])
        polyline(draw, fy_map, [(r[4], r[5 + i]) for r in data], COLOURS[i])
    legend(draw, 90, 660, [(COLOURS[i], f"Fz = {load} N") for i, load in enumerate((2000, 4000, 8000))])
    draw.text((660, 668), "Bit-exact vs the demo's TyreSlipSolver.js (checks A1-A3, max rel err 1.8e-15)", fill=AXIS)
    img.save("PacejkaCurves.png")


# ---------------------------------------------------------------- Carcass curves: lattice vs spring
def carcass_image():
    data = rows("lattice.csv")
    img, draw = canvas("Carcass load/deflection - XPBD lattice (markers) vs demo carcass spring K = 25 kN/m + 0.9*p (lines)")
    mapper = plot_frame(draw, (100, 70, 820, 640), (0, 42), (0, 14000), "radial deflection [mm]", "normal force [N]")
    pressures = sorted({r[0] for r in data})
    for i, pressure in enumerate(pressures):
        subset = [r for r in data if r[0] == pressure]
        polyline(draw, mapper, [(r[1] * 1000, r[3]) for r in subset], COLOURS[i], 1)
        for r in subset:
            px, py = mapper(r[1] * 1000, r[2])
            draw.ellipse([px - 4, py - 4, px + 4, py + 4], outline=COLOURS[i], width=2)
    legend(draw, 850, 90, [(COLOURS[i], f"{int(p/1000)} kPa gauge") for i, p in enumerate(pressures)])
    draw.text((850, 220), "220 kPa: lattice within 0.75-1.30x\nof spring, 10-40 mm (check C1).\n0 kPa deep-deflection runs ~2x\nstiff: belt geometric floor,\ndisclosed in README.", fill=AXIS)
    img.save("CarcassCurves.png")


# ---------------------------------------------------------------- FEM outline + lattice particles
def deformation_image():
    outline = rows("fem_outline.csv")
    particles = rows("lattice_particles.csv")
    img, draw = canvas("Tyre shape under load - left: PB-FEM deformed outline (ported WGSL); right: XPBD lattice particles")
    left = plot_frame(draw, (90, 80, 540, 620), (-0.62, 0.62), (-0.62, 0.62), "x [m] (wheel local)", "z [m]")
    labels = ["unloaded", "14 mm @ 220 kPa", "60 mm flat @ 30 kPa + shear"]
    for record in (0, 1, 2):
        pts = [(r[1], r[3]) for r in outline if int(r[0]) == record]
        polyline(draw, left, pts, COLOURS[record])
    # ground line for the 14 mm record: hub at R - 0.014
    gy = left(0, -(0.50855 - 0.014))[1]
    draw.line([(90, gy), (540, gy)], fill=AXIS)
    legend(draw, 90, 650, [(COLOURS[i], labels[i]) for i in range(3)])
    right = plot_frame(draw, (650, 80, 1100, 620), (-0.62, 0.62), (-0.62, 0.62), "x [m]", "z [m]")
    # rim circle
    rim = [(0.3446 * math.cos(a / 40 * 2 * math.pi), 0.3446 * math.sin(a / 40 * 2 * math.pi)) for a in range(41)]
    polyline(draw, right, rim, AXIS, 1)
    gy2 = right(0, -(0.50855 - 0.030))[1]
    draw.line([(650, gy2), (1100, gy2)], fill=AXIS)
    for p in particles:
        px, py = right(p[0], p[2])
        shade = COLOURS[1] if abs(p[1]) < 0.01 else COLOURS[3]
        draw.ellipse([px - 3, py - 3, px + 3, py + 3], fill=shade)
    draw.text((650, 650), "Lattice at 30 mm deflection, 220 kPa: flattened patch,\nshoulder rows (yellow) bulge, crown row (blue) carries centre.", fill=AXIS)
    img.save("TyreDeformation.png")


# ---------------------------------------------------------------- Drive trace
def drive_image():
    data = rows("drive.csv")
    img, draw = canvas("Scripted drive on the ported spring model - path (top) and speed / rear slip / tread temperature (bottom)")
    xs = [r[1] for r in data]
    ys = [r[2] for r in data]
    top = plot_frame(draw, (90, 60, 1120, 300), (min(xs) - 5, max(xs) + 5), (min(ys) - 4, max(ys) + 4), "x [m]", "y [m]")
    polyline(draw, top, [(r[1], r[2]) for r in data], COLOURS[1])
    t_max = data[-1][0]
    bottom = plot_frame(draw, (90, 380, 1120, 640), (0, t_max), (0, 50), "time [s]", "speed [m/s] / slip x20 / (T-293K)/3")
    polyline(draw, bottom, [(r[0], r[3]) for r in data], COLOURS[1])
    polyline(draw, bottom, [(r[0], abs(r[14]) * 20) for r in data], COLOURS[0])
    polyline(draw, bottom, [(r[0], (r[15] - 293.15) / 3) for r in data], COLOURS[3])
    legend(draw, 90, 670, [(COLOURS[1], "speed [m/s]"), (COLOURS[0], "|rear slip ratio| x20"),
                           (COLOURS[3], "rear tread temp rise /3 [K] (thermal model)")])
    draw.text((560, 676), "launch -> slalom -> brake -> line-lock burnout (slip pegged, temp climbs) -> release", fill=AXIS)
    img.save("DriveTrace.png")


# ---------------------------------------------------------------- Jolt scene top-down
def jolt_image():
    path, cones = [], []
    with open("jolt_trace.csv") as handle:
        for line in handle:
            parts = line.replace(",", " ").split()
            if not parts:
                continue
            if parts[0] == "#" and len(parts) > 8 and parts[1] == "cone":
                cones.append([float(v) for v in parts[2:9]])
            elif not line.startswith("#"):
                path.append([float(v) for v in parts])
    img, draw = canvas("Jolt loose-body scene - car path through the cone row (top-down); circles: start, crosses: final")
    mapper = plot_frame(draw, (80, 90, 1130, 600), (-4, 64), (-10, 10), "x [m]", "y [m]")
    polyline(draw, mapper, [(p[1], p[2]) for p in path], COLOURS[1], 3)
    for cone in cones:
        sx, sy = mapper(cone[0], cone[1])
        fx, fy = mapper(cone[2], cone[3])
        draw.ellipse([sx - 5, sy - 5, sx + 5, sy + 5], outline=AXIS, width=2)
        draw.line([(fx - 5, fy - 5), (fx + 5, fy + 5)], fill=COLOURS[0], width=2)
        draw.line([(fx - 5, fy + 5), (fx + 5, fy - 5)], fill=COLOURS[0], width=2)
        draw.line([(sx, sy), (fx, fy)], fill=GRID)
    wx = mapper(59.5, 0)[0]
    draw.line([(wx, 90), (wx, 600)], fill=COLOURS[2], width=4)
    draw.text((wx - 60, 610), "wall face x=59.5", fill=COLOURS[2])
    draw.text((80, 640), "All 10 cones displaced/tipped; the car slows and yaws from the proxy velocity exchange (check F1).", fill=AXIS)
    img.save("JoltScene.png")


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)) + "/..")
    pacejka_image()
    carcass_image()
    deformation_image()
    drive_image()
    jolt_image()
    print("images written: PacejkaCurves.png CarcassCurves.png TyreDeformation.png DriveTrace.png JoltScene.png")

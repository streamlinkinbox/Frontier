"""Arrange actual C++ before/after captures without altering their pixels; requires Pillow."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[3]
frames = root / "_ClaudeScratch/vehiclephysics"
out = root / "Exhibits/VehiclePhysics/Visuals"
out.mkdir(parents=True, exist_ok=True)
canvas = Image.new("RGB", (1280, 1504), "#111820")
draw = ImageDraw.Draw(canvas)
for row, variant in enumerate(("before", "after")):
    y = row * 752
    draw.text((20, y + 10), f"{variant.upper()} | same geometry, camera, sunlight | Lambert + Blinn, planar shadows", fill="white")
    image = Image.open(frames / variant / "view-0.ppm")
    canvas.paste(image, (0, y + 32))
    for pose in range(3):
        Image.open(frames / variant / f"view-{pose}.ppm").save(out / f"Raster-{variant}-{pose}.png")
canvas.save(out / "RasterComparison.png")
print(out / "RasterComparison.png")

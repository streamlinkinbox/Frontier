# 🧩 Publish unretouched Vulkan vehicle readbacks and paired lighting controls.
from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Build = Root / "_ClaudeScratch/build/vehicle-lighting"
Images = Root / "Exhibits/LightCompute/Images"
Results = Root / "Exhibits/LightCompute/Results"
Log = (Results / "VehicleLighting-render.txt").read_text()
assert "VEHICLE_LIGHTING_COMPLETE views=3 pass=1" in Log
assert "VEHICLE_LIGHTING_CONTROLS repeat_exact=1" in Log
Titles = [
    "Sun / clouds / flare",
    "Cool IES + warm spotlight / emissive gantry",
    "Wide proving ground / shifted cloud deck",
]
Hashes = {}
Pictures = []
for Index, Title in enumerate(Titles):
    Source = Build / "captures" / f"VehicleLighting{Index}.ppm"
    Picture = Image.open(Source).convert("RGB")
    assert Picture.size == (384, 240)
    Output = Images / f"VehicleLighting{Index}.png"
    Picture.save(Output)
    assert Image.open(Output).convert("RGB").tobytes() == Picture.tobytes()
    Pixels = np.fromfile(Source.with_suffix(".f32"), dtype="<f4").reshape(240, 384, 4)
    assert np.isfinite(Pixels).all()
    assert np.count_nonzero(np.max(Pixels[:, :, :3], axis=-1) > 0.001) > 10000
    Hashes[Source.name] = hashlib.sha256(Source.read_bytes()).hexdigest()
    Hashes[Source.with_suffix(".f32").name] = hashlib.sha256(Source.with_suffix(".f32").read_bytes()).hexdigest()
    Pictures.append(Picture)
Gallery = Image.new("RGB", (1280, 858), "#111720")
Pen = ImageDraw.Draw(Gallery)
Font = ImageFont.load_default(size=17)
for Index, Picture in enumerate(Pictures):
    Column, Row = (0, 0) if Index == 0 else ((1, 0) if Index == 1 else (0, 1))
    Gallery.paste(Picture.resize((640, 400), Image.Resampling.NEAREST), (Column * 640, Row * 429 + 29))
    Pen.text((Column * 640 + 12, Row * 429 + 5), Titles[Index], font=Font, fill="white")
Pen.text((660, 460), "Actual ControlVehicle / existing Vulkan exhibit", font=Font, fill="white")
Pen.text((660, 495), "ReSTIR analytic direct + temporal/spatial reuse", font=Font, fill="white")
Pen.text((660, 530), "Separate emissive sampling / reflections / closed glass", font=Font, fill="white")
Pen.text((660, 565), "Clouds / GPU solar disc and lens flare", font=Font, fill="white")
Pen.text((660, 620), "384 x 240 each / two history steps / 2 transport samples", font=Font, fill="white")
Pen.text((660, 655), "Synthetic IES shape; not calibrated luminaire data", font=Font, fill="white")
Pen.text((660, 710), "SwiftShader CPU Vulkan / not real-time or editor proof", font=Font, fill="white")
Pen.text((660, 745), "No AI imagery / no production wiring / no retouching", font=Font, fill="white")
Gallery.save(Images / "VehicleLightingGallery.png")
Controls = Image.new("RGB", (1280, 858), "#111720")
Pen = ImageDraw.Draw(Controls)
for Index, (Name, Title) in enumerate(
    [
        ("Cold", "Fixed-seed cold ReSTIR / full lighting"),
        ("FlareOff", "Same seed/reset / lens flare OFF"),
        ("IesOff", "Same seed/reset / IES source OFF"),
    ]
):
    Picture = Image.open(Build / "captures" / f"VehicleLighting{Name}.ppm").convert("RGB")
    Column, Row = Index % 2, Index // 2
    Controls.paste(Picture.resize((640, 400), Image.Resampling.NEAREST), (Column * 640, Row * 429 + 29))
    Pen.text((Column * 640 + 12, Row * 429 + 5), Title, font=Font, fill="white")
    Picture.save(Images / f"VehicleLighting{Name}.png")
    Hashes[f"VehicleLighting{Name}.ppm"] = hashlib.sha256(
        (Build / "captures" / f"VehicleLighting{Name}.ppm").read_bytes()
    ).hexdigest()
Pen.text((660, 490), "Control images are cold, not the two-step hero.", font=Font, fill="white")
Pen.text((660, 530), "Exact same-seed repeat verified before controls.", font=Font, fill="white")
Pen.text((660, 570), "IES off disables its source, not only its angular profile.", font=Font, fill="white")
Pen.text((660, 610), "No isolated emissive/spot/cloud quality acceptance.", font=Font, fill="white")
Controls.save(Images / "VehicleLightingControls.png")
(Results / "VehicleLighting-images.json").write_text(
    json.dumps(
        {
            "raw_sha256": Hashes,
            "display": "shared EncodeDisplayRgb8 exposure .45; no further grading, denoising or AI upscaling",
            "gallery_dimensions": [1280, 858],
        },
        indent=2,
    )
    + "\n"
)
print("VEHICLE_LIGHTING_IMAGES views=3 png_byte_exact=3 finite_hdr=3 controls=3 pass=1")

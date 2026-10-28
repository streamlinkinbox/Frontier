"""Convert night-sky PPM captures to labelled PNG images and tile the dusk sweep.

Requires Pillow (PyPI). Usage: NightSkyImages.py <capture_dir> <image_dir>
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw


DUSK_LABELS = ["Sun +14 deg", "Sun +2 deg", "Sun -2 deg", "Sun -6 deg", "Sun -12 deg", "Sun -18 deg"]

SINGLE = {
    "NightField": "NightSkyCatalogueMilkyWay",
    "MoonField": "NightSkyMoonField",
    "MoonClose": "MoonClose",
    "SunFlare": "SunDiskLensFlare",
    "SunDiscClose": "SunDiscClose",
    "SunDiscFiltered": "SunDiscFiltered",
}


def label(image: Image.Image, text: str) -> Image.Image:
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 8 * len(text) + 12, 18), fill=(0, 0, 0))
    draw.text((6, 4), text, fill=(235, 235, 235))
    return image


def main():
    capture = Path(sys.argv[1] if len(sys.argv) > 1 else "Exhibits/NightSky/Capture")
    images = Path(sys.argv[2] if len(sys.argv) > 2 else "Exhibits/NightSky/Images")
    images.mkdir(parents=True, exist_ok=True)

    for source, target in SINGLE.items():
        Image.open(capture / f"{source}.ppm").save(images / f"{target}.png")
        print(f"wrote {target}.png")

    phases = Image.open(capture / "MoonPhases.ppm")
    tile = phases.width // 4
    names = ["New", "First quarter", "Full", "Last quarter"]
    for index, name in enumerate(names):
        label(phases.crop((index * tile, 0, (index + 1) * tile, phases.height)), name)
    phases.save(images / "MoonPhases.png")
    print("wrote MoonPhases.png")

    frames = [Image.open(capture / f"Dusk{index}.ppm") for index in range(len(DUSK_LABELS))]
    width, height = frames[0].size
    sheet = Image.new("RGB", (width * 3, height * 2), (0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(label(frame, DUSK_LABELS[index]), ((index % 3) * width, (index // 3) * height))
    sheet.save(images / "DuskSequence.png")
    print("wrote DuskSequence.png")


if __name__ == "__main__":
    main()

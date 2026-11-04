"""Verify all 6 real 2K celestial moon maps and emit the unpacked Luna PPM archive when requested.

Sources in EngineContent/EnvironmentArchives/CelestialMaps/:
  - MoonAlbedoSource2048.jpg (Luna — NASA SVS CGI Moon)
  - Ember2048.jpg            (Ember — Solar System Scope CC-BY 4.0)
  - Glacier2048.jpg          (Glacier — Solar System Scope CC-BY 4.0)
  - Sulfur2048.jpg           (Sulfur — USGS Astrogeology Galileo/Voyager Io mosaic)
  - Shroud2048.jpg           (Shroud — Solar System Scope CC-BY 4.0)
  - Shard2048.jpg            (Shard — Solar System Scope CC-BY 4.0)

Usage: PrepareMoonAlbedo.py [source_dir]
"""

import sys
from pathlib import Path

from PIL import Image

MOON_MAPS = [
    ("Luna", "MoonAlbedoSource2048.jpg"),
    ("Ember", "Ember2048.jpg"),
    ("Glacier", "Glacier2048.jpg"),
    ("Sulfur", "Sulfur2048.jpg"),
    ("Shroud", "Shroud2048.jpg"),
    ("Shard", "Shard2048.jpg"),
]


def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "EngineContent/EnvironmentArchives/CelestialMaps")
    if (folder / "EngineContent/EnvironmentArchives/CelestialMaps").is_dir():
        folder = folder / "EngineContent/EnvironmentArchives/CelestialMaps"
    for body_name, filename in MOON_MAPS:
        path = folder / filename
        image = Image.open(path).convert("RGB")
        if image.size != (2048, 1024):
            raise RuntimeError(f"{path} expected 2048x1024, got {image.size}")
        grey = image.convert("L")
        values = list(grey.getdata())
        print(
            f"verified {body_name:7s} -> {path} size={image.size} "
            f"mean={sum(values) / len(values):.1f} min={min(values)} max={max(values)}"
        )

    luna_source = Image.open(folder / "MoonAlbedoSource2048.jpg").convert("RGB")
    target = folder / "MoonAlbedo2048.ppm"
    luna_source.save(target)
    print(f"wrote {target} size={luna_source.size}")


if __name__ == "__main__":
    main()

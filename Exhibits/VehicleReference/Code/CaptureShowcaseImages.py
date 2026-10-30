#!/usr/bin/env python3
"""Losslessly convert four native ControlVehicle showcase PPM frames to PNG."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[3]
NAMES = ("VehicleSunSkyClouds", "VehicleFog", "VehicleSunset", "VehicleLocalLights")
WIDTH, HEIGHT = 640, 400
HEADER = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def convert(source_dir, output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    results = {}
    for name in NAMES:
        source = source_dir / f"{name}.ppm"
        payload = source.read_bytes()
        if not payload.startswith(HEADER):
            raise ValueError(f"{source}: expected a 640x400 binary PPM frame")
        pixels = payload[len(HEADER):]
        if len(pixels) != WIDTH * HEIGHT * 3:
            raise ValueError(f"{source}: RGB payload has {len(pixels)} bytes")
        scanlines = b"".join(b"\0" + pixels[y * WIDTH * 3:(y + 1) * WIDTH * 3]
                              for y in range(HEIGHT))
        encoded = (b"\x89PNG\r\n\x1a\n"
                   + png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
                   + png_chunk(b"IDAT", zlib.compress(scanlines, 9))
                   + png_chunk(b"IEND", b""))
        (output_dir / f"{name}.png").write_bytes(encoded)
        results[name] = hashlib.sha256(pixels).hexdigest()
    if len(set(results.values())) != len(NAMES):
        raise ValueError("Showcase variants must produce distinct rendered frames")
    settings = {
        "shared": {"pose": 0, "camera": "same 8, -9, 4 camera framing for every variant",
                   "seed": "0x5a17", "clouds_enabled": True, "resolution": [WIDTH, HEIGHT],
                   "accumulation_frames": 8, "ris_candidates": 8, "environment_samples": 8},
        "VehicleSunSkyClouds": {"sun_direction": [0.35, -0.55, 0.7582875444],
                                 "sun_irradiance": [20, 20, 20], "fog_enabled": False},
        "VehicleFog": {"fog_enabled": True, "density_at_base": 0.028, "base_height_m": 0,
                       "falloff_height_m": 22, "fog_colour": [0.58, 0.68, 0.82]},
        "VehicleSunset": {"sun_direction_from": [-0.55, -0.18, 0.15],
                           "sun_irradiance": [13, 6.8, 3.4], "fog_enabled": False},
        "VehicleLocalLights": {"sun_irradiance": [4, 4.6, 6.2], "fog_enabled": False,
                                "local_point_lights": [
                                    {"position": [0, -3, 2], "radiance": [65, 27, 10], "reach": 12},
                                    {"position": [1.5, 2.5, 3.5], "radiance": [12, 38, 78], "reach": 10}],
                                "exhaustive_light_sum": True, "spatial_reuse": False,
                                "ris_candidates": 4, "environment_samples": 16},
    }
    manifest = {"extent": [WIDTH, HEIGHT], "renderer": "native C++ CPU ReferenceRenderer",
                "shared_settings": settings["shared"], "variant_settings": {k: v for k, v in settings.items() if k != "shared"},
                "rgb_sha256": results, "lossless_png_conversion": True}
    (output_dir / "Manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"PASS ControlVehicle environment showcase: {len(NAMES)} distinct native frames, "
          "losslessly converted to PNG")
    for name, digest in results.items():
        print(f"SHOWCASE_IMAGE {name} sha256={digest}")


if __name__ == "__main__":
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "_ClaudeScratch/vehicle-reference/showcase"
    output = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "Exhibits/VehicleReference/Capture/Showcase"
    try:
        convert(source, output)
    except (OSError, ValueError) as error:
        print(f"FAIL ControlVehicle environment showcase conversion: {error}", file=sys.stderr)
        raise SystemExit(1)

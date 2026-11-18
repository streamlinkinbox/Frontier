"""Build display evidence and check linear buffers before any display mapping."""

import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

p, d = map(Path, sys.argv[1:])
out = d / "frames"
out.mkdir(exist_ok=True)
stats = {}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def raw(name, channels=4, w=480, h=270):
    f = p / (name + ".raw")
    a = np.fromfile(f, "<f4").reshape(h, w, channels)
    assert np.isfinite(a).all(), name
    stats.setdefault(name, {}).update(
        {
            "sha256": sha(f),
            "extent": [w, h],
            "min": float(a.min()),
            "max": float(a.max()),
            "nonfinite": 0,
        }
    )
    return a[::-1]


def rgb(a, exposure):
    v = exposure * a
    v = v / (1 + v)
    return Image.fromarray(
        np.uint8(
            np.clip(
                np.where(v <= 0.0031308, 12.92 * v, 1.055 * v ** (1 / 2.4) - 0.055)
                * 255
                + 0.5,
                0,
                255,
            )
        )
    )


def save(im, name):
    im.save(out / (name + ".png"))
    a = np.array(im)
    stats.setdefault(name, {})["display"] = {
        "min": int(a.min()),
        "max": int(a.max()),
        "black_samples": int((a == 0).sum()),
        "white_samples": int((a == 255).sum()),
        "samples": a.size,
    }


sun = raw("sun-reference", 1)[:, :, 0]
edge = (sun > 0) & (sun < 1)
assert edge.sum() > 0
for s in [
    "pcf",
    "pcss",
    "contact_pcss",
    "smrt",
    "hybrid",
    "sdf",
    "clip-shadow",
    "native-cap",
    "packet",
    "sun-reference",
]:
    a = sun if s == "sun-reference" else raw(s)[:, :, 0]
    save(Image.fromarray(np.uint8(np.clip(a, 0, 1) * 255 + 0.5)), s)
    stats[s].update(
        rmse=float(np.sqrt(np.mean((a - sun) ** 2))),
        partial_rmse=float(np.sqrt(np.mean((a[edge] - sun[edge]) ** 2))),
    )
for s in ["native-local", "native-clip", "native-hybrid", "native-cap", "native-gi"]:
    f = p / (s + ".raw")
    g = p / (s + ".cpp.raw")
    assert sha(f) == sha(g), s
    stats.setdefault(s, {})["native_webgl_sha256"] = sha(g)
for s in ["null", "point", "area", "sun", "all", "emission", "emission-old"]:
    a = raw(s)[:, :, :3]
    assert (a.min() >= 0) and (a.max() == 0 if s == "null" else a.max() > 0), s
    save(rgb(a, 1), s)
assert sha(p / "emission.raw") == sha(p / "emission-repeat.raw")
assert sha(p / "combined-view.png") == sha(p / "repeat-view.png")
stats["repeats"] = {n: sha(p / n) for n in ["emission-repeat.raw", "repeat-view.png"]}
flo = (
    np.fromfile(p / "native/native-cap/uGNormMat.f32", "<f4").reshape(270, 480, 4)[
        ::-1, :, 3
    ]
    == 1
)
ref = raw("lamp64", 3)
assert np.count_nonzero(flo) > 0
for s in ["lamp64", "lamp1", "lamp16", "lamps-fast", "emission", "emission-old"]:
    a = ref if s == "lamp64" else raw(s, 3 if s in ["lamp1", "lamp16"] else 4)[:, :, :3]
    stats[s]["floor_rmse_lamp64"] = float(np.sqrt(np.mean((ref[flo] - a[flo]) ** 2)))
    save(rgb(a, 4), s + "-transfer")


# Nearest-cell display of an actual GPU XZ readback. No synthetic car outline.
# Same metric scale on both axes. Outside lattice is dark grey. 5cm colour bands.
def section(s):
    a = raw(s, 4, 144, 64)[::-1, :, 3]
    x = -5.6 + (np.arange(480) + 0.5) * 11.2 / 480
    z = 4.8 - (np.arange(270) + 0.5) * 11.2 / 480
    iz = np.floor((z + 0.35) / 0.09).astype(int)
    ix = np.floor((x + 9.45) / (18.9 / 144)).astype(int)
    v = a[np.clip(iz, 0, 63)[:, None], ix]
    near = np.exp(-abs(v) / 0.5)
    r = np.where(v < 0, 235, 23 + 20 * near)
    g = np.where(v < 0, 105, 35 + 140 * near)
    b = np.where(v < 0, 65, 53 + 135 * near)
    col = np.stack(np.broadcast_arrays(r, g, b), axis=-1)
    col[abs(v) < 0.045] = [245, 239, 218]
    col[(iz < 0) | (iz >= 64)] = [13, 17, 22]
    assert v.min() < 0 < v.max()
    return Image.fromarray(col.astype("u1"))


frames = [Image.open(p / f"move{i:02d}.png").convert("RGB") for i in range(12)]
slices = [section(f"slice{i:02d}") for i in range(12)]
for name, seq in [("drive", frames), ("signed-field", slices)]:
    assert all(im.size == (480, 270) for im in seq)
    assert len({im.tobytes() for im in seq}) == 12
    save(seq[0], name + "-start")
    save(seq[-1], name + "-end")
    seq[0].save(
        out / (name + ".png"),
        save_all=True,
        append_images=seq[1:],
        duration=140,
        loop=0,
        disposal=0,
        blend=0,
    )
emoving = [rgb(raw(f"emove{i:02d}")[:, :, :3], 4) for i in range(12)]
assert len({f.tobytes() for f in emoving}) == 12
emoving[0].save(
    out / "emissive-motion.png",
    save_all=True,
    append_images=emoving[1:],
    duration=140,
    loop=0,
    disposal=0,
    blend=0,
)
frames[0].save(
    out / "drive.gif", save_all=True, append_images=frames[1:], duration=140, loop=0
)
save(section("slice"), "clip-xz")
for s in ["pt1", "pt16", "pt64", "combined-view"]:
    im = Image.open(p / (s + ".png")).convert("RGB")
    assert im.size == (480, 270) and np.array(im).std() > 5
    save(im, s)
stats["partial_sun_pixels"] = int(edge.sum())
stats["floor_pixels"] = int(flo.sum())
(d / "checks.json").write_text(json.dumps(stats, indent=2) + "\n")

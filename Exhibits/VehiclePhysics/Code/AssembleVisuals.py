#!/usr/bin/env python3
"""Assembles SceneRender PPM output into annotated PNG stills and GIFs.

Usage: AssembleVisuals.py <scratch-frames-dir> <output-dir>
Expects:
  <frames>/hero/{hero,hero-rear}.ppm
  <frames>/tyre/{tyre-nominal,tyre-flat,tyre-lattice}.ppm
  <frames>/{drive,burnout,jolt}/frame-####.ppm
Writes annotated PNG stills + GIFs into <output-dir>.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for name in ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf"):
        for root in ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts"):
            p = Path(root) / name
            if p.exists():
                return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


TITLE = font(30)
SUB = font(21)


def annotate(img, title, sub):
    out = img.convert("RGB")
    d = ImageDraw.Draw(out)
    w, _ = out.size
    d.rectangle([0, 0, w, 84], fill=(10, 12, 16))
    d.text((18, 10), title, font=TITLE, fill=(235, 240, 245))
    d.text((18, 50), sub, font=SUB, fill=(160, 180, 205))
    return out


def still(frames, outdir, path, title, sub):
    img = Image.open(frames / path)
    annotate(img, title, sub).save(outdir / (Path(path).stem + ".png"))
    print("still", Path(path).stem + ".png")


def gif(frames, outdir, subdir, name, title, sub, fps=20, keep_every=1):
    paths = sorted((frames / subdir).glob("frame-*.ppm"))[::keep_every]
    imgs = []
    for p in paths:
        img = Image.open(p).convert("RGB")
        img = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)
        imgs.append(img)
    imgs[0] = annotate(imgs[0], title, sub)
    dst = outdir / name
    pal = [im.convert("P", palette=Image.ADAPTIVE, colors=160, dither=Image.Dither.NONE) for im in imgs]
    pal[0].save(dst, save_all=True, append_images=pal[1:], duration=int(1000 / fps), loop=0, optimize=True)
    print(f"gif {name}: {len(imgs)} frames, {dst.stat().st_size // 1024} KB")


def main():
    frames, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    still(frames, outdir, "hero/hero.ppm",
          "ControlVehicle - real .facet geometry, 480 Hz spring-model physics",
          "Settled on analytically deformed spring tyres. Flat-shaded software raster.")
    still(frames, outdir, "hero/hero-rear.ppm",
          "ControlVehicle - rear three-quarter",
          "Authored materials (BodyCoat chartreuse, headlight/taillight glass, black plastic flanks).")
    still(frames, outdir, "tyre/tyre-nominal.ppm",
          "Analytical spring tyre @ 220 kPa",
          "Contact flattening and sidewall bulge from the demo's spring-deformation fallback.")
    still(frames, outdir, "tyre/tyre-flat.ppm",
          "Analytical spring tyre @ 30 kPa (flat)",
          "Same vehicle re-settled at 30 kPa: the carcass spring sags, the mesh follows.")
    still(frames, outdir, "tyre/tyre-lattice.ppm",
          "Independent 84-particle lattice @ 220 kPa",
          "28x3 particles (blue = crown, amber = sidewall); separate from the demo's 160-particle XPBD.")
    still(frames, outdir, "tyre/tyre-demo-xpbd.ppm",
          "VehicleHMI demo XPBD shape @ 30 kPa",
          "160-particle projection; the spring still carries chassis load, as in the demo.")
    still(frames, outdir, "tyre/tyre-demo-solid.ppm",
          "VehicleHMI demo 3D solid shape @ 30 kPa",
          "128 Hex8 cells; the spring still carries chassis load, as in the demo.")
    gif(frames, outdir, "drive", "SlalomDrive.gif",
        "Slalom drive - spring-model tyre forces (480 Hz)",
        "Closed-loop steering at 15 m/s. Tyre deflection + camber deformation live per frame.")
    gif(frames, outdir, "burnout", "Burnout.gif",
        "Line-lock burnout - rear tread heating (thermal solver)",
        "Tread mesh tint = TyreThermalSolver surface temperature (+78 K in the proof exhibit).")
    gif(frames, outdir, "jolt", "ConeHits.gif",
        "Jolt loose-body physics - cone row at 10 m/s",
        "Cones are Jolt bodies knocked over by the chassis proxy (velocity-delta exchange @ 120 Hz).")


if __name__ == "__main__":
    main()

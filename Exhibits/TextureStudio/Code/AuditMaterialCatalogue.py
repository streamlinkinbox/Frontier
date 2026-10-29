#!/usr/bin/env python3
"""Audit every shipped material against its HTML Alloy SourceRecipe.

For each of the 131 catalogue entries this reports:
  * the HTML kernel type and recipe id (from Library.toml SourceRecipe),
  * what the native .material actually carries (procedural ops, texture slots,
    paint block) — i.e. whether the material renders flat,
  * which recipe controls resolve to a native coefficient through the same
    rules the inspector uses (shared procedural pool first, then stratum
    scalars, then automotive paint), and which stay locked.

Outputs Exhibits/TextureStudio/Results/MaterialCatalogueAudit.json plus a
human summary on stdout. Regenerate after any catalogue conversion:

    python3 Exhibits/TextureStudio/Code/AuditMaterialCatalogue.py
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ARCHIVE = ROOT / "EngineContent/MaterialArchives/TextureStudio"
RESULTS = ROOT / "Exhibits/TextureStudio/Results"

STRATUM_KEYS = {
    "roughness", "coatRoughness", "metalness", "ior", "coatIor", "coat",
    "anisotropy", "specular", "heightStrength", "normalStrength",
}
PAINT_KEYS = {
    "flakes", "flakeSize", "flakeScale", "depth", "flakeRoughnessMin",
    "flakeRoughnessMax", "flakeMetalnessMin", "flakeMetalnessMax", "flakeTilt",
    "flakeLayerDepth", "orangePeel", "orangePeelScale", "flakeLayers",
}
# ResolveProceduralParamKey aliases (Engine/Render/MaterialProceduralBaking.cpp).
ALIASES = {"weaveAngle": "angle", "scratchAngle": "angle"}


def parse_library():
    text = (ARCHIVE / "Library.toml").read_text()
    materials = []
    for block in re.split(r"\n\[\[Materials\]\]\n", text)[1:]:
        path = re.search(r'Path = "([^"]+)"', block).group(1)
        src = re.search(r"SourceRecipe = \{(.*?)\}\n", block, re.S).group(1)
        recipe = {k: v.strip() for k, v in re.findall(r'"([^"]+)" = ("[^"]*"|[^,}]+)', src)}
        controls = []
        for control in re.split(r"\n\[\[Materials\.Controls\]\]\n", block)[1:]:
            head, _, rest = control.partition("\n[[Materials.Controls.Targets]]")
            entry = {
                "id": re.search(r'Id = "([^"]+)"', head).group(1),
                "caption": re.search(r'Caption = "([^"]*)"', head).group(1),
                "direct": re.search(r"Direct = (true|false)", head).group(1) == "true",
                "targets": re.findall(r'Key = "([^"]+)"', rest),
            }
            # Non-slider controls (colour wells etc.) carry no Direct flag shape we need;
            # anything without targets and without Direct resolves through its own id.
            controls.append(entry)
        materials.append({"path": path, "recipe": recipe, "controls": controls})
    return materials


def parse_material(path):
    text = (ARCHIVE / path).read_text()
    ops = sorted(set(re.findall(r'Op = "([^"]+)"', text)))
    params = set()
    for block in re.findall(r"\[Strata\.Channels\.Params\](.*?)(?=\n\[|\Z)", text, re.S):
        params.update(re.findall(r"^([A-Za-z0-9_]+) =", block, re.M))
    textures = re.findall(r'^\w+ = "(Textures/[^"]+)"', text, re.M)
    return {
        "ops": ops,
        "params": params,
        "textures": textures,
        "mask": [int(m) for m in re.findall(r"TextureMask = (\d+)", text)],
        "paint": "[AutomotivePaint]" in text or "Paint =" in text,
        "radiance": bool(re.search(r"^Radiance = ", text, re.M)),
        "strata": len(re.findall(r"\[\[Strata\]\]", text)),
    }


def resolves(key, native):
    if key in native["params"] or ALIASES.get(key) in native["params"]:
        return "pool"
    if key == "detailScale" and "ribs" in native["params"]:
        return "pool"  # ribbedSheet rib-frequency alias
    if key == "emissionStrength" and native["radiance"]:
        return "radiance"
    if key in STRATUM_KEYS:
        index = 1 if key.startswith("coat") and key != "coat" else 0
        if key == "coat":
            index = 1
        return "stratum" if index < native["strata"] else None
    if native["paint"] and key in PAINT_KEYS:
        return "paint"
    return None


def main():
    materials = parse_library()
    report = []
    flat, locked_materials = [], []
    type_counts, op_counts = {}, {}
    total_controls = resolved_controls = 0
    for material in materials:
        recipe = material["recipe"]
        native = parse_material(material["path"])
        html_type = int(float(recipe.get("type", -1)))
        recipe_id = recipe.get("recipeId", "").strip('"')
        type_counts[html_type] = type_counts.get(html_type, 0) + 1
        for op in native["ops"]:
            op_counts[op] = op_counts.get(op, 0) + 1
        textured = bool(native["ops"] or native["textures"])
        unresolved = []
        for control in material["controls"]:
            keys = control["targets"] or [control["id"]]
            total_controls += 1
            if all(resolves(key, native) for key in keys):
                resolved_controls += 1
            else:
                unresolved.append({
                    "id": control["id"], "caption": control["caption"],
                    "keys": [k for k in keys if not resolves(k, native)],
                })
        entry = {
            "path": material["path"], "htmlType": html_type, "recipeId": recipe_id,
            "nativeOps": native["ops"], "textureMask": native["mask"],
            "textured": textured, "controls": len(material["controls"]),
            "unresolvedControls": unresolved,
        }
        report.append(entry)
        if not textured:
            flat.append(entry)
        if unresolved:
            locked_materials.append(entry)
    RESULTS.mkdir(parents=True, exist_ok=True)
    out = RESULTS / "MaterialCatalogueAudit.json"
    out.write_text(json.dumps({
        "materials": report,
        "htmlTypes": {str(k): v for k, v in sorted(type_counts.items())},
        "nativeOps": dict(sorted(op_counts.items())),
    }, indent=1) + "\n")
    print(f"materials={len(report)} textured={len(report) - len(flat)} flat={len(flat)}")
    print(f"controls resolved {resolved_controls}/{total_controls}; "
          f"materials with locked controls: {len(locked_materials)}")
    print("\nFLAT (no ops, no textures) by HTML type:")
    by_type = {}
    for entry in flat:
        by_type.setdefault((entry["htmlType"], entry["recipeId"]), []).append(entry["path"])
    for (html_type, recipe_id), paths in sorted(by_type.items()):
        print(f"  type {html_type:>2} {recipe_id:<24} {len(paths):>2}: {', '.join(p[:-9] for p in paths)}")
    print("\nLOCKED CONTROLS by material:")
    for entry in locked_materials:
        keys = sorted({k for c in entry["unresolvedControls"] for k in c["keys"]})
        print(f"  {entry['path'][:-9]:<36} type {entry['htmlType']:>2} {entry['recipeId']:<22} {keys}")
    print(f"\nreport: {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

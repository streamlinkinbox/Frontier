#!/usr/bin/env python3
"""Compile SolidArc and run focused regressions in an x64 MSVC developer shell."""
from __future__ import annotations

import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Editor/AuthoringTools/Modelling/SolidArc"
OUTPUT = ROOT.parent / "_AgentScratch/build/msvc-solidarc"


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    if sys.platform != "win32" or not shutil.which("cl.exe"):
        raise RuntimeError("Use an x64 Visual Studio developer shell with cl.exe on PATH")
    objects, logs, proofs = (OUTPUT / name for name in ("objects", "logs", "proofs"))
    for folder in (objects, logs, proofs):
        folder.mkdir(parents=True, exist_ok=True)
    sources = sorted(path for folder in ("Kernel", "Presentation", "Interaction", "Document", "Console")
                     for path in (SOURCE / folder).glob("*.cpp") if path.name != "SolidArcConsole.cpp")
    sources.append(SOURCE / "Editor/SolidArcOutlinerAdapter.cpp")
    flags = ["/nologo", "/c", "/std:c++20", "/EHsc", "/MD", "/O2", "/W4", "/WX", "/utf-8",
             "/permissive-", "/DNOMINMAX", "/D_CRT_SECURE_NO_WARNINGS",
             f'/DSOLIDARC_PROOF_FOLDER="{proofs.as_posix()}"', f"/I{SOURCE}", f"/I{SOURCE / 'Presentation'}"]

    def run(command: list[str], name: str) -> None:
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace")
        (logs / f"{name}.log").write_text(subprocess.list2cmdline(command) + "\n" + result.stdout, encoding="utf-8")
        if result.returncode:
            print(result.stdout, flush=True)
            if os.environ.get("GITHUB_ACTIONS"):
                diagnostics = [line for line in result.stdout.splitlines() if "error " in line or "warning " in line]
                message = "\n".join(diagnostics or result.stdout.splitlines()[-20:])[:12000]
                for offset in range(0, len(message), 3000):
                    part = message[offset:offset + 3000]
                    part = part.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
                    print(f"::error title={name}::{part}", flush=True)
            raise RuntimeError(f"{name} failed with exit code {result.returncode}")

    def compile_source(path: Path) -> Path:
        obj = objects / (str(path.relative_to(SOURCE)).replace(os.sep, "_") + ".obj")
        print(f"Compile {path.relative_to(SOURCE)}", flush=True)
        run(["cl.exe", *flags, str(path), f"/Fo{obj}"], obj.stem)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        futures = [executor.submit(compile_source, source) for source in sources]
        compiled = []
        failures = []
        for future in concurrent.futures.as_completed(futures):
            try:
                compiled.append(future.result())
            except RuntimeError as error:
                failures.append(str(error))
        if failures:
            raise RuntimeError("\n".join(failures))
    archive = OUTPUT / "SolidArc.lib"
    run(["lib.exe", "/nologo", f"/OUT:{archive}", *map(str, compiled)], "Archive")

    def link_program(source: Path, name: str) -> Path:
        obj = compile_source(source)
        executable = OUTPUT / f"{name}.exe"
        run(["link.exe", "/nologo", f"/OUT:{executable}", str(obj), str(archive)], name + "Link")
        return executable

    console = link_program(SOURCE / "Console/SolidArcConsole.cpp", "SolidArc")
    for name in ("Kernel", "Blend", "MultiEdgeFillet", "Document", "Feature", "SurfaceOffset"):
        executable = link_program(SOURCE / f"Verification/{name}Verification.cpp", name + "Verification")
        run([str(executable)], name + "Verification")
        print(f"PASS {name}Verification", flush=True)
    # The Liger journals live with Project-Drive's vehicle content, which is not in every checkout.
    #    Without them the kernel is still fully verified above; only the replay half is skipped.
    documents = sorted((ROOT / "Projects/Project-Drive/Content/Vehicles/Liger").glob("*.arc"))
    if not documents:
        print("SKIP Liger replay: Projects/Project-Drive/Content/Vehicles/Liger is not in this "
              "checkout", flush=True)
        print("PASS SolidArc kernel, raster, interaction, selection, topology, blends, fillets, "
              "documents, features and surface offsets under MSVC x64 /MD", flush=True)
        return 0
    if len(documents) != 6:
        raise RuntimeError(f"Expected six Liger journals, found {len(documents)}")
    for document in documents:
        run([str(console), "--proofs", str(proofs), "-c", f'open "{document.as_posix()}"'], document.stem)
        print(f"PASS replay {document.name}", flush=True)
    cowl = ROOT / "Projects/Project-Drive/Content/Vehicles/Liger/Reconstruction/Liger_Front_Cowl.arc"
    executable = link_program(SOURCE / "Verification/CowlVerification.cpp", "CowlVerification")
    run([str(executable), str(cowl)], "CowlVerification")
    print("PASS native cowl topology, orientation, persistence and rendering", flush=True)
    reconstruction = cowl.parent
    run([str(executable), str(reconstruction / "Liger_Front_Cowl_Consolidated.arc"),
         str(proofs / "cowl-consolidated.f64"), str(reconstruction / "Liger_Front_Cowl_Consolidated.queries")],
        "ConsolidatedCowlVerification")
    executable = link_program(SOURCE / "Verification/FacetVerification.cpp", "FacetVerification")
    run([str(executable), str(reconstruction / "Liger_Roof_Glass_Frame.arc"),
         str(reconstruction / "Liger_Roof_Glass_Frame.triangles"), str(reconstruction / "Liger_Exterior_Partial.arc")],
        "FacetVerification")
    print("PASS consolidated cowl, source-faceted frame and partial assembly", flush=True)
    executable = link_program(SOURCE / "Verification/BodyVerification.cpp", "BodyVerification")
    run([str(executable), str(reconstruction / "Liger_Main_Body.arc"),
         str(reconstruction / "Liger_Main_Body.queries"), str(OUTPUT / "main-body.f64"),
         str(reconstruction / "Liger_Reconstruction.arc")], "BodyVerification")
    print("PASS native main body, explicit junction sheets, persistence and combined reconstruction", flush=True)
    run([str(OUTPUT / "FeatureVerification.exe"), str(reconstruction / "Liger_Feature_Aligned.arc"),
         str(OUTPUT / "contour-edges.json"), "--roundtrip"], "LigerContourVerification")
    print("PASS aligned Liger features, paired crowns, lower-fade continuity and mirror symmetry", flush=True)
    executable = link_program(SOURCE / "Verification/RoofRepairVerification.cpp", "RoofRepairVerification")
    run([str(executable), str(reconstruction / "Liger_Feature_Aligned.arc"),
         str(reconstruction / "Liger_Roof_Repair.arc")], "LigerRoofRepairVerification")
    print("PASS native roof repair, unchanged surrounding skin, mirror layout, topology and persistence", flush=True)
    executable = link_program(SOURCE / "Verification/GuideVerification.cpp", "GuideVerification")
    run([str(executable), str(reconstruction / "Liger_Roof_Repair.arc"),
         str(reconstruction / "Liger_Guide_Candidates.arc")], "LigerGuideVerification")
    print("PASS native named guides, surface proximity, mirrored pairs and unchanged repaired skin", flush=True)
    run([str(executable), "--consolidated", str(reconstruction / "Liger_Roof_Repair.arc"),
         str(reconstruction / "Liger_Consolidated.arc")], "LigerConsolidationVerification")
    run([str(executable), "--layout", str(reconstruction / "Liger_Consolidated.arc"),
         str(reconstruction / "Liger_Layout.arc")], "LigerLayoutVerification")
    run([str(OUTPUT / "SurfaceOffsetVerification.exe"), str(reconstruction / "Liger_Surface_Offset.arc"),
         str(proofs / "LigerSurfaceOffset")], "LigerSurfaceOffsetVerification")
    PatchExecutable = link_program(SOURCE / "Verification/LigerPatchVerification.cpp", "LigerPatchVerification")
    run([str(PatchExecutable), str(reconstruction / "Liger_Window_Review.arc"),
         str(reconstruction / "Liger_Patch_Repair.arc"), str(proofs / "LigerPatchRepair")],
        "LigerPatchRepairVerification")
    run([str(PatchExecutable), str(reconstruction / "Liger_Patch_Repair.arc"),
         str(reconstruction / "Liger_Orange_Repair.arc"), str(proofs / "LigerOrangeRepair"), "--orange"],
        "LigerOrangeRepairVerification")
    print("MSVC SolidArc: console linked, focused regressions and all Liger journals passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)

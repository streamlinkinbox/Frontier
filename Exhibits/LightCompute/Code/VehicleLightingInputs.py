# 🧩 Recover pinned authored vehicle/preset inputs into ignored storage without production wiring.
from pathlib import Path
import base64
import concurrent.futures
import hashlib
import json
import subprocess
import sys
import tomllib

Root = Path(__file__).resolve().parents[3]
Destination = Root / "_ClaudeScratch/vehicle-lighting-inputs"
Revision = "3de9061ab614856de03ef61b534c641061d14a53"
Repository = "eosclient0001-rgb/Frontier"
Tree = json.loads(subprocess.check_output(["gh", "api", f"repos/{Repository}/git/trees/{Revision}?recursive=1"]))
Entries = {Entry["path"]: Entry for Entry in Tree["tree"] if Entry["type"] == "blob"}
Recovered = {}


def Retrieve(Pathname):
    Entry = Entries[Pathname]
    File = Destination / Pathname
    if not File.exists():
        Response = json.loads(
            subprocess.check_output(["gh", "api", f"repos/{Repository}/contents/{Pathname}?ref={Revision}"])
        )
        Payload = base64.b64decode(Response["content"])
        File.parent.mkdir(parents=True, exist_ok=True)
        File.write_bytes(Payload)
    Payload = File.read_bytes()
    assert hashlib.sha1(b"blob " + str(len(Payload)).encode() + b"\0" + Payload).hexdigest() == Entry["sha"], Pathname
    Recovered[Pathname] = hashlib.sha256(Payload).hexdigest()
    return File


Vehicle = "Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/"
Library = "EngineContent/MaterialArchives/TextureStudio/"
Paths = [
    "Exhibits/VehicleReference/Code/VehicleCapture.cpp",
    "Exhibits/ClusteredLighting/Fixtures/Synthetic.ies",
    Vehicle + "Source/MaterialSpecification.js",
]
Paths += [Vehicle + "Archive/ControlVehicle" + Part + ".facet" for Part in ("Body", "Tyre", "Rim")]
Paths += [
    Library + Name + ".material"
    for Name in ("racing-green", "performance-rubber", "crystal-glass", "pp-polypropylene", "indigo-denim")
]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as Executor:
    list(Executor.map(Retrieve, Paths))
Textures = []
for File in (Destination / Library).glob("*.material"):
    for Stratum in tomllib.loads(File.read_text())["Strata"]:
        Textures += [
            Library + Texture
            for Channel, Texture in Stratum.get("Textures", {}).items()
            if Channel in ("Albedo", "Normal")
        ]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as Executor:
    list(Executor.map(Retrieve, sorted(set(Textures))))
Source = (Root / "Exhibits/LightCompute/Code/PrepareMaterialScene.py").read_text()
Source = Source.replace("Root = Path(__file__).resolve().parents[3]", f"Root = Path({str(Root)!r})")
Source = Source.replace(
    "Root / 'EngineContent/MaterialArchives/TextureStudio'", f"Path({str(Destination / Library)!r})"
)
exec(
    compile(Source, "PrepareMaterialScene.py", "exec"),
    {"__file__": str(Root / "Exhibits/LightCompute/Code/PrepareMaterialScene.py")},
)
Palette = Destination / Vehicle / "Source/MaterialSpecification.js"
Module = Destination / "MaterialSpecification.mjs"
Module.write_text(Palette.read_text())
subprocess.run(
    [
        "node",
        "--input-type=module",
        "-e",
        f"import {{ConstructMaterialSpecification}} from {json.dumps(Module.as_uri())}; import {{writeFileSync,mkdirSync}} from 'node:fs'; mkdirSync('_ClaudeScratch/vehicle-reference',{{recursive:true}}); writeFileSync('_ClaudeScratch/vehicle-reference/Materials.txt',ConstructMaterialSpecification().map(M=>[...M.Albedo,M.Roughness,M.Metalness,...M.Emission].join(' ')).join('\\n')+'\\n');",
    ],
    cwd=Root,
    check=True,
)
Source = (
    (Destination / Paths[0])
    .read_text()
    .replace("../../LightCompute/Code/LightComputeFormat.h", "Exhibits/LightCompute/Code/LightComputeFormat.h")
)
Source = Source.replace(
    'Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/"\n                                        "Archive/ControlVehicle',
    "_ClaudeScratch/vehicle-lighting-inputs/Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Archive/ControlVehicle",
)
Source = Source.replace(
    "Exhibits/ClusteredLighting/Fixtures/Synthetic.ies",
    "_ClaudeScratch/vehicle-lighting-inputs/Exhibits/ClusteredLighting/Fixtures/Synthetic.ies",
)
(Destination / "VehicleCapture.cpp").write_text(Source)
(Root / "Exhibits/LightCompute/Results/VehicleLighting-inputs.json").write_text(
    json.dumps({"repository": Repository, "revision": Revision, "sha256": Recovered}, indent=2) + "\n"
)
print("VEHICLE_LIGHTING_INPUTS verified=", len(Recovered))

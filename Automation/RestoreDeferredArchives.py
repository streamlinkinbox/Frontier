"""Restore bulk static TextureStudio and historical gallery assets deferred from the 128 MB sandbox patchset."""
from pathlib import Path
import subprocess

PinnedRepository = "https://github.com/eosclient0001-rgb/Frontier.git"
PinnedRevision   = "fa04adfb4a1db1a668126ca02b5910d74c72d73a"
DeferredPaths    = [
    "EngineContent/PatternArchives",
    "EngineContent/MaterialArchives",
    "Experimental/TextureStudio",
    "Experimental/SlateReference",
    "Exhibits/TextureStudio/Images",
    "Exhibits/VehiclePhysics/Visuals",
    "Exhibits/LogStream",
    "Exhibits/SpaceFormats/index.html",
    "Exhibits/ProgressEvidence.html",
]

Root = Path(__file__).resolve().parents[1]
subprocess.run(["git", "-C", str(Root), "fetch", PinnedRepository, PinnedRevision], check=True)
subprocess.run(["git", "-C", str(Root), "checkout", PinnedRevision, "--"] + DeferredPaths, check=True)
print("Restored deferred static archives from", PinnedRevision)

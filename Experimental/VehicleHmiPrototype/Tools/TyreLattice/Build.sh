#!/usr/bin/env bash
# Rebuilds Archive/TyreLattice.bin with the Slate engine's own ConstructTyreLattice for this vehicle's tyre. See README.md.
set -euo pipefail
Here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
App="$(cd "$Here/../.." && pwd)"
Scratch="$(mktemp -d)"
trap 'rm -rf "$Scratch"' EXIT
Slate="${SLATE_REPO:-https://github.com/SultanAladin/Slate.git}"
Branch="${SLATE_BRANCH:-arena/ca517016-slate}"

git clone --quiet --depth 1 --branch "$Branch" --filter=blob:none --sparse "$Slate" "$Scratch/slate"
git -C "$Scratch/slate" sparse-checkout set Engine/Foundation Engine/Shared Engine/SlateCompute/Compute/TyreSolver
g++ -std=c++20 -O2 -I"$Scratch/slate/Engine" -o "$Scratch/export_tyre_lattice" "$Here/ExportTyreLattice.cpp" \
    "$Scratch/slate/Engine/SlateCompute/Compute/TyreSolver/Source/TyreSolver.cpp"
#                                     out                          outer(particles) rim    section bead  carcass pressure segments
"$Scratch/export_tyre_lattice" "$App/Archive/TyreLattice.bin" 0.49855 0.3446 0.3958 0.30 12.0 220000 64

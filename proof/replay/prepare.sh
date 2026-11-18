#!/bin/sh
# Materialize the pinned lab in a fresh temporary directory; never switch branches.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
rev=c205cb698288ccd90bf909e25e28ce28d48d0dfa
git init -q "$scratch/source"
git -C "$scratch/source" fetch --quiet --depth 1 https://github.com/SultanAladin/Frontier-1.git "$rev"
git -C "$scratch/source" archive FETCH_HEAD Experimental/ShadowAlgorithmLab Experimental/DeterministicPathTracer | tar -x -C "$scratch"
(cd "$scratch"; patch -s -p1 < "$here/shadow-lab.patch")
asset="$scratch/Experimental/ShadowAlgorithmLab/Assets"
printf '%s  %s\n' 78a37f3a3c3270dd0e7078ef0ba23408fb20082d33b3696ed333caa1ed65ac19 "$asset/VehicleBenchmarkAssets.bin" | sha256sum -c -
c++ -std=c++17 -O3 "$here/BakeBodyCache.cpp" -o "$scratch/bake"
"$scratch/bake" "$asset/VehicleBenchmarkAssets.bin" "$asset/body.r8"
printf 'Scratch directory: %s\n' "$scratch"

#!/usr/bin/env bash
#===========================================================================================================================================
#  FacetClusterValidation/Build.sh - build and run the K1 geometry-build gate on a non-Windows host.
#
#  CPU ONLY: no device, no Vulkan, no shaders. The build it judges is pure C++20 over local-space facet soup, so this
#  gate is the one Division 1 check that also runs on the Linux sandbox, where the Windows delivery build cannot run.
#
#  Usage: bash Executables/Validation/FacetClusterValidation/Build.sh [--dump <path>]
#  Exit:  0 = all checks pass, 1 = a check failed, 2 = compilation failed.
#===========================================================================================================================================
set -u

ApplicationDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RepositoryRoot="$(cd "${ApplicationDirectory}/../../.." && pwd)"
BinaryDirectory="${RepositoryRoot}/Binaries/Validation"
Compiler="${CXX:-g++}"

mkdir -p "${BinaryDirectory}"

echo "[FacetClusterValidation] compiler ${Compiler}"
"${Compiler}" -std=c++20 -O2 -Wall -Wextra -I"${RepositoryRoot}" \
    -o "${BinaryDirectory}/FacetClusterValidation" \
    "${ApplicationDirectory}/FacetClusterValidation.cpp" \
    "${RepositoryRoot}/Engine/Geometry/FacetClusterBuild.cpp"
Status=$?
if [ ${Status} -ne 0 ]; then
    echo "[FacetClusterValidation] build failed (${Status})"
    exit 2
fi

"${BinaryDirectory}/FacetClusterValidation" "$@"

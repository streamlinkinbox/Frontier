#!/usr/bin/env bash
# File/policy tests only: no Vulkan driver is invoked.
set -euo pipefail
cd "$(dirname "$0")/../.."
Stage=$(mktemp -d)
trap 'rm -rf "$Stage"' EXIT
Flags=(-std=c++20 -O1 -g -Wall -Wextra -Werror -I.)
if [[ "${SANITIZE:-0}" == 1 ]]; then Flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-g++}" "${Flags[@]}" Tools/Build/Gates/PipelineCacheFileGate.cpp -o "$Stage/PipelineCacheFileGate"
"$Stage/PipelineCacheFileGate" "$Stage/files"

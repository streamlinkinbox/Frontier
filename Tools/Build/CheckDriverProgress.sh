#!/usr/bin/env bash
# CPU-only blocking-call simulation; does not require Vulkan or claim driver execution.
set -euo pipefail
cd "$(dirname "$0")/../.."
Stage=$(mktemp -d)
trap 'rm -rf "$Stage"' EXIT
Flags=(-std=c++20 -O1 -g -Wall -Wextra -Werror -pthread -I.)
if [[ "${SANITIZE:-0}" == 1 ]]; then Flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CXX:-g++}" "${Flags[@]}" Tools/Build/Gates/DriverProgressGate.cpp -o "$Stage/DriverProgressGate"
"$Stage/DriverProgressGate"

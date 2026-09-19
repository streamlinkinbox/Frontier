#!/usr/bin/env bash
# Sync tool panels into docs/ for GitHub Pages.
set -euo pipefail; cd "$(dirname "$0")/.."
stamp="$(date -u +%Y%m%d-%H%M)-$(git rev-parse --short HEAD)"
sed "s/__BUILD__/$stamp/" Editor/EditorTools/ParametricSketcher/Panel/index.html > docs/solidarc/index.html
echo "build $stamp"
echo "docs/ synced"

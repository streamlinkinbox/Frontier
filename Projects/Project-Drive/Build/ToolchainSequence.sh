#!/usr/bin/env bash
#============================================================================================================================================
#                                                TOOLCHAINSEQUENCE.SH
#============================================================================================================================================
# 📦 Compatibility forwarding route; Frontier.exe is the sole windowed host and ProjectDrive is a code image.

set -euo pipefail
RepositoryRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec pwsh -File "$RepositoryRoot/Tools/Build/ToolchainSequence.ps1" "$@"

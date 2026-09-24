#!/usr/bin/env bash
# Static descriptor-contract gate for the D6/D7 two-level traversal upload.
#
# The GPU shader has no dedicated BLAS descriptor bindings. When TlasInstanceCount > 0, the TLAS path reads
# BlasPlacements[].NodeOffset/LeafOffset and then indexes CwbvhNodes/CwbvhTris, which are bindings 8/9. Therefore
# UploadInstanceTraversal must replace bindings 8/9 with InstanceAcceleration's concatenated BLAS blobs before it binds
# TLAS buffers 27-30. This gate catches the exact omission that made multi-object GPU shadow rays miss all blockers.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

python3 - <<'PY'
from pathlib import Path
import sys

swap = Path('Engine/DeviceExchange/SwapchainExchange.cpp').read_text()
traversal = Path('Engine/Shaders/TraversalCWBVH.slang').read_text()
inst_h = Path('Engine/GeometricRaster/InstanceAcceleration.h').read_text()

def function_body(src: str, signature: str) -> str:
    start = src.find(signature)
    if start < 0:
        raise AssertionError(f'missing function signature: {signature}')
    brace = src.find('{', start)
    if brace < 0:
        raise AssertionError(f'missing function body: {signature}')
    depth = 0
    for i in range(brace, len(src)):
        ch = src[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return src[brace + 1:i]
    raise AssertionError(f'unbalanced function body: {signature}')

upload_instance = function_body(swap, 'void SwapchainExchange::UploadInstanceTraversal')
upload_world = function_body(swap, 'void SwapchainExchange::UploadTraversal')

checks = [
    ('InstanceAcceleration exposes QueryNodeBlob', 'QueryNodeBlob()' in inst_h),
    ('InstanceAcceleration exposes QueryLeafBlob', 'QueryLeafBlob()' in inst_h),
    ('UploadInstanceTraversal reads BLAS node blob', 'Instances.QueryNodeBlob()' in upload_instance),
    ('UploadInstanceTraversal reads BLAS leaf blob', 'Instances.QueryLeafBlob()' in upload_instance),
    ('UploadInstanceTraversal uploads BLAS nodes into binding-8 buffer', 'Vulkan->TraversalNodeBuffer' in upload_instance and 'BlasNodes.data()' in upload_instance),
    ('UploadInstanceTraversal uploads BLAS leaves into binding-9 buffer', 'Vulkan->TraversalLeafBuffer' in upload_instance and 'BlasLeaves.data()' in upload_instance),
    ('UploadInstanceTraversal records binding-8 capacity from BLAS nodes', 'TraversalNodeCapacity = static_cast<VkDeviceSize>(BlasNodes.size()) * sizeof(float)' in upload_instance),
    ('UploadInstanceTraversal records binding-9 capacity from BLAS leaves', 'TraversalLeafCapacity = static_cast<VkDeviceSize>(BlasLeaves.size()) * sizeof(float)' in upload_instance),
    ('UploadInstanceTraversal marks the active traversal resident', 'TraversalResident = true' in upload_instance),
    ('UploadTraversal tears down two-level residency for world fallback', 'InstanceTraversalResident = false' in upload_world),
    ('Shader closest-hit TLAS path indexes BLAS by placement offsets', 'TraceBlasClosest(objectO, objectD, objectR, result.hit.t, placement.NodeOffset, placement.LeafOffset)' in traversal),
    ('Shader occlusion TLAS path indexes BLAS by placement offsets', 'TraceBlasOccluded(objectO, objectD, objectR, tmax, placement.NodeOffset, placement.LeafOffset)' in traversal),
]

failed = [name for name, ok in checks if not ok]
if failed:
    print('[InstanceTraversalUpload] RED — descriptor contract is broken:')
    for name in failed:
        print(f'  - {name}')
    sys.exit(1)

print('[InstanceTraversalUpload] GREEN — two-level TLAS now has matching BLAS blobs in bindings 8/9')
PY

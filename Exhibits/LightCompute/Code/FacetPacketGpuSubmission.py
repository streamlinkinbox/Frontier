# 🧩 Reuse existing Vulkan submission with a bounded exhibit-only diagnostic capacity extension.
from pathlib import Path
import hashlib
import sys

Code = Path(__file__).resolve().parent
Source = (Code / "LightComputeValidation.cpp").read_text()
Prefix = Source[: Source.index("\nvoid ValidateFreshSampling(")]
Needle = "DiffuseOnly && DiffuseAuditSlabs == 40"
assert Prefix.count(Needle) == 1
Prefix = Prefix.replace(Needle, "DiffuseOnly && (DiffuseAuditSlabs == 40 || DiffuseAuditSlabs == 80)")
Path(sys.argv[1]).write_text(Prefix + "\n}\n")
print("PACKET_GPU_SUBMISSION capacity=80 sha256=" + hashlib.sha256(Prefix.encode()).hexdigest())
Source = (Code / "FacetCausalGpuValidation.cpp").read_text()
Comparison = Source[: Source.index("void ValidateCausalGpu(")]
Comparison = Comparison.replace('"FacetCausalGpuSubmission.h"', '"FacetPacketGpuSubmission.h"')
Path(sys.argv[2]).write_text(Comparison + "\n}\n")
print("PACKET_GPU_COMPARISONS sha256=" + hashlib.sha256(Comparison.encode()).hexdigest())

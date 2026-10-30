# 🧩 Reuse the exact existing Vulkan submission prefix,without copying it into a new maintained implementation.
from pathlib import Path
import hashlib
import sys

Code = Path(__file__).resolve().parent
Source = (Code / "LightComputeValidation.cpp").read_text()
Prefix = Source[: Source.index("\nvoid ValidateFreshSampling(")]
assert Prefix.rstrip().endswith("};")
Output = Path(sys.argv[1])
Output.write_text(Prefix + "\n}\n")
print("OCCLUSION_GPU_SUBMISSION exact_prefix_sha256=" + hashlib.sha256(Prefix.encode()).hexdigest())

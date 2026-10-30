# 🧩 Reuse exact mixed-scene types and independent visibility routines without maintaining a second implementation.
from pathlib import Path
import hashlib
import sys

Code = Path(__file__).resolve().parent
Source = (Code / "FacetMixedValidation.cpp").read_text()
Prefix = Source[: Source.index("void ValidateMixedUnits()")]
Path(sys.argv[1]).write_text(Prefix + "\n}\n")
print("FACET_NORM_PREFIX exact_sha256=" + hashlib.sha256(Prefix.encode()).hexdigest())

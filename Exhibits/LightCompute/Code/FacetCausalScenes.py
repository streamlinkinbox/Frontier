# 🧩 Instrument exact historical transport replay while retaining its estimator arithmetic and source constructors.
from pathlib import Path
import hashlib
import sys

Code = Path(__file__).resolve().parent
Source = (Code / "FacetAreaTransportValidation.cpp").read_text()
Begin = Source.index("void Capture(")
End = Source.index("\n}\nint main", Begin)
Replay = Source[Begin:End]
Replay = Replay.replace("void Capture(", "void CaptureCausal(")
Replay = Replay.replace("uint32_t Corpus=0;Corpus<3", "uint32_t Corpus=2;Corpus<3")
Needle = "                        Packed[Ordinal]="
Position = Replay.index(Needle)
Replay = (
    Replay[:Position]
    + """                        AttributeCausalPath(Fixture,Method,Packet,Lane,P[Ordinal],Primary,Path,H[Ordinal],O,D,
                            (Method<2?Normalizer:1/P[Ordinal].Density)*(Method>=4?.5f:1.f)/4);
"""
    + Replay[Position:]
)
Replay = (
    Replay.replace("221184", "73728")
    .replace("1474560", "491520")
    .replace("1179648", "393216")
    .replace("2359296", "786432")
)
Path(sys.argv[1]).write_text(Replay + "\n")
print("FACET_CAUSAL_REPLAY sha256=" + hashlib.sha256(Replay.encode()).hexdigest())

# 🧩 Derive fixed-period correct payload refresh plus independent fresh-six/eight component controls.
from pathlib import Path
import subprocess
import sys

Destination = Path(sys.argv[1])
subprocess.run([sys.executable, 'Exhibits/LightCompute/Code/CompileVertexCache.py', str(Destination)], check=True)
Source = Destination.read_text()
Fresh = Source[Source.index('void InlineVertexEstimate('):Source.index('uint VertexSlot(')]
Fresh = Fresh.replace('void InlineVertexEstimate(uint Pixel)', 'void FreshComponent(uint Pixel, uint Budget)')
Fresh = Fresh.replace('uint Method = 1;', 'uint Method = 6;')
assert Fresh.count('Index < 8') == 1
Fresh = Fresh.replace('Index < 8', 'Index < Budget').replace('TriangleOutgoing(Point) / 8', 'TriangleOutgoing(Point) / float(Budget)')
Source = Source.replace('uint VertexSlot(', Fresh + 'uint VertexSlot(', 1)
Source = Source.replace('    if (Method == 0) { InlineVertexEstimate(Pixel); return; }', '''    if (Method == 0) { InlineVertexEstimate(Pixel); return; }
    if (Method >= 4) { FreshComponent(Pixel, Method == 4 ? 6 : 8); return; }''')
Source = Source.replace('    if (Method == 2) SameRevision = true;  // Deliberate stale-light negative control.\n', '')
Source = Source.replace('    if (Method == 3) Reset = false;        // Deliberate stale-geometry/material negative control.\n', '')
Source = Source.replace('if (Pixel >= Extent.Dimensions.x || uint(Receivers[Pixel].MotionPixel.z) == 0) return;',
                        'if (Pixel >= Extent.Dimensions.x || uint(Receivers[Pixel].MotionPixel.z) == 0 || uint(Receivers[Pixel].MotionPixel.z) >= 4) return;')
Source = Source.replace('% 4096u', '% 1024u')
Destination.write_text(Source)

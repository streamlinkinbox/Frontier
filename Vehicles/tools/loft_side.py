#!/usr/bin/env python3
"""
Reconstruct Liger_Body_SideR.arc (and all Liger SolidArc deliverables) for the +Y
right-hand half-shell NURBS body and +Y 3D feature curve network.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from reconstruct_liger import main

if __name__ == "__main__":
    main()

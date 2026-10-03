#!/usr/bin/env python3
"""
Reconstruct Liger_Body_Surface.arc (and all Liger SolidArc deliverables) using exact
per-face Catmull-Clark B-spline NURBS patch fitting from Vehicles/Liger/mesh/*.npz.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from reconstruct_liger import main

if __name__ == "__main__":
    main()

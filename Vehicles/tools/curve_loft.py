#!/usr/bin/env python3
"""
Reconstruct Liger_Body_CurveLoft.arc (and all Liger SolidArc deliverables) combining
exact Catmull-Clark B-spline NURBS exterior patches with the 3D feature curve network.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from reconstruct_liger import main

if __name__ == "__main__":
    main()

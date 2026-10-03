#!/usr/bin/env python3
"""
Reconstruct Liger_Body_Panels.arc (and all Liger SolidArc deliverables) using exact
per-face Catmull-Clark B-spline NURBS patch fitting segmented into anatomical panel zones.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from reconstruct_liger import main

if __name__ == "__main__":
    main()

# SolidArc sample `.arc` documents

These are the native SolidArc construction journals found in the upstream SolidArc branch and kept here as lightweight C++-side CAD test inputs:

- `ToyCar.arc` — side/profile + plan-view booleans, axle bores, wheels and grille features.
- `ToySailboat.arc` — lofted hull sections, deck boolean, swept mast and sail outlines.
- `ToyBiplane.arc` — lofted fuselage, filleted cowl, radial plugs, wings and mirrored/arrayed details.

They are not browser assets; they are command scripts consumed by `Console/SolidArcConsole.cpp` or `ConsoleHost::RunScript(...)`.

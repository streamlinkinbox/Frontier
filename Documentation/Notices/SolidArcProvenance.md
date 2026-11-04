# SolidArc source provenance

The CAD sources originate in `unassignedinbox/Frontier` commit
`82ed251267ed8457ca3530d8228b91555572c25d`, with the SolidArc overlays and NURBS importer precision changes from
`SultanAladin/Slate` commit `c3a187909483aa8182ba96cf4916e3542a09395e`.
The previous UI foundation comparison uses `SultanAladin/Frontier` commit
`65cf0f131a6afe8bdcd722c006b10cfab0e1aafd` solely for three-way porting of CAD additions.

This change reuses the destination's existing ImGui, GLFW, toml++, stb, ThorVG and Vulkan headers.
The additional volk dependency is checksummed in Automation/UIDependencies.json; its original licence is retained
by the existing dependency recovery process and copied into CAD distributions.

Existing source/artwork notices remain applicable. This document does not invent a new licence grant.
The source-only change contains no downloaded third-party trees, user vehicle, historical product ZIPs,
web demonstrations or unrelated Slate framework sources. T-spline/CAD Form work remains on hold.

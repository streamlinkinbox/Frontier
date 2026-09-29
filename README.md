# Frontier

## Known limitation

Localized 5–20 cm fractures/chip removal for Natural Arches are deferred. The current terrain SDF uses roughly 2 m voxels, so it cannot faithfully represent those sub-voxel gaps. The earlier screen-space Voronoi overlay was removed because it looked like polygon outlines and pinholes rather than fractured sandstone. A proper implementation needs localized high-resolution SDF remeshing or an adaptive surface-fracture mesh.

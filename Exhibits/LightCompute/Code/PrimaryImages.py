"""Wrap actual Vulkan and independent native ray readbacks without altering pixels."""
from Images import Root, Output, Read, Png
for Name in ('Primary', 'PrimaryNative'):
    Pixels = Read(Root / f'_ClaudeScratch/light-compute/{Name}.ppm')
    (Output / f'Images/{Name}.png').write_bytes(Png(640, 400, Pixels))

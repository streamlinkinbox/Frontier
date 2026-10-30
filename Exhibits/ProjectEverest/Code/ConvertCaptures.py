"""Convert native CPU raster output to lossless viewable exhibits."""
from pathlib import Path
from PIL import Image

Exhibit = Path(__file__).resolve().parents[1]
for Capture in (Exhibit / 'Results').glob('*.ppm'):
    (Exhibit / 'Images').mkdir(exist_ok=True)
    Image.open(Capture).save(Exhibit / 'Images' / (Capture.stem + '.png'))

"""Create a 30x30, one-pixel black/white checkerboard and nearest preview."""
from pathlib import Path
from PIL import Image

out = Path(__file__).resolve().parents[1] / "build/icon-checkerboard"
out.mkdir(parents=True, exist_ok=True)
image = Image.new("L", (30, 30))
image.putdata([255 if (x + y) % 2 else 0 for y in range(30) for x in range(30)])
image.save(out / "checkerboard-30x30.png")
image.resize((600, 600), Image.Resampling.NEAREST).save(out / "checkerboard-20x.png")
assert image.size == (30, 30)
assert image.histogram()[0] == image.histogram()[255] == 450
print(out)

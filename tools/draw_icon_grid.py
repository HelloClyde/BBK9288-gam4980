"""A 30x30 cell guide: black grid lines and white interiors."""
from pathlib import Path
from PIL import Image, ImageDraw

out = Path(__file__).resolve().parents[1] / "build/icon-checkerboard"
out.mkdir(parents=True, exist_ok=True)
cells, step = 30, 20
side = cells * step + 1
image = Image.new("RGB", (side, side), "white")
draw = ImageDraw.Draw(image)
for i in range(cells + 1):
    p = i * step
    draw.line((p, 0, p, side - 1), fill="black", width=1)
    draw.line((0, p, side - 1, p), fill="black", width=1)
image.save(out / "grid-30x30-cells.png")
print(out / "grid-30x30-cells.png")

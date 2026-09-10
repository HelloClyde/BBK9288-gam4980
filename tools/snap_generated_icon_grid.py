"""Snap generated cell artwork back onto the user's exact Python grid."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageStat

root = Path(__file__).resolve().parents[1]
source = Path('C:/Users/ASUS/.codex/generated_images/01a00b11-be1b-7500-a1bd-6687121dedb3/exec-218bd0bb-1bc2-4dab-8c78-76bc06081a0a.png')
grid = Path('C:/Users/ASUS/AppData/Local/Temp/codex-clipboard-1f558e18-3d43-409b-8fd0-74abfb028ec0.png')
out = root / 'assets/9288/grid-study'
out.mkdir(parents=True, exist_ok=True)
art = Image.open(source).convert('L')

def lines(axis):
    count = art.width if axis == 0 else art.height
    positions = []
    for pos in range(count):
        samples = [art.getpixel((pos, q) if axis == 0 else (q, pos)) for q in range(4, 16)]
        if sum(samples) / len(samples) < 100:
            positions.append(pos)
    groups = []
    for pos in positions:
        if not groups or pos > groups[-1][-1] + 1:
            groups.append([pos])
        else:
            groups[-1].append(pos)
    result = [round(sum(g) / len(g)) for g in groups]
    if result[0] > 3: result.insert(0, 0)
    if result[-1] < count - 4: result.append(count - 1)
    return result

xs, ys = lines(0), lines(1)
cells = Image.new('L', (len(xs) - 1, len(ys) - 1))
palette = (0, 85, 170, 255)
for y in range(cells.height):
    for x in range(cells.width):
        x0, x1, y0, y1 = xs[x], xs[x + 1], ys[y], ys[y + 1]
        dx, dy = max(2, (x1 - x0) // 4), max(2, (y1 - y0) // 4)
        value = ImageStat.Stat(art.crop((x0 + dx, y0 + dy, x1 - dx, y1 - dy))).mean[0]
        cells.putpixel((x, y), min(palette, key=lambda tone: abs(value - tone)))

icon = cells.resize((30, 30), Image.Resampling.NEAREST)
# Strengthen the exterior silhouette with the fourth (black) hardware tone;
# interior key/shading cells retain dark gray.
outside = set()
pending = [(x, y) for y in range(30) for x in range(30)
           if x in (0, 29) or y in (0, 29)]
while pending:
    x, y = pending.pop()
    if not (0 <= x < 30 and 0 <= y < 30) or (x, y) in outside:
        continue
    if icon.getpixel((x, y)) != 255:
        continue
    outside.add((x, y))
    pending.extend(((x-1, y), (x+1, y), (x, y-1), (x, y+1)))
for y in range(30):
    for x in range(30):
        if icon.getpixel((x, y)) == 85 and any(
            p in outside for p in ((x-1, y), (x+1, y), (x, y-1), (x, y+1))
        ):
            icon.putpixel((x, y), 0)
icon.save(out / 'dictionary-30x30.png')
icon.resize((600, 600), Image.Resampling.NEAREST).save(out / 'dictionary-no-grid.png')
canvas = Image.open(grid).convert('RGB')
assert canvas.size == (601, 601)
draw = ImageDraw.Draw(canvas)
for y in range(30):
    for x in range(30):
        value = icon.getpixel((x, y))
        draw.rectangle((x*20+1, y*20+1, (x+1)*20-1, (y+1)*20-1), fill=(value,)*3)
canvas.save(out / 'dictionary-filled-grid.png')
assert set(icon.getdata()) <= set(palette)
for k in range(31):
    assert all(canvas.getpixel((k*20, y)) == (0, 0, 0) for y in range(601))
    assert all(canvas.getpixel((x, k*20)) == (0, 0, 0) for x in range(601))
print(f'Generated grid {cells.size}; final 30x30 cells; palette={sorted(set(icon.getdata()))}; original grid preserved')

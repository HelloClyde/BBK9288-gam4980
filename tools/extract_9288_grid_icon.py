"""Recover the user-selected pixel art, excluding its graph-paper lines."""
from pathlib import Path
from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parents[1] / 'assets/9288'
PALETTE = (0, 85, 170, 255)

def grid_lines(image, axis):
    count = image.width if axis == 0 else image.height
    groups = []
    for pos in range(count):
        samples = [image.getpixel((pos, q) if axis == 0 else (q, pos))
                   for q in range(4, 16)]
        if sum(samples) / len(samples) >= 100:
            continue
        if not groups or pos > groups[-1][-1] + 1:
            groups.append([pos])
        else:
            groups[-1].append(pos)
    lines = [round(sum(g) / len(g)) for g in groups]
    if not lines:
        raise ValueError('No grid detected')
    if lines[0] > 3: lines.insert(0, 0)
    if lines[-1] < count - 4: lines.append(count - 1)
    if len(lines) != 37:
        raise ValueError(f'Expected selected 36-cell grid; found {len(lines)-1}')
    return lines

def main():
    # RGB -> L also converts the single green button to a hardware gray.
    source = Image.open(ROOT / 'gam4980-icon-selected-grid.png').convert('L')
    xs, ys = grid_lines(source, 0), grid_lines(source, 1)
    cells = Image.new('L', (36, 36))
    for y in range(36):
        for x in range(36):
            x0, x1, y0, y1 = xs[x], xs[x+1], ys[y], ys[y+1]
            dx, dy = (x1-x0)//4, (y1-y0)//4
            # Sample interiors, not the black grid separating adjacent cells.
            value = ImageStat.Stat(source.crop((x0+dx, y0+dy, x1-dx, y1-dy))).mean[0]
            cells.putpixel((x, y), min(PALETTE, key=lambda p: abs(p-value)))
    cells.save(ROOT / 'gam4980-icon-selected-36.png')
    for size in (30, 12):
        icon = cells.resize((size, size), Image.Resampling.NEAREST)
        assert set(icon.tobytes()) == set(PALETTE)
        icon.save(ROOT / f'gam4980-icon-selected-{size}.png')
    print('Selected artwork extracted: 36x36 cells -> 30x30 and 12x12, four grays')

if __name__ == '__main__':
    main()

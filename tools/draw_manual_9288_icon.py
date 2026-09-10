"""Hand-authored 30x30 icon; all coordinates are native hardware pixels."""
from pathlib import Path
from PIL import Image, ImageDraw
from convert_9288_icon import decode_icon, encode_icon, write_preview

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/9288/manual-grid-v4'
OUT.mkdir(parents=True, exist_ok=True)
icon = Image.new('L', (30, 30), 255)
d = ImageDraw.Draw(icon)

# Four hardware tones only. Polygon edges rasterize to whole cells at 1:1;
# there is no antialiasing, resizing, or imported generated image.
BLACK, DARK, LIGHT, WHITE = 0, 85, 170, 255
# Screen case: diagonal upper lid, fully inside a two-pixel margin.
d.polygon([(14,2),(28,9),(24,21),(10,14)],fill=BLACK)
d.polygon([(14,3),(27,9),(23,19),(11,13)],fill=LIGHT)
d.line([(14,3),(26,9)], fill=WHITE)
d.line([(14,4),(11,13)], fill=WHITE)
# Dark screen inset and white LCD interior.
d.polygon([(14,4),(27,10),(24,19),(11,13)],fill=BLACK)
d.polygon([(14,5),(26,10),(23,18),(12,13)],fill=WHITE)
# BBK-like tiny pixel lettering, following the tilted screen baseline.
glyphs = {'B':('110','101','110','101','110'),
          'K':('101','110','100','110','101')}
for letter, x0, y0 in [('B',14,7),('B',18,9),('K',22,11)]:
    for y,row in enumerate(glyphs[letter]):
        for x,value in enumerate(row):
            if value=='1': icon.putpixel((x0+x,y0+y),BLACK)

# Reassert a continuous one-pixel black case outline after the highlights.
d.line([(14,2),(28,9),(24,21),(10,14),(14,2)],fill=BLACK)

# Keyboard lower case; bottom edge gives it thickness.
d.polygon([(10,14),(25,21),(17,27),(15,28),(2,21),(2,19)], fill=BLACK)
d.polygon([(10,15),(24,21),(16,27),(3,20)], fill=LIGHT)
d.polygon([(10,15),(23,21),(16,25),(3,19)], fill=WHITE)
d.line([(3,21),(15,27)], fill=BLACK)
d.line([(4,21),(15,26)], fill=LIGHT)
# Hinge: two dark blocks separated by a light bridge.
d.line([(10,14),(23,20)], fill=DARK)
d.line([(11,14),(13,15)], fill=BLACK)
d.line([(21,19),(23,20)], fill=BLACK)
# Twelve individually positioned 2-pixel keys; never draw fine key outlines.
for x,y in [(9,17),(12,18),(15,20),(18,21),
            (7,18),(10,20),(13,21),(16,23),
            (5,20),(8,21),(11,23),(14,24)]:
    d.line([(x,y),(x+1,y)],fill=BLACK)
# Power button, gray rather than the reference green.
icon.putpixel((21,21),BLACK)

# Optical centering, exactly one native pixel left/up. No interpolation or
# wraparound, and the previous white margins guarantee no clipped artwork.
assert all(icon.getpixel((0,y))==WHITE for y in range(30))
assert all(icon.getpixel((x,0))==WHITE for x in range(30))
centered=Image.new('L',(30,30),WHITE)
centered.paste(icon.crop((1,1,30,30)),(0,0))
icon=centered

assert icon.size==(30,30) and set(icon.tobytes())=={0,85,170,255}
icon.save(OUT/'dictionary-30x30.png')
icon.resize((600,600),Image.Resampling.NEAREST).save(OUT/'dictionary-20x.png')
# Export the literal grid so each pixel can be reviewed/edited as 0/1/2/3.
rows=[''.join(str(icon.getpixel((x,y))//85) for x in range(30)) for y in range(30)]
(OUT/'pixels.txt').write_text('\n'.join(rows)+'\n',encoding='ascii')
grid=Image.new('L',(601,601),BLACK)
g=ImageDraw.Draw(grid)
for y in range(30):
    for x in range(30):
        g.rectangle((x*20+1,y*20+1,(x+1)*20-1,(y+1)*20-1),fill=icon.getpixel((x,y)))
grid.save(OUT/'dictionary-grid.png')
# Preview in the unmodified SDK frame; don't install before user review.
frame=decode_icon(ROOT/'sdk/apmk/ico1.bin',40,40)
for y in range(30):
    for x in range(30): frame[(y+5)*40+x+5]=icon.getpixel((x,y))//85
write_preview(OUT/'framed.png',40,40,frame)
(OUT/'ico1.bin').write_bytes(encode_icon(40,40,frame))
assert all(frame[y*40+x]==decode_icon(ROOT/'sdk/apmk/ico1.bin',40,40)[y*40+x]
           for y in range(40) for x in range(40) if not(5<=x<35 and 5<=y<35))
# Side-by-side, exact-pixel enlargement. Left=previous, right=new.
current=Image.open(OUT/'framed.png').convert('L')
previous_path=ROOT/'assets/9288/manual-grid-v2/framed.png'
previous=Image.open(previous_path).convert('L') if previous_path.exists() else current
current.resize((240,240),Image.Resampling.NEAREST).save(OUT/'framed-6x.png')
comparison=Image.new('L',(560,280),WHITE)
comparison.paste(previous.resize((240,240),Image.Resampling.NEAREST),(20,20))
comparison.paste(current.resize((240,240),Image.Resampling.NEAREST),(300,20))
comparison.save(OUT/'before-after.png')
counts=icon.histogram()
print('Black pixels: %d; exact SDK frame preserved' % counts[0])
print('PASS: 30x30 hand-authored cells; exactly four gray levels; no resampling of design')

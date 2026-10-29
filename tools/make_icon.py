"""Draw the launcher icon (src\\launcher.ico): a worn gunmetal plate with corner rivets and a distressed
blood-red "1" stencilled on it, for solo.

usage: python make_icon.py [preview.png]
"""
import random
import sys
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 1024  # drawn large, scaled down per size
random.seed(5)


def noise(size, lo, hi, blur):
    img = Image.effect_noise((size, size), 64).point(lambda v: lo + (hi - lo) * v // 255)
    return img.filter(ImageFilter.GaussianBlur(blur)) if blur else img


def plate():
    # gunmetal: a dark vertical gradient with grain and blotches
    base = Image.linear_gradient('L').resize((S, S)).point(lambda v: 52 - v * 30 // 255)
    grain = noise(S, -14, 14, 0.6)
    blotch = noise(S // 16, -20, 20, 0).resize((S, S), Image.BICUBIC).filter(ImageFilter.GaussianBlur(24))
    lum = ImageChops.add(ImageChops.add(base, grain, offset=-0), blotch, offset=0)
    lum = Image.eval(ImageChops.add(base, ImageChops.add(grain, blotch, offset=128), offset=-128), lambda v: v)
    metal = Image.merge('RGB', (lum.point(lambda v: v + 2), lum, lum.point(lambda v: max(0, v - 3))))
    d = ImageDraw.Draw(metal, 'RGBA')
    for _ in range(140):  # scratches
        x, y = random.randrange(S), random.randrange(S)
        ln = random.randrange(40, 260)
        dx = random.choice([1, -1]) * random.randrange(ln // 3, ln)
        dy = random.randrange(-ln // 4, ln // 4)
        d.line([(x, y), (x + dx, y + dy)], fill=(200, 200, 195, random.randrange(10, 40)), width=random.choice([1, 2, 3]))
    return metal


def draw():
    m = plate().convert('RGBA')
    d = ImageDraw.Draw(m, 'RGBA')
    # bevel: light top-left edge, dark bottom-right edge
    d.rounded_rectangle([20, 20, S - 20, S - 20], radius=170, outline=(84, 82, 78, 255), width=14)
    d.rounded_rectangle([34, 34, S - 34, S - 34], radius=156, outline=(10, 10, 10, 200), width=10)
    d.rounded_rectangle([64, 64, S - 64, S - 64], radius=126, outline=(0, 0, 0, 90), width=6)
    # rivets
    for cx, cy in ((140, 140), (S - 140, 140), (140, S - 140), (S - 140, S - 140)):
        d.ellipse([cx - 34, cy - 34, cx + 34, cy + 34], fill=(18, 18, 18, 255))
        d.ellipse([cx - 30, cy - 30, cx + 26, cy + 26], fill=(92, 90, 86, 255))
        d.ellipse([cx - 18, cy - 20, cx + 2, cy - 2], fill=(170, 168, 160, 160))
    # the "1": blood red, distressed by knocking speckles out of it
    font = ImageFont.truetype('C:/Windows/Fonts/impact.ttf', 820)
    mask = Image.new('L', (S, S), 0)
    md = ImageDraw.Draw(mask)
    box = md.textbbox((0, 0), '1', font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    md.text((S / 2 - w / 2 - box[0], S / 2 - h / 2 - box[1] + 10), '1', font=font, fill=255)
    wear = noise(S // 4, 0, 255, 0).resize((S, S), Image.BICUBIC).point(lambda v: 0 if v < 70 else 255)
    chips = noise(S // 2, 0, 255, 0).resize((S, S), Image.BICUBIC).point(lambda v: 0 if v < 34 else 255)
    mask = ImageChops.multiply(ImageChops.multiply(mask, wear), chips)
    shadow = mask.filter(ImageFilter.GaussianBlur(14))
    m.paste((0, 0, 0, 200), (14, 18), shadow)
    red = Image.new('RGBA', (S, S))
    rg = Image.linear_gradient('L').resize((S, S))
    red = Image.merge('RGBA', (rg.point(lambda v: 170 - v * 70 // 255), rg.point(lambda v: 12), rg.point(lambda v: 14),
                               Image.new('L', (S, S), 255)))
    m.paste(red, (0, 0), mask)
    # trim to the rounded plate
    clip = Image.new('L', (S, S), 0)
    ImageDraw.Draw(clip).rounded_rectangle([20, 20, S - 20, S - 20], radius=170, fill=255)
    m.putalpha(clip)
    return m


def small(img, s):
    # below 48 px the texture is mush: a flat plate with a clean red "1" reads better
    if s >= 48:
        return img.resize((s, s), Image.LANCZOS)
    t = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(t)
    d.rounded_rectangle([20, 20, S - 20, S - 20], radius=170, fill=(38, 37, 35, 255), outline=(78, 76, 72, 255),
                        width=60)
    font = ImageFont.truetype('C:/Windows/Fonts/impact.ttf', 860)
    box = d.textbbox((0, 0), '1', font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    d.text((S / 2 - w / 2 - box[0], S / 2 - h / 2 - box[1] + 10), '1', font=font, fill=(176, 18, 18, 255))
    return t.resize((s, s), Image.LANCZOS)


img = draw()
sizes = [256, 128, 64, 48, 40, 32, 24, 20, 16]
frames = [small(img, s) for s in sizes]
out = Path(__file__).resolve().parent.parent / 'src' / 'launcher.ico'
frames[0].save(out, sizes=[(s, s) for s in sizes], append_images=frames[1:])
print('wrote', out)
if len(sys.argv) > 1:
    sheet = Image.new('RGBA', (sum(sizes[:6]) + 60 + 80, 256), (32, 32, 32, 255))
    x = 0
    for f, s in zip(frames, sizes):
        if s in (40, 24, 20):
            continue
        sheet.alpha_composite(f, (x, 0))
        x += s + 12
    sheet.save(sys.argv[1])

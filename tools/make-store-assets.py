# Store and Reddit assets for Kasumi, built from real emulator frames (screenshots/reel/*.png,
# captured with a KASUMI_REEL=1 build and `pebble emu-tap`).
#
#   python make-store-assets.py <reel dir> <Jost-Medium.ttf> <ShipporiMinchoB1-ExtraBold.ttf> <out dir>
#
# Writes: store-time2/*.png|gif (200x228), store-round2/*.png|gif (260x260), icon_small_80.png,
# icon_large_144.png, banner_time2.png, banner_round2.png (720x320), reddit/kasumi.gif (+ still).
import os, sys
from PIL import Image, ImageDraw, ImageFont

REEL, JOST, MINCHO, OUT = sys.argv[1:5]
os.makedirs(OUT, exist_ok=True)

PRUSSIAN = (0, 85, 170); OXFORD = (0, 0, 85); PICTON = (85, 170, 255); CELESTE = (170, 255, 255)
WHITE = (255, 255, 255); PASTEL = (255, 255, 170); RED = (255, 0, 0); WINDSOR = (170, 85, 0)
BULGAR = (85, 0, 0); MIDGREEN = (0, 85, 85); INK = (12, 14, 22)

def frame(platform, i):
    return Image.open(f'{REEL}/{platform}-{i:02d}.png').convert('RGB')

# reel order (see src/c/demo.h REEL[]): 0 dawn, 1 morning, 2-6 wind, 7 hot noon, 8 dusk,
# 9-12 snow at blue hour, 13 night, 14-18 rain, 19-27 storm, 28 forecast views, 29 forecast seals
STORM_FLASH = {20, 21, 22, 25, 26}
def duration(i):
    if i in (0, 1, 7, 8, 13): return 1400
    if 2 <= i <= 6: return 200
    if 9 <= i <= 12: return 240
    if 14 <= i <= 18: return 160
    if i in STORM_FLASH: return 90
    return 170
SEQ = list(range(0, 28))

# ---------------------------------------------------------------- store screenshots
def store(platform, folder):
    d = f'{OUT}/{folder}'
    os.makedirs(d, exist_ok=True)
    for name, i in [('1_red_fuji_dawn', 0), ('2_five_day_views', 28), ('3_snow_blue_hour', 10), ('4_night', 13)]:
        frame(platform, i).save(f'{d}/{name}.png')
    storm = [frame(platform, i) for i in range(14, 28)]
    storm[0].save(f'{d}/5_rain_and_lightning.gif', save_all=True, append_images=storm[1:],
                  duration=[duration(i) for i in range(14, 28)], loop=0, optimize=False)

store('emery', 'store-time2')
store('gabbro', 'store-round2')

# ---------------------------------------------------------------- icons: a small print of Fuji
def icon(size):
    k = 8
    S = size * k
    im = Image.new('RGB', (S, S), WHITE)
    d = ImageDraw.Draw(im)
    bands = [(0.16, PRUSSIAN), (0.32, PICTON), (0.50, CELESTE), (1.0, WHITE)]
    y0 = 0
    for end, c in bands:
        d.rectangle([0, y0, S, int(S * end)], fill=c)
        y0 = int(S * end)
    # sun
    r = S * 0.09
    d.ellipse([S * 0.18 - r, S * 0.38 - r, S * 0.18 + r, S * 0.38 + r], fill=RED)
    # Fuji: concave flanks, small flat summit
    cx, top, base, hb, sw = S * 0.56, S * 0.30, S * 0.78, S * 0.62, S * 0.05
    pts = []
    for i in range(0, 201):
        x = cx - hb + 2 * hb * i / 200
        dd = abs(x - cx)
        if dd <= sw: y = top
        else:
            u = min(1, (dd - sw) / (hb - sw)); y = base - (base - top) * (1 - u) ** 2
        pts.append((x, y))
    d.polygon(pts + [(cx + hb, base), (cx - hb, base)], fill=PRUSSIAN)
    # snow cap with three fingers
    snow = [p for p in pts if p[1] < top + (base - top) * 0.30]
    sl, sr = snow[0][0], snow[-1][0]
    fingers = [(sl, top + (base - top) * 0.30)]
    n = 6
    for j in range(n + 1):
        x = sl + (sr - sl) * j / n
        y = top + (base - top) * (0.30 + (0.14 if j % 2 else 0))
        fingers.append((x, y))
    d.polygon(snow + fingers[::-1], fill=WHITE)
    d.line(pts, fill=OXFORD, width=max(k, int(S * 0.012)))
    # water
    d.rectangle([0, base, S, S], fill=PRUSSIAN)
    for j in range(2):
        yy = base + (S - base) * (0.35 + 0.35 * j)
        d.line([(S * 0.1, yy), (S * 0.9, yy)], fill=WHITE, width=max(k, int(S * 0.02)))
    return im.resize((size, size), Image.LANCZOS)

icon(80).save(f'{OUT}/icon_small_80.png')
icon(144).save(f'{OUT}/icon_large_144.png')

# ---------------------------------------------------------------- device mock-ups (drawn, no photos)
def time2_device(face, scale):
    fw, fh = face.width * scale, face.height * scale
    pad_x, pad_y = int(18 * scale), int(30 * scale)
    W, H = fw + 2 * pad_x, fh + 2 * pad_y
    k = 4
    body = Image.new('RGBA', (W * k, H * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    d.rounded_rectangle([k * 4, 0, W * k - k * 4, H * k], radius=int(26 * scale * k), fill=(24, 25, 28))
    d.rounded_rectangle([k * 4 + 6 * k, 6 * k, W * k - k * 4 - 6 * k, H * k - 6 * k], radius=int(22 * scale * k), fill=(10, 10, 12))
    bw = int(5 * scale * k)
    d.rounded_rectangle([0, int(H * 0.30 * k), bw, int(H * 0.46 * k)], radius=bw // 2, fill=(40, 41, 45))
    for yy in (0.24, 0.44, 0.64):
        d.rounded_rectangle([W * k - bw, int(H * yy * k), W * k, int(H * (yy + 0.12) * k)], radius=bw // 2, fill=(40, 41, 45))
    body = body.resize((W, H), Image.LANCZOS)
    body.paste(face.resize((fw, fh), Image.NEAREST), (pad_x, pad_y))
    return body

def round2_device(face, scale):
    fw = face.width * scale
    pad = int(20 * scale)
    W = fw + 2 * pad
    k = 4
    body = Image.new('RGBA', (W * k, W * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    d.ellipse([k * 6, k * 6, W * k - k * 6, W * k - k * 6], fill=(24, 25, 28))
    d.ellipse([k * 12, k * 12, W * k - k * 12, W * k - k * 12], fill=(10, 10, 12))
    bw = int(5 * scale * k)
    d.rounded_rectangle([0, int(W * 0.42 * k), bw + k * 6, int(W * 0.58 * k)], radius=bw // 2, fill=(40, 41, 45))
    for yy in (0.26, 0.44, 0.62):
        d.rounded_rectangle([W * k - bw - k * 6, int(W * yy * k), W * k, int(W * (yy + 0.11) * k)], radius=bw // 2, fill=(40, 41, 45))
    body = body.resize((W, W), Image.LANCZOS)
    f = face.resize((fw, fw), Image.NEAREST)
    m = Image.new('L', (fw * k, fw * k), 0)
    ImageDraw.Draw(m).ellipse([0, 0, fw * k - 1, fw * k - 1], fill=255)
    body.paste(f, (pad, pad), m.resize((fw, fw), Image.LANCZOS))
    return body

def backdrop(W, H):
    im = Image.new('RGB', (W, H), INK)
    d = ImageDraw.Draw(im)
    # bokashi: a Prussian band fading down, in bands like the face's sky
    for i, c in enumerate([(0, 40, 90), (0, 30, 70), (0, 22, 52), (6, 16, 36)]):
        d.rectangle([0, int(H * 0.10 * i), W, int(H * 0.10 * (i + 1))], fill=c)
    return im

def font(path, size): return ImageFont.truetype(path, size)

# ---------------------------------------------------------------- banners 720x320
def banner(device, platform):
    b = backdrop(720, 320)
    dev = device.copy()
    h = 290
    dev = dev.resize((int(dev.width * h / dev.height), h), Image.LANCZOS)
    b.paste(dev, (40, 15), dev)
    d = ImageDraw.Draw(b)
    x = 40 + dev.width + 46
    d.text((x, 62), '霞', font=font(MINCHO, 64), fill=(216, 70, 28))
    d.text((x + 82, 70), 'KASUMI', font=font(JOST, 56), fill=WHITE)
    d.text((x + 2, 150), "Weather, painted after Hokusai's", font=font(JOST, 22), fill=(190, 205, 230))
    d.text((x + 2, 180), 'Thirty-six Views of Mount Fuji', font=font(JOST, 22), fill=(190, 205, 230))
    size = 20
    while d.textlength(platform, font=font(JOST, size)) > 720 - 20 - x and size > 12:
        size -= 1
    d.text((x + 2, 238), platform, font=font(JOST, size), fill=WHITE)
    return b

banner(time2_device(frame('emery', 0), 1), 'WATCHFACE FOR PEBBLE TIME 2').save(f'{OUT}/banner_time2.png')
banner(round2_device(frame('gabbro', 8), 1), 'WATCHFACE FOR PEBBLE ROUND 2').save(f'{OUT}/banner_round2.png')

# ---------------------------------------------------------------- Reddit: both watches, the same reel
os.makedirs(f'{OUT}/reddit', exist_ok=True)
def reddit_frame(i):
    W, H = 1280, 720
    im = backdrop(W, H)
    t2 = time2_device(frame('emery', i), 2)
    r2 = round2_device(frame('gabbro', i), 2)
    gap = 70
    total = t2.width + gap + r2.width
    x0 = (W - total) // 2
    im.paste(t2, (x0, (H - t2.height) // 2 - 14), t2)
    im.paste(r2, (x0 + t2.width + gap, (H - r2.height) // 2 - 14), r2)
    d = ImageDraw.Draw(im)
    cap = 'KASUMI  ·  Pebble Time 2  &  Round 2'
    f = font(JOST, 22)
    tw = d.textlength(cap, font=f)
    d.text(((W - tw) / 2, H - 40), cap, font=f, fill=(170, 185, 210))
    return im

frames = [reddit_frame(i) for i in SEQ]
pal = [f.convert('P', palette=Image.ADAPTIVE, colors=128) for f in frames]
pal[0].save(f'{OUT}/reddit/kasumi.gif', save_all=True, append_images=pal[1:],
            duration=[duration(i) for i in SEQ], loop=0, optimize=True, disposal=1)
reddit_frame(0).save(f'{OUT}/reddit/kasumi_still.png')
print('done')

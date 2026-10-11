#!/usr/bin/env python3
"""
make_dragon.py - the five-headed AMPSURD dragon as background layers for the UI.

Geometry: the owner's approved sketch, version 3 (project doc claude/dragon_sketch.py, thick line 2x),
placed on the current 1200 x 800 layout. Output (plugin/assets/dragon/, 2x = 2400 x 1600, white line art
with alpha, tinted and faded by the UI):
  dragon_body.png                   wings, tail, torso, legs, necks (heads cut out, so they cover the necks)
  dragon_head{1..5}_asleep.png      closed eye (head 5: smoke)
  dragon_head{1..5}_awake.png       open eye
  dragon_eyes.txt                   eye centre x, y, radius, angle (degrees) per head, 1200 x 800 units
Usage: python3 make_dragon.py <outDir> [ui_snapshot.png -> writes preview_*.png next to the layers]
"""
import math, os, random, sys
from PIL import Image, ImageDraw

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

S = 4                     # supersampling
W, H = 1200, 800          # UI layout units
OUTSCALE = 2              # PNG size = 2x layout
DX, DY = float(os.environ.get("DRAGON_DX", "10")), float(os.environ.get("DRAGON_DY", "30"))
LW = float(os.environ.get("LW", "2"))

canvas = None
d = None
DRAW_LINES = True
DRAW_FILL = True

def new_canvas():
    global canvas, d
    canvas = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(canvas)

# ---------------------------------------------------------------- helpers (as in the sketch)
def bez(p0, p1, p2, p3, n=50):
    out = []
    for i in range(n + 1):
        t = i / n; u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out

def chain(*segs, n=50):
    pts = []
    for s in segs:
        p = bez(*s, n=n)
        pts += p if not pts else p[1:]
    return pts

def sc(pts): return [((x + DX) * S, (y + DY) * S) for x, y in pts]

def line(pts, w=1.5, v=255, sketch=True):
    if not DRAW_LINES or len(pts) < 2: return
    w = w * LW
    if sketch:  # faint offset pass underneath = hand-drawn feel
        dx, dy = random.uniform(-0.9, 0.9) * LW, random.uniform(-0.9, 0.9) * LW
        d.line(sc([(x + dx, y + dy) for x, y in pts]), fill=int(v * 0.45), width=max(1, round(w * 0.55 * S)), joint="curve")
    d.line(sc(pts), fill=v, width=max(1, round(w * S)), joint="curve")

def fill(poly):  # occlude what is behind
    if DRAW_FILL: d.polygon(sc(poly), fill=0)

def normals(pts):
    out = []
    for i in range(len(pts)):
        a = pts[max(0, i - 1)]; b = pts[min(len(pts) - 1, i + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]; n = math.hypot(tx, ty) or 1
        out.append((-ty / n, tx / n, tx / n, ty / n))
    return out

def tube(pts, w0, w1, v=255, lw=1.5, cap=True, occlude=True):
    nm = normals(pts); L = []; R = []; ws = []
    for i, ((x, y), (nx, ny, _, _)) in enumerate(zip(pts, nm)):
        w = (w0 + (w1 - w0) * i / (len(pts) - 1)) / 2
        ws.append(w)
        L.append((x + nx * w, y + ny * w)); R.append((x - nx * w, y - ny * w))
    if occlude: fill(L + R[::-1])
    line(L, lw, v); line(R, lw, v)
    if cap and w1 > 3: line([L[-1], R[-1]], lw * 0.8, v, sketch=False)
    return L, R, ws, nm

def spikes(pts, ws, nm, every=6, size=8, side=1, v=255, start=3, stop=None):
    stop = stop or len(pts) - 3
    for i in range(start, stop, every):
        (x, y), w, (nx, ny, tx, ty) = pts[i], ws[i], nm[i]
        if isinstance(side, tuple):
            if (x + nx - side[0])**2 + (y + ny - side[1])**2 < (x - nx - side[0])**2 + (y - ny - side[1])**2: nx, ny = -nx, -ny
        elif (ny > 0) == (side > 0): nx, ny = -nx, -ny
        bx, by = x + nx * w, y + ny * w
        s = size * (0.6 + 0.4 * (1 - i / len(pts)))
        line([(bx - tx * s * 0.5, by - ty * s * 0.5), (bx + nx * s - tx * s * 0.35, by + ny * s - ty * s * 0.35), (bx + tx * s * 0.5, by + ty * s * 0.5)],
             1.0, v, sketch=False)

def bands(pts, ws, nm, every=6, v=150, side=-1, start=4, stop=None):
    stop = stop or len(pts) - 3
    for i in range(start, stop, every):
        (x, y), w, (nx, ny, tx, ty) = pts[i], ws[i], nm[i]
        if isinstance(side, tuple):
            if (x + nx - side[0])**2 + (y + ny - side[1])**2 > (x - nx - side[0])**2 + (y - ny - side[1])**2: nx, ny = -nx, -ny
        elif (ny > 0) == (side > 0): nx, ny = -nx, -ny
        a = (x - nx * w * 0.1, y - ny * w * 0.1); b = (x + nx * w * 0.95, y + ny * w * 0.95)
        mid = ((a[0] + b[0]) / 2 + tx * w * 0.35, (a[1] + b[1]) / 2 + ty * w * 0.35)
        line(bez(a, mid, mid, b, 10), 0.8, v, sketch=False)

# ---------------------------------------------------------------- body parts
CX = 590
WV = 150
BV = 210

def wing(sx):
    X = lambda dx: CX + sx * dx
    root, elbow, wrist = (X(95), 548), (X(205), 395), (X(340), 238)
    a1, a2 = chain((root, (X(120), 470), (X(160), 420), elbow)), chain((elbow, (X(250), 330), (X(290), 270), wrist))
    tube(a1, 13, 9, v=WV, lw=1.4, cap=False, occlude=False)
    tube(a2, 9, 6, v=WV, lw=1.4, cap=False, occlude=False)
    line([(elbow[0] - sx * 6, elbow[1] + 4), (elbow[0] + sx * 2, elbow[1] - 6)], 1.2, WV, sketch=False)
    line(bez(wrist, (X(330), 222), (X(316), 214), (X(306), 222), 12), 1.3, WV)
    tips = [(X(565), 212), (X(585), 375), (X(515), 540), (X(375), 622)]
    for k, t in enumerate(tips):
        bend = 18 - k * 4
        mid1 = (wrist[0] + (t[0] - wrist[0]) * 0.35, wrist[1] + (t[1] - wrist[1]) * 0.35 - bend)
        mid2 = (wrist[0] + (t[0] - wrist[0]) * 0.75, wrist[1] + (t[1] - wrist[1]) * 0.75 - bend * 0.5)
        line(bez(wrist, mid1, mid2, t, 40), 1.25, WV)
        line(bez(t, (t[0] + sx * 6, t[1] - 4), (t[0] + sx * 9, t[1] + 2), (t[0] + sx * 7, t[1] + 8), 8), 1.0, WV, sketch=False)
    edge = tips + [(X(150), 650)]
    for a, b in zip(edge, edge[1:]):
        m = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        pull = (m[0] + (wrist[0] - m[0]) * 0.22, m[1] + (wrist[1] - m[1]) * 0.22)
        line(bez(a, pull, pull, b, 30), 1.3, WV)
    for k in range(3):
        a, b = tips[k], tips[k + 1]
        for f in (0.45, 0.62):
            p = (wrist[0] + ((a[0] + b[0]) / 2 - wrist[0]) * f, wrist[1] + ((a[1] + b[1]) / 2 - wrist[1]) * f)
            line(bez((p[0] - sx * 10, p[1] - 6), (p[0], p[1] + 4), (p[0], p[1] + 4), (p[0] + sx * 14, p[1] + 2), 10), 0.7, 90, sketch=False)

def tail_part():
    tail = chain(((452, 728), (360, 768), (250, 752), (178, 712)),
                 ((178, 712), (118, 680), (70, 640), (86, 596)),
                 ((86, 596), (98, 566), (138, 562), (146, 590)), n=60)
    tL, tR, tW, tN = tube(tail, 34, 7, v=BV, lw=1.4, cap=False)
    spikes(tail, tW, tN, every=9, size=11, side=(250, 650), v=BV, start=6, stop=len(tail) - 12)
    bands(tail, tW, tN, every=8, v=120, side=(250, 650), start=6, stop=len(tail) - 14)
    ex, ey = tail[-1]; _, _, tx, ty = tN[-1]; nx, ny = -ty, tx
    sp = [(ex - nx * 11, ey - ny * 11), (ex + tx * 24, ey + ty * 24), (ex + nx * 11, ey + ny * 11)]
    fill(sp); line(sp + [sp[0]], 1.4, BV, sketch=False)
    line([(ex, ey), (ex + tx * 16, ey + ty * 16)], 0.8, 140, sketch=False)

def torso_part():
    torso = chain(((470, 532), (436, 548), (418, 590), (420, 640)),
                  ((420, 640), (422, 700), (450, 745), (500, 770)),
                  ((500, 770), (560, 790), (620, 790), (680, 770)),
                  ((680, 770), (730, 745), (758, 700), (760, 640)),
                  ((760, 640), (762, 590), (744, 548), (710, 532)))
    fill(torso + [(590, 520)])
    line(torso, 1.8, BV)
    for k in range(10):
        y = 560 + k * 20
        half = 70 - k * 3.5
        line(bez((CX - half, y), (CX - half * 0.4, y + 9), (CX + half * 0.4, y + 9), (CX + half, y), 20), 1.0, 160, sketch=False)
    line(chain(((CX - 70, 556), (CX - 60, 640), (CX - 45, 720), (CX - 36, 760))), 1.1, 170)
    line(chain(((CX + 70, 556), (CX + 60, 640), (CX + 45, 720), (CX + 36, 760))), 1.1, 170)
    for row in range(3):
        for k in range(4):
            for sx in (-1, 1):
                x = CX + sx * (88 + k * 11 + row * 4); y = 572 + row * 13 + k * 3
                line(bez((x - 5, y), (x - 2, y + 5), (x + 2, y + 5), (x + 5, y), 8), 0.8, 130, sketch=False)

def leg(sx):
    X = lambda dx: CX + sx * dx
    up = chain(((X(112), 612), (X(150), 622), (X(166), 660), (X(160), 700)))
    lo = chain(((X(160), 700), (X(156), 720), (X(146), 734), (X(134), 746)))
    tube(up, 30, 22, v=BV, lw=1.5, cap=False)
    tube(lo, 22, 16, v=BV, lw=1.5, cap=False)
    for k in range(4):
        bx = X(118 + k * 8); by = 748
        line(bez((bx, by), (bx - sx * 2, by + 6), (bx - sx * 6, by + 10), (bx - sx * 12, by + 10), 10), 1.3, BV)
    line([(X(172), 676), (X(186), 684), (X(170), 690)], 1.1, BV, sketch=False)

# ---------------------------------------------------------------- heads
HK = 1.6
def P(pts, J, ang, flip):
    a = math.radians(ang); c, s = math.cos(a), math.sin(a)
    out = []
    for x, y in pts:
        x, y = x * HK, y * HK
        if flip: y = -y
        out.append((J[0] + x * c - y * s, J[1] + x * s + y * c))
    return out

eye_info = {}

def head(idx, J, ang, awake, mouth_open, smoke=False):
    flip = math.cos(math.radians(ang)) < -0.01
    pl = lambda pts: P(pts, J, ang, flip)
    o = 11 if mouth_open else 2
    for base, c1, c2, tip, w0 in (((18, -20), (4, -40), (-18, -50), (-46, -54), 9),
                                  ((30, -24), (24, -44), (12, -56), (-8, -66), 6)):
        h = pl(bez(base, c1, c2, tip, 30))
        hl, hr, hw, hn = tube(h, w0 * HK, 1.2, lw=1.3, cap=False)
        for i in range(4, len(h) - 6, 5):
            (x, y), w, (nx, ny, _, _) = h[i], hw[i], hn[i]
            line([(x + nx * w, y + ny * w), (x - nx * w, y - ny * w)], 0.7, 170, sketch=False)
    upper = chain(((0, -16), (12, -24), (30, -28), (42, -28)),
                  ((42, -28), (50, -28), (54, -22), (62, -19)),
                  ((62, -19), (76, -16), (88, -16), (94, -13)),
                  ((94, -13), (100, -11), (103, -6), (100, -2)),
                  ((100, -2), (86, -1), (66, 0), (52, 2)), n=20)
    lower = chain(((52, 2), (68, 4 + o * 0.5), (84, 4 + o), (94, 6 + o)),
                  ((94, 6 + o), (95, 10 + o), (90, 13 + o), (84, 13 + o)),
                  ((84, 13 + o), (66, 16 + o * 0.6), (40, 22), (20, 22)),
                  ((20, 22), (10, 22), (4, 20), (0, 18)), n=20)
    fill(pl(upper + lower))
    line(pl(upper), 1.6); line(pl(lower), 1.6)
    t = []
    for i in range(7):
        x = 58 + i * 5.5
        t += [(x, 0), (x + 2.2, 4.5 - (i == 1) * -2), (x + 4.5, -0.5)]
    line(pl(t), 0.9, 230, sketch=False)
    if mouth_open:
        t = []
        for i in range(6):
            x = 60 + i * 5.5
            t += [(x, 6 + o * 0.55 + i * 0.08 * o), (x + 2.2, 1.5 + o * 0.55 + i * 0.08 * o), (x + 4.5, 6.5 + o * 0.55 + i * 0.08 * o)]
        line(pl(t), 0.9, 230, sketch=False)
        line(pl(bez((54, 5), (70, 6 + o * 0.5), (78, 5), (86, 8 + o * 0.4), 12)), 0.8, 150, sketch=False)
    line(pl(bez((32, -27), (40, -31), (50, -30), (58, -24), 12)), 1.2)
    line(pl([(36, -28), (30, -36), (42, -30)]), 1.0, sketch=False)
    line(pl(bez((90, -12), (93, -15), (97, -14), (98, -10), 10)), 1.0, sketch=False)
    for a, b in (((8, 4), (-14, 6)), ((10, 12), (-10, 22)), ((14, 19), (-2, 32))):
        line(pl([a, b, (a[0] + 6, a[1] + 3)]), 1.1, sketch=False)
    for k in range(3):
        line(pl([(26 + k * 12, 21), (24 + k * 12, 28), (32 + k * 12, 20)]), 0.9, sketch=False)
    for r in range(2):
        for k in range(4):
            x, y = 14 + k * 8 + r * 4, -8 + r * 8
            line(pl(bez((x - 3, y), (x - 1, y + 3), (x + 1, y + 3), (x + 3, y), 6)), 0.7, 150, sketch=False)
    line(pl(bez((50, 2), (46, -4), (36, -6), (24, -4), 12)), 0.8, 150, sketch=False)
    # eye
    ex, ey = pl([(46, -19)])[0]
    r = 4.6
    eye_info[idx] = (ex + DX, ey + DY, r, ang)
    if awake:
        if DRAW_LINES:
            d.ellipse([(ex + DX - r) * S, (ey + DY - r * 0.7) * S, (ex + DX + r) * S, (ey + DY + r * 0.7) * S], fill=255)
            d.line([((ex + DX) * S, (ey + DY - r * 0.6) * S), ((ex + DX) * S, (ey + DY + r * 0.6) * S)], fill=0, width=S)
        line(pl(bez((40, -19), (44, -23), (49, -23), (53, -19), 10)), 1.0, sketch=False)
    else:
        line(pl(bez((40, -18), (44, -15), (49, -15), (53, -18), 10)), 1.3, sketch=False)
        line(pl([(42, -15.5), (41, -13)]), 0.7, sketch=False); line(pl([(46, -14.7), (46, -12)]), 0.7, sketch=False)
    if smoke:
        mx, my = pl([(102, -4)])[0]
        line(bez((mx, my), (mx + 16, my - 8), (mx + 6, my - 26), (mx + 22, my - 40), 30), 1.0, 200)
        line(bez((mx + 22, my - 40), (mx + 36, my - 50), (mx + 26, my - 64), (mx + 40, my - 76), 30), 0.9, 170)
        line(bez((mx + 4, my + 2), (mx + 20, my + 4), (mx + 22, my - 10), (mx + 36, my - 14), 20), 0.8, 150)

# head centre, angle, mouth open, smoke while asleep (head 5)
HEADS = [((124, 92), 200, True, False),
         ((360, 140), -150, False, False),
         ((600, 135), -80, True, False),
         ((830, 150), -16, False, False),
         ((1045, 175), 32, True, True)]
SHOULDERS = [(486, 540), (538, 528), (590, 524), (642, 528), (694, 540)]

def joint(i):
    (cx, cy), ang, _, _ = HEADS[i]
    a = math.radians(ang)
    return (cx - 72 * math.cos(a), cy - 72 * math.sin(a)), ang

def save(name):
    img = canvas.resize((W * OUTSCALE, H * OUTSCALE), Image.LANCZOS)
    rgba = Image.new("RGBA", img.size, (255, 255, 255, 0))
    rgba.putalpha(img)
    rgba.save(os.path.join(OUT, name), optimize=True)
    return img

# ---------------------------------------------------------------- 1. body layer (heads cut out)
random.seed(11)
new_canvas()
wing(-1); wing(1)
tail_part(); torso_part(); leg(-1); leg(1)
for i in [2, 1, 3, 0, 4]:
    J, ang = joint(i)
    a = math.radians(ang)
    Sx, Sy = SHOULDERS[i]
    back = (J[0] - 150 * math.cos(a), J[1] - 150 * math.sin(a))
    neck = bez((Sx, Sy), (Sx + (J[0] - Sx) * 0.12, Sy - 180), back, J, 90)
    nL, nR, nW, nN = tube(neck, 40, 28, lw=1.5, cap=False)
    spikes(neck, nW, nN, every=6, size=11, side=1, start=4, stop=len(neck) - 4)
    bands(neck, nW, nN, every=5, v=130, side=-1, start=3, stop=len(neck) - 3)
DRAW_LINES = False                       # heads only erase what is behind them
for i in range(5):
    J, ang = joint(i)
    head(i, J, ang, False, HEADS[i][2])
DRAW_LINES = True
body = save("dragon_body.png")

# ---------------------------------------------------------------- 2. heads (each alone, two states)
heads = {}
for i in range(5):
    J, ang = joint(i)
    for state in ("asleep", "awake"):
        random.seed(100 + i)
        new_canvas()
        head(i, J, ang, state == "awake", HEADS[i][2], smoke=(HEADS[i][3] and state == "asleep"))
        heads[(i, state)] = save(f"dragon_head{i + 1}_{state}.png")

with open(os.path.join(OUT, "dragon_eyes.txt"), "w") as f:
    f.write("# head: eye centre x y, radius, angle (1200 x 800 layout units)\n")
    for i in range(5):
        ex, ey, r, ang = eye_info[i]
        f.write(f"{i + 1} {ex:.2f} {ey:.2f} {r:.2f} {ang:.1f}\n")

# ---------------------------------------------------------------- 3. previews on a UI snapshot
# the UI covers the lower panel, the master row and the footer with an 80 % fill: the body is only hinted there
DIM_RECTS = [(24, 380, 1176, 682), (24, 696, 1176, 748), (0, 760, 1200, 800)]
def dim_mask():
    m = Image.new("L", (W * OUTSCALE, H * OUTSCALE), 255)
    md = ImageDraw.Draw(m)
    for x0, y0, x1, y1 in DIM_RECTS:
        md.rectangle([x0 * OUTSCALE, y0 * OUTSCALE, x1 * OUTSCALE, y1 * OUTSCALE], fill=51)
    return m

SNAPS = sys.argv[2:]
for SNAP in SNAPS:
    base = Image.open(SNAP).convert("RGB").resize((W * OUTSCALE, H * OUTSCALE))
    mask = dim_mask()
    def compose(states, strength=0.11, red=()):
        out = base.copy()
        grey = Image.new("RGB", out.size, (215, 215, 215))
        layers = [body] + [heads[(i, states[i])] for i in range(5)]
        for L in layers:
            a = Image.composite(L, Image.new("L", L.size, 0), Image.new("L", L.size, 255))
            a = Image.eval(Image.merge("L", [Image.frombytes("L", L.size, bytes(x * y // 255 for x, y in zip(L.tobytes(), mask.tobytes())))]), lambda v: v)
            out = Image.composite(grey, out, a.point(lambda v: int(v * strength)))
        dr = ImageDraw.Draw(out)
        for i in red:
            ex, ey, r, ang = eye_info[i]
            k = OUTSCALE
            dr.ellipse([(ex - r) * k, (ey - r * 0.7) * k, (ex + r) * k, (ey + r * 0.7) * k], fill=(210, 60, 66))
        return out
    name = os.path.splitext(os.path.basename(SNAP))[0]
    st = os.environ.get("DRAGON_STATES", "awake,awake,awake,asleep,asleep").split(",")
    red = tuple(int(k) - 1 for k in os.environ.get("DRAGON_RED", "1").split(",") if k)
    compose(st, red=red).save(os.path.join(OUT, f"preview_{name}.png"))
print("done", OUT)

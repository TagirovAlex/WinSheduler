#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WinSheduler: генератор логотипа, картинок для WiX и набора иконок.
Запуск:  python make_assets.py      (нужны Pillow, numpy, matplotlib)
Название и слоган меняются в блоке НАСТРОЙКИ ниже.
"""
import os, math, struct, shutil, io
import numpy as np
from PIL import Image, ImageDraw, ImageChops, ImageFont
from matplotlib.textpath import TextPath, text_to_path
from matplotlib.font_manager import FontProperties, findfont

# ───────────── НАСТРОЙКИ ─────────────
NAME_A, NAME_B = "Win", "Sheduler"          # две части названия (двухцветный логотип)
TAGLINE = "Планировщик заданий для Windows"
OUT = os.path.dirname(os.path.abspath(__file__))

# палитра
BLUE, BLUE_D, BLUE_L = "#2F6FDE", "#1B4FB5", "#5AA2FF"
GREEN, RED, AMBER = "#2FA84F", "#D8453F", "#F2A000"
GRAY, GRAY_L, DARK, WHITE = "#5F6B7A", "#C9D1DB", "#27313F", "#FFFFFF"
NAVY = "#0F1E45"

FONT_BOLD = findfont(FontProperties(family="DejaVu Sans", weight="bold"))
FONT_REG = findfont(FontProperties(family="DejaVu Sans"))

# ───────────── векторный мини-DSL ─────────────
def R(x, y, w, h, r=0, fill=None, stroke=None, sw=0, op=1):
    return dict(t="rect", x=x, y=y, w=w, h=h, r=r, fill=fill, stroke=stroke, sw=sw, op=op)
def C(cx, cy, r, fill=None, stroke=None, sw=0, op=1):
    return dict(t="circle", cx=cx, cy=cy, r=r, fill=fill, stroke=stroke, sw=sw, op=op)
def P(pts, fill=None, stroke=None, sw=0, close=True, op=1):
    return dict(t="poly", pts=list(pts), fill=fill, stroke=stroke, sw=sw, close=close, op=op)
def L(pts, stroke, sw, op=1):
    return P(pts, None, stroke, sw, close=False, op=op)

def T(text, x, y, size, fill, bold=True, op=1):
    fp = FontProperties(fname=FONT_BOLD if bold else FONT_REG)
    tp = TextPath((0, 0), text, size=size, prop=fp)
    verts = [(vx + x, y - vy) for vx, vy in tp.vertices]
    return dict(t="text", verts=verts, codes=[int(c) for c in tp.codes],
                fill=fill, stroke=None, sw=0, op=op)

def adv(text, size, bold=True):
    fp = FontProperties(fname=FONT_BOLD if bold else FONT_REG, size=size)
    w, _, _ = text_to_path.get_text_width_height_descent(text, fp, ismath=False)
    return w

def xf(shapes, s=1.0, dx=0.0, dy=0.0):
    out = []
    for sh in shapes:
        n = dict(sh); t = sh["t"]
        if t == "rect":
            n.update(x=sh["x"]*s+dx, y=sh["y"]*s+dy, w=sh["w"]*s, h=sh["h"]*s, r=sh["r"]*s)
        elif t == "circle":
            n.update(cx=sh["cx"]*s+dx, cy=sh["cy"]*s+dy, r=sh["r"]*s)
        elif t == "poly":
            n["pts"] = [(px*s+dx, py*s+dy) for px, py in sh["pts"]]
        elif t == "text":
            n["verts"] = [(px*s+dx, py*s+dy) for px, py in sh["verts"]]
        n["sw"] = sh["sw"]*s
        out.append(n)
    return out

# ───────────── SVG ─────────────
def fmt(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")

def svg_of(shapes, vw, vh, w=None, h=None):
    defs, body = [], []
    def paint(v):
        if isinstance(v, tuple):
            gid = f"g{len(defs)}"
            defs.append(f'<linearGradient id="{gid}" x1="0" y1="0" x2="1" y2="1">'
                        f'<stop offset="0" stop-color="{v[1]}"/><stop offset="1" stop-color="{v[2]}"/></linearGradient>')
            return f"url(#{gid})"
        return v
    for sh in shapes:
        a = []
        if sh["fill"]:
            a.append(f'fill="{paint(sh["fill"])}"')
            if sh["op"] < 1: a.append(f'fill-opacity="{sh["op"]}"')
        else:
            a.append('fill="none"')
        if sh["stroke"] and sh["sw"] > 0:
            a.append(f'stroke="{sh["stroke"]}" stroke-width="{fmt(sh["sw"])}" '
                     f'stroke-linecap="round" stroke-linejoin="round"')
            if sh["op"] < 1: a.append(f'stroke-opacity="{sh["op"]}"')
        a = " ".join(a); t = sh["t"]
        if t == "rect":
            rx = f' rx="{fmt(sh["r"])}"' if sh["r"] else ""
            body.append(f'<rect x="{fmt(sh["x"])}" y="{fmt(sh["y"])}" width="{fmt(sh["w"])}" height="{fmt(sh["h"])}"{rx} {a}/>')
        elif t == "circle":
            body.append(f'<circle cx="{fmt(sh["cx"])}" cy="{fmt(sh["cy"])}" r="{fmt(sh["r"])}" {a}/>')
        elif t == "poly":
            pts = " ".join(f"{fmt(x)},{fmt(y)}" for x, y in sh["pts"])
            tag = "polygon" if sh["close"] else "polyline"
            body.append(f'<{tag} points="{pts}" {a}/>')
        elif t == "text":
            v, c = sh["verts"], sh["codes"]; d = []; i = 0
            while i < len(c):
                if c[i] == 1:   d.append(f"M{fmt(v[i][0])} {fmt(v[i][1])}"); i += 1
                elif c[i] == 2: d.append(f"L{fmt(v[i][0])} {fmt(v[i][1])}"); i += 1
                elif c[i] == 3: d.append(f"Q{fmt(v[i][0])} {fmt(v[i][1])} {fmt(v[i+1][0])} {fmt(v[i+1][1])}"); i += 2
                elif c[i] == 4: d.append(f"C{fmt(v[i][0])} {fmt(v[i][1])} {fmt(v[i+1][0])} {fmt(v[i+1][1])} {fmt(v[i+2][0])} {fmt(v[i+2][1])}"); i += 3
                else:           d.append("Z"); i += 1
            body.append(f'<path fill-rule="evenodd" d="{"".join(d)}" {a}/>')
    wh = f' width="{w or vw}" height="{h or vh}"'
    dd = f"<defs>{''.join(defs)}</defs>" if defs else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {fmt(vw)} {fmt(vh)}"{wh}>'
            f'{dd}{"".join(body)}</svg>\n')

# ───────────── растеризация (Pillow, суперсэмплинг) ─────────────
def _rrect(W, H, x0, y0, x1, y1, rad):
    m = Image.new("L", (W, H), 0)
    if x1 <= x0 or y1 <= y0: return m
    d = ImageDraw.Draw(m)
    rad = max(0, min(rad, (x1-x0)/2, (y1-y0)/2))
    if rad > 0: d.rounded_rectangle([x0, y0, x1, y1], radius=rad, fill=255)
    else: d.rectangle([x0, y0, x1, y1], fill=255)
    return m

def _ell(W, H, x0, y0, x1, y1):
    m = Image.new("L", (W, H), 0)
    if x1 > x0 and y1 > y0: ImageDraw.Draw(m).ellipse([x0, y0, x1, y1], fill=255)
    return m

def _flatten(sh, k):
    v, c = sh["verts"], sh["codes"]; polys = []; cur = []; i = 0
    def bez(pts, n=14):
        out = []
        for j in range(1, n+1):
            t = j/n; m = len(pts)-1
            if m == 2:
                q = ((1-t)**2*np.array(pts[0]) + 2*(1-t)*t*np.array(pts[1]) + t*t*np.array(pts[2]))
            else:
                q = ((1-t)**3*np.array(pts[0]) + 3*(1-t)**2*t*np.array(pts[1]) +
                     3*(1-t)*t*t*np.array(pts[2]) + t**3*np.array(pts[3]))
            out.append((q[0]*k, q[1]*k))
        return out
    while i < len(c):
        if c[i] == 1:
            if len(cur) > 2: polys.append(cur)
            cur = [(v[i][0]*k, v[i][1]*k)]; i += 1
        elif c[i] == 2: cur.append((v[i][0]*k, v[i][1]*k)); i += 1
        elif c[i] == 3: cur += bez([(cur[-1][0]/k, cur[-1][1]/k), v[i], v[i+1]]); i += 2
        elif c[i] == 4: cur += bez([(cur[-1][0]/k, cur[-1][1]/k), v[i], v[i+1], v[i+2]]); i += 3
        else:
            if len(cur) > 2: polys.append(cur)
            cur = []; i += 1
    if len(cur) > 2: polys.append(cur)
    return polys

def _masks(sh, k, W, H):
    t = sh["t"]; sw = sh["sw"]*k; hs = sw/2
    want_s = bool(sh["stroke"]) and sh["sw"] > 0
    fm = sm = None; bbox = None
    if t == "rect":
        x, y, w, h, r = (sh[q]*k for q in ("x", "y", "w", "h", "r"))
        bbox = (x, y, x+w, y+h)
        fm = _rrect(W, H, x, y, x+w, y+h, r)
        if want_s:
            sm = ImageChops.subtract(_rrect(W, H, x-hs, y-hs, x+w+hs, y+h+hs, r+hs),
                                     _rrect(W, H, x+hs, y+hs, x+w-hs, y+h-hs, max(r-hs, 0)))
    elif t == "circle":
        cx, cy, r = sh["cx"]*k, sh["cy"]*k, sh["r"]*k
        bbox = (cx-r, cy-r, cx+r, cy+r)
        fm = _ell(W, H, cx-r, cy-r, cx+r, cy+r)
        if want_s:
            sm = ImageChops.subtract(_ell(W, H, cx-r-hs, cy-r-hs, cx+r+hs, cy+r+hs),
                                     _ell(W, H, cx-r+hs, cy-r+hs, cx+r-hs, cy+r-hs))
    elif t == "poly":
        pts = [(x*k, y*k) for x, y in sh["pts"]]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        bbox = (min(xs), min(ys), max(xs), max(ys))
        if sh["close"] and sh["fill"]:
            fm = Image.new("L", (W, H), 0); ImageDraw.Draw(fm).polygon(pts, fill=255)
        if want_s:
            sm = Image.new("L", (W, H), 0); d = ImageDraw.Draw(sm)
            segs = list(zip(pts, pts[1:])) + ([(pts[-1], pts[0])] if sh["close"] else [])
            for a, b in segs: d.line([a, b], fill=255, width=max(1, round(sw)))
            for p in pts: d.ellipse([p[0]-hs, p[1]-hs, p[0]+hs, p[1]+hs], fill=255)
    elif t == "text":
        fm = Image.new("L", (W, H), 0)
        for poly in _flatten(sh, k):
            m = Image.new("L", (W, H), 0); ImageDraw.Draw(m).polygon(poly, fill=255)
            fm = ImageChops.difference(fm, m)      # even-odd: дырки в буквах
    return fm, sm, bbox

def _grad(W, H, bbox, c1, c2):
    x0, y0, x1, y1 = bbox
    xs = np.arange(W, dtype=np.float32)[None, :]; ys = np.arange(H, dtype=np.float32)[:, None]
    u = np.clip((xs-x0)/max(x1-x0, 1e-6), 0, 1); v = np.clip((ys-y0)/max(y1-y0, 1e-6), 0, 1)
    tt = ((u+v)/2)[..., None]
    a = np.array([int(c1[i:i+2], 16) for i in (1, 3, 5)], np.float32)
    b = np.array([int(c2[i:i+2], 16) for i in (1, 3, 5)], np.float32)
    return Image.fromarray((a*(1-tt)+b*tt).astype(np.uint8), "RGB").convert("RGBA")

def _rgba(hexc):
    return tuple(int(hexc[i:i+2], 16) for i in (1, 3, 5)) + (255,)

def render(shapes, vw, vh, out_w, ss=4):
    W = int(round(out_w*ss)); k = W/vw; H = int(round(vh*k))
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for sh in shapes:
        fm, sm, bbox = _masks(sh, k, W, H)
        for kind, m in (("fill", fm), ("stroke", sm)):
            col = sh["fill"] if kind == "fill" else sh["stroke"]
            if m is None or not col: continue
            if sh["op"] < 1: m = m.point(lambda v, o=sh["op"]: int(v*o))
            layer = _grad(W, H, bbox, col[1], col[2]) if isinstance(col, tuple) \
                else Image.new("RGBA", (W, H), _rgba(col))
            layer.putalpha(m)
            img = Image.alpha_composite(img, layer)
    return img.resize((out_w, int(round(out_w*vh/vw))), Image.LANCZOS)

# ───────────── ICO (BMP-DIB для малых размеров, PNG для больших) ─────────────
def write_ico(master, sizes, path):
    entries = []
    for s in sizes:
        im = master.resize((s, s), Image.LANCZOS)
        if s <= 64:
            hdr = struct.pack("<IiiHHIIiiII", 40, s, s*2, 1, 32, 0, s*s*4, 0, 0, 0, 0)
            px = im.tobytes("raw", "BGRA")
            rows = [px[i*s*4:(i+1)*s*4] for i in range(s)][::-1]
            data = hdr + b"".join(rows) + b"\x00"*(((s+31)//32)*4*s)
        else:
            b = io.BytesIO(); im.save(b, "PNG"); data = b.getvalue()
        entries.append((s, data))
    off = 6 + 16*len(entries); head = struct.pack("<HHH", 0, 1, len(entries)); dirs = b""; blobs = b""
    for s, data in entries:
        dirs += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), off + len(blobs))
        blobs += data
    with open(path, "wb") as f: f.write(head + dirs + blobs)

# ───────────── ЛОГОТИП-ЗНАК (сетка 64×64) ─────────────
def mark():
    return [
        R(2, 2, 60, 60, 14, fill=("g", "#5BAEFF", "#1E47C8")),
        C(27, 29, 18, fill=WHITE),
        L([(27, 13.5), (27, 16.5)], "#9DB8E8", 2.2), L([(27, 41.5), (27, 44.5)], "#9DB8E8", 2.2),
        L([(11.5, 29), (14.5, 29)], "#9DB8E8", 2.2), L([(39.5, 29), (42.5, 29)], "#9DB8E8", 2.2),
        L([(27, 29), (27, 18.5)], BLUE_D, 4), L([(27, 29), (34.5, 33.5)], BLUE_D, 4),
        C(27, 29, 2.6, fill=BLUE_D),
        C(47, 47, 11.5, fill=GREEN, stroke=WHITE, sw=3.5),
        L([(41.8, 47.5), (45.5, 51), (52.5, 42.5)], WHITE, 4),
    ]

# ───────────── ИКОНКИ (сетка 32×32) ─────────────
def clock(cx, cy, r):
    k = r/12
    return [C(cx, cy, r, fill=WHITE, stroke=BLUE, sw=2.6*k),
            L([(cx, cy), (cx, cy-7*k)], DARK, 2.2*k), L([(cx, cy), (cx+5*k, cy+3*k)], DARK, 2.2*k),
            C(cx, cy, 1.5*k, fill=DARK)]

def plus_badge(cx=23.5, cy=23.5, r=7):
    return [C(cx, cy, r, fill=GREEN, stroke=WHITE, sw=2),
            L([(cx, cy-3.4), (cx, cy+3.4)], WHITE, 2.4), L([(cx-3.4, cy), (cx+3.4, cy)], WHITE, 2.4)]

def pencil_badge(cx=23.5, cy=23.5, r=7):
    return [C(cx, cy, r, fill=BLUE, stroke=WHITE, sw=2),
            P([(cx-3.0, cy+3.0), (cx+2.0, cy-2.0), (cx+3.0, cy-1.0), (cx-2.0, cy+4.0)],
              fill=WHITE, stroke=WHITE, sw=0.8),
            P([(cx-3.0, cy+3.0), (cx-4.0, cy+5.0), (cx-2.0, cy+4.0)],
              fill=GRAY_L, stroke=GRAY_L, sw=0.6),
            P([(cx+2.0, cy-2.0), (cx+2.8, cy-2.8), (cx+3.8, cy-1.8), (cx+3.0, cy-1.0)],
              fill=RED, stroke=RED, sw=0.6)]

def task(dx=0, line=True):
    s = [P([(7, 3), (19, 3), (25, 9), (25, 29), (7, 29)], fill=WHITE, stroke=GRAY, sw=1.6),
         P([(19, 3), (19, 9), (25, 9)], fill=GRAY_L, stroke=GRAY, sw=1.2),
         L([(10.5, 16.5), (14.5, 20.5), (21.5, 12.5)], GREEN, 3)]
    if line: s.append(L([(10.5, 25), (21.5, 25)], GRAY_L, 2))
    return xf(s, 1, dx, 0)

def refresh():
    cx = cy = 16; r = 9
    pts = [(cx+r*math.cos(math.radians(a)), cy+r*math.sin(math.radians(a))) for a in range(20, 301, 5)]
    th = math.radians(300); tan = (-math.sin(th), math.cos(th)); nor = (math.cos(th), math.sin(th))
    ex, ey = pts[-1]
    tip = (ex+tan[0]*6.5, ey+tan[1]*6.5)
    b1 = (ex-tan[0]*1.5+nor[0]*5, ey-tan[1]*1.5+nor[1]*5)
    b2 = (ex-tan[0]*1.5-nor[0]*5, ey-tan[1]*1.5-nor[1]*5)
    return [L(pts, BLUE, 3.6), P([tip, b1, b2], fill=BLUE, stroke=BLUE, sw=1)]

def calendar():
    s = [R(4, 6, 24, 22, 3, fill=WHITE),
         R(4, 6, 24, 7, 3, fill=BLUE), R(4, 9.5, 24, 3.5, 0, fill=BLUE)]
    for ry in (15.8, 19.8, 23.8):
        for cx in (7.2, 12.2, 17.2, 22.2):
            if (ry, cx) == (19.8, 17.2): continue
            s.append(R(cx, ry, 2.6, 2.6, 0.5, fill=GRAY_L))
    s += [R(16.8, 19.4, 3.4, 3.4, 0.8, fill=RED),
          R(4, 6, 24, 22, 3, stroke=GRAY, sw=1.5),
          R(9.5, 3, 3, 6.5, 1.5, fill=DARK), R(19.5, 3, 3, 6.5, 1.5, fill=DARK)]
    return s

ICONS = {
    "app":          (64, mark()),
    "add_task":     (32, task(-2, line=False) + plus_badge()),
    "add_schedule": (32, clock(14, 14, 10.5) + plus_badge()),
    "edit_task":    (32, task(line=False) + pencil_badge()),
    "edit_schedule":(32, clock(14, 14, 10.5) + pencil_badge()),
    "start":        (32, [P([(9, 7), (25, 16), (9, 25)], fill=GREEN, stroke=GREEN, sw=3)]),
    "stop":         (32, [R(7, 7, 18, 18, 3.5, fill=RED)]),
    "pause":        (32, [R(8, 7, 5.5, 18, 1.8, fill=AMBER), R(18.5, 7, 5.5, 18, 1.8, fill=AMBER)]),
    "refresh":      (32, refresh()),
    "add":          (32, [C(16, 16, 12.5, fill=GREEN),
                          L([(16, 9.5), (16, 22.5)], WHITE, 3.4), L([(9.5, 16), (22.5, 16)], WHITE, 3.4)]),
    "delete":       (32, [R(12.5, 3, 7, 4.5, 1.5, fill=GRAY), R(6, 6.5, 20, 3.6, 1.6, fill=GRAY),
                          P([(9.3, 12), (22.7, 12), (21.3, 27), (10.7, 27)], fill=RED, stroke=RED, sw=2),
                          L([(13.2, 15.5), (13.7, 24)], WHITE, 1.6), L([(16, 15.5), (16, 24)], WHITE, 1.6),
                          L([(18.8, 15.5), (18.3, 24)], WHITE, 1.6)]),
    "journal":      (32, [R(7, 4, 19, 24, 2.5, fill=WHITE, stroke=BLUE, sw=1.8),
                          R(4.5, 7.2, 5, 2.4, 1.2, fill=GRAY), R(4.5, 13.8, 5, 2.4, 1.2, fill=GRAY),
                          R(4.5, 20.4, 5, 2.4, 1.2, fill=GRAY),
                          L([(13, 10), (22, 10)], GRAY, 1.8), L([(13, 15), (22, 15)], GRAY, 1.8),
                          L([(13, 20), (19.5, 20)], GRAY, 1.8)]),
    "run":          (32, [L([(3.5, 11), (8.5, 11)], GREEN, 2.2), L([(2.5, 16), (8.5, 16)], GREEN, 2.2),
                          L([(3.5, 21), (8.5, 21)], GREEN, 2.2),
                          P([(13, 7), (27, 16), (13, 25)], fill=GREEN, stroke=GREEN, sw=3)]),
    "open_folder":  (32, [P([(3, 7), (12, 7), (14.5, 10), (27, 10), (27, 25), (3, 25)],
                            fill="#E2A21A", stroke="#C98A00", sw=1.2),
                          P([(3, 25), (7, 14), (30, 14), (26, 25)], fill="#FFCB52", stroke="#C98A00", sw=1.2)]),
    "save":         (32, [R(5, 5, 22, 22, 3.5, fill=BLUE), R(10, 5, 12, 8, 1, fill=GRAY_L),
                          R(17.5, 6.5, 2.5, 5, 0.6, fill=BLUE_D), R(8.5, 16.5, 15, 10.5, 1.2, fill=WHITE),
                          L([(11, 20), (21, 20)], GRAY_L, 1.4), L([(11, 23.5), (21, 23.5)], GRAY_L, 1.4)]),
    "cancel":       (32, [C(16, 16, 12.5, fill=RED),
                          L([(11, 11), (21, 21)], WHITE, 3.2), L([(21, 11), (11, 21)], WHITE, 3.2)]),
    "calendar":     (32, calendar()),
    "task":         (32, task()),
    "schedule":     (32, clock(16, 16, 12)),
}
CAPTIONS = {
    "app": "Главная форма", "add_task": "Добавить задание", "add_schedule": "Добавить расписание",
    "edit_task": "Изменить задание", "edit_schedule": "Изменить расписание",
    "start": "Старт", "stop": "Стоп", "pause": "Пауза", "refresh": "Обновить", "add": "Добавить",
    "delete": "Удалить", "journal": "Журнал", "run": "Запустить", "open_folder": "Открыть папку",
    "save": "Сохранить", "cancel": "Отмена", "calendar": "Выбор периода",
    "task": "Задание (узел)", "schedule": "Расписание (узел)",
}

# ───────────── ЛОГОТИП (горизонтальный) ─────────────
def wordmark(x, y, size, ca, cb):
    wa = adv(NAME_A, size)
    return [T(NAME_A, x, y, size, ca), T(NAME_B, x+wa, y, size, cb)], wa + adv(NAME_B, size)

def build_logo():
    name, w = wordmark(158, 80, 58, BLUE, DARK)
    tag = [T(TAGLINE, 160, 112, 18, GRAY, bold=False)]
    vw = max(158+w, 160+adv(TAGLINE, 18, False)) + 16
    return xf(mark(), 2, 6, 6) + name + tag, vw, 140

# ───────────── КАРТИНКИ ДЛЯ WiX ─────────────
def build_banner():      # WixUIBannerBmp 493×58
    s = [R(0, 0, 493, 58, 0, fill=WHITE),
         C(459, 29, 52, stroke="#E3ECFA", sw=1), C(459, 29, 36, stroke="#E3ECFA", sw=1)]
    return s + xf(mark(), 44/64, 493-12-44, 7), 493, 58

def build_dialog():      # WixUIDialogBmp 493×312 (слева 164 px — картинка)
    s = [R(0, 0, 493, 312, 0, fill=WHITE), R(0, 0, 164, 312, 0, fill=("g", NAVY, "#2457C9"))]
    for r_, o in ((70, .14), (52, .10), (34, .07)):
        s.append(C(82, 244, r_, stroke=WHITE, sw=1.2, op=o))
    s += xf(mark(), 1.5, 34, 38)
    sz = 17; wa = adv(NAME_A, sz); wt = wa + adv(NAME_B, sz); x0 = 82 - wt/2
    s += [T(NAME_A, x0, 172, sz, "#8EC1FF"), T(NAME_B, x0+wa, 172, sz, WHITE)]
    tw = adv("Планировщик заданий", 10, False)
    s.append(T("Планировщик заданий", 82-tw/2, 192, 10, "#B9CCF2", bold=False))
    return s, 493, 312

# ───────────── MAIN ─────────────
def main():
    for d in ("logo", "installer", "icons/svg", "icons/png", "icons/ico"):
        os.makedirs(os.path.join(OUT, d), exist_ok=True)
    def save_svg(path, shapes, vw, vh, w=None, h=None):
        open(os.path.join(OUT, path), "w", encoding="utf-8").write(svg_of(shapes, vw, vh, w, h))

    # логотип
    sh, vw, vh = build_logo()
    save_svg("logo/logo.svg", sh, vw, vh)
    render(sh, vw, vh, 1400).save(os.path.join(OUT, "logo/logo.png"))
    save_svg("logo/logo-mark.svg", mark(), 64, 64, 256, 256)
    render(mark(), 64, 64, 512).save(os.path.join(OUT, "logo/logo-mark-512.png"))

    # WiX
    for name, fn in (("banner", build_banner), ("dialog", build_dialog)):
        sh, vw, vh = fn()
        save_svg(f"installer/{name}.svg", sh, vw, vh)
        render(sh, vw, vh, 493, ss=4).convert("RGB").save(os.path.join(OUT, f"installer/{name}.bmp"))

    # иконки
    masters = {}
    for name, (vb, sh) in ICONS.items():
        save_svg(f"icons/svg/{name}.svg", sh, vb, vb, 32, 32)
        m = render(sh, vb, vb, 512); masters[name] = m
        for s in (16, 24, 32, 48):
            d = os.path.join(OUT, f"icons/png/{s}"); os.makedirs(d, exist_ok=True)
            m.resize((s, s), Image.LANCZOS).save(os.path.join(d, f"{name}.png"))
        write_ico(m, (16, 24, 32, 48), os.path.join(OUT, f"icons/ico/{name}.ico"))
    # иконка приложения / главной формы — расширенный набор размеров
    write_ico(masters["app"], (16, 24, 32, 48, 64, 128, 256), os.path.join(OUT, "icons/ico/app.ico"))
    shutil.copy(os.path.join(OUT, "icons/ico/app.ico"), os.path.join(OUT, "installer/app.ico"))

    # превью
    names = list(ICONS); cols = 6; tw, th = 170, 150
    rows = math.ceil(len(names)/cols)
    sheet = Image.new("RGB", (cols*tw, rows*th), "#F3F5F8")
    font = ImageFont.truetype(FONT_REG, 13); d = ImageDraw.Draw(sheet)
    for i, n in enumerate(names):
        cx, cy = (i % cols)*tw, (i//cols)*th
        d.rounded_rectangle([cx+6, cy+6, cx+tw-6, cy+th-6], 8, fill="white", outline="#DDE2EA")
        sheet.paste(masters[n].resize((64, 64), Image.LANCZOS), (cx+tw//2-32, cy+14),
                    masters[n].resize((64, 64), Image.LANCZOS))
        t = CAPTIONS[n]; w = d.textlength(t, font=font)
        d.text((cx+(tw-w)/2, cy+86), t, fill="#27313F", font=font)
        for j, s in enumerate((16, 24, 32)):
            im = masters[n].resize((s, s), Image.LANCZOS)
            sheet.paste(im, (cx+tw//2-36+[0, 24, 56][j], cy+114+(32-s)//2), im)
    sheet.save(os.path.join(OUT, "preview.png"))

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""make-icons.py -- AquaDE's icon theme, drawn here as SVG.

The programs in the tank (Thunar first) show folders, files, drives and
places in whatever icon theme they are given; Adwaita's are flat and grey,
and on the sand among the plastic sheets they look like something left
behind by another desktop. These are the things themselves, as near as a
drawing gets, and as they would be under water: a sea-green plastic folder
with sheets in it, a white plastic page with a corner turned down and
typing on it, a spiral shell with a hermit crab at home in it, a patch of
the tank floor, a crab pot for the trash, a treasure chest for a package,
a sonar set for the computer -- and, as they always were, a stack of
instant photographs, a record sliding out of its sleeve, a film slate,
punched cards, a block of wood type, a brass globe, a drive in its
aluminium case. (The set began as DEsk's, drawn for a walnut desk.) What is not here (toolbar arrows, dialog
signs) comes from Adwaita, which the theme inherits.

How they are made real: the shape and its light are gradients -- every
icon reads as itself with nothing more, which is how Qt's programs (whose
SVG has no filters) show them -- and over that, SVG filters as librsvg
draws them for GTK: the grain of card, paper, wood and brushed metal
(feTurbulence), the light catching an edge (feSpecularLighting on the
blurred outline), what is pressed in (an inner shadow) and the shadow a
thing lays on the desk. Nothing is drawn by a filter alone.

Each icon is drawn twice: in full, from 32 px up (scalable/), and plainer
-- no grain, no fine print, heavier edges -- for the 16 to 31 px of a side
pane or a list (16x16/, scaled). Both on 256 units. Run it from anywhere;
it writes themes/icons/AquaDE next to itself.

    python3 themes/make-icons.py
"""

import math
import os
import random
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "icons", "AquaDE")


def rgbf(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def poly(*pts):
    return "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + " Z"


class Icon:
    """One SVG, 256 units square: its gradients, clips and filters
    numbered within it, each filter made once."""

    def __init__(self, small):
        self.small = small
        self.defs = []
        self.body = []
        self.n = 0
        self.made = {}

    def _id(self, p):
        self.n += 1
        return f"{p}{self.n}"

    @staticmethod
    def _stops(stops):
        out = []
        for st in stops:
            op = st[2] if len(st) > 2 else 1
            out.append(f'<stop offset="{st[0]:g}" stop-color="{st[1]}"'
                       + (f' stop-opacity="{op:g}"' if op != 1 else "") + "/>")
        return "".join(out)

    def lin(self, stops, x1=0, y1=0, x2=0, y2=1, user=False):
        i = self._id("l")
        u = ' gradientUnits="userSpaceOnUse"' if user else ""
        self.defs.append(f'<linearGradient id="{i}" x1="{x1:g}" y1="{y1:g}" x2="{x2:g}" '
                         f'y2="{y2:g}"{u}>{self._stops(stops)}</linearGradient>')
        return f"url(#{i})"

    def rad(self, stops, cx=0.5, cy=0.5, r=0.5, fx=None, fy=None, user=False):
        i = self._id("r")
        u = ' gradientUnits="userSpaceOnUse"' if user else ""
        f = f' fx="{fx:g}" fy="{fy:g}"' if fx is not None else ""
        self.defs.append(f'<radialGradient id="{i}" cx="{cx:g}" cy="{cy:g}" r="{r:g}"{f}{u}>'
                         f"{self._stops(stops)}</radialGradient>")
        return f"url(#{i})"

    def clip(self, shapes):
        i = self._id("c")
        self.defs.append(f'<clipPath id="{i}">{shapes}</clipPath>')
        return f"url(#{i})"

    def filt(self, key, body, region='x="-4%" y="-4%" width="108%" height="108%"'):
        """a filter, or None small: at 16 px there is nothing for one to do"""
        if self.small:
            return None
        if key not in self.made:
            i = self._id("f")
            self.defs.append(f'<filter id="{i}" {region} color-interpolation-filters="sRGB">'
                             f"{body}</filter>")
            self.made[key] = f"url(#{i})"
        return self.made[key]

    def add(self, s):
        self.body.append(s)

    def svg(self):
        size = 16 if self.small else 64
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" '
                f'viewBox="0 0 256 256"><defs>{"".join(self.defs)}</defs>'
                f'{"".join(self.body)}</svg>\n')


def g(inner, filt=None, tf=None, op=None, clip=None):
    a = ""
    if filt:
        a += f' filter="{filt}"'
    if tf:
        a += f' transform="{tf}"'
    if op is not None:
        a += f' opacity="{op:g}"'
    if clip:
        a += f' clip-path="{clip}"'
    return f"<g{a}>{inner}</g>"


# ---- what makes a drawing a thing ------------------------------------------

def grain(ic, amt=0.05, freq="0.8", octaves=2, seed=1, dark="#3a2812", light=0.7):
    """Specks, dark and light, in whatever it is put on: the fibres of
    paper and card. freq as "fx fy" draws them out into streaks -- wood
    along its grain, brushed metal along the brush."""
    r, gg, b = rgbf(dark)
    k = 4.0 * amt
    kl = k * light
    body = (f'<feTurbulence type="fractalNoise" baseFrequency="{freq}" numOctaves="{octaves}" '
            f'seed="{seed}" result="n"/>'
            f'<feColorMatrix in="n" type="matrix" values="0 0 0 0 {r:.3f} 0 0 0 0 {gg:.3f} '
            f'0 0 0 0 {b:.3f} {k:.3f} 0 0 0 {-0.5 * k:.3f}" result="d"/>'
            f'<feColorMatrix in="n" type="matrix" values="0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 '
            f'{-kl:.3f} 0 0 0 {0.5 * kl:.3f}" result="l"/>'
            '<feMerge result="m"><feMergeNode in="d"/><feMergeNode in="l"/></feMerge>'
            '<feComposite in="m" in2="SourceAlpha" operator="in" result="t"/>'
            '<feMerge><feMergeNode in="SourceGraphic"/><feMergeNode in="t"/></feMerge>')
    return ic.filt(("grain", amt, freq, octaves, seed, dark, light), body)


def edge_light(ic, blur=3.0, depth=3.0, shine=0.9, exp=26, elev=48, k=0.7):
    """The light along an edge that faces it: the outline, blurred, lit as
    a surface from above -- what a bevel is."""
    body = (f'<feGaussianBlur in="SourceAlpha" stdDeviation="{blur:g}" result="b"/>'
            f'<feSpecularLighting in="b" surfaceScale="{depth:g}" specularConstant="{shine:g}" '
            f'specularExponent="{exp:g}" lighting-color="#fff" result="s">'
            f'<feDistantLight azimuth="270" elevation="{elev:g}"/></feSpecularLighting>'
            '<feComposite in="s" in2="SourceAlpha" operator="in" result="si"/>'
            f'<feComposite in="SourceGraphic" in2="si" operator="arithmetic" k1="0" k2="1" '
            f'k3="{k:g}" k4="0"/>')
    return ic.filt(("bevel", blur, depth, shine, exp, elev, k), body)


def pressed(ic, dy=2.0, blur=1.6, col="#000", op=0.5):
    """An inner shadow: the shape pressed into what it is on, darker under
    its top edge."""
    body = ('<feComponentTransfer in="SourceAlpha" result="inv">'
            '<feFuncA type="table" tableValues="1 0"/></feComponentTransfer>'
            f'<feGaussianBlur in="inv" stdDeviation="{blur:g}"/>'
            f'<feOffset dx="0" dy="{dy:g}" result="o"/>'
            f'<feFlood flood-color="{col}" flood-opacity="{op:g}"/>'
            '<feComposite in2="o" operator="in"/>'
            '<feComposite in2="SourceAlpha" operator="in" result="is"/>'
            '<feMerge><feMergeNode in="SourceGraphic"/><feMergeNode in="is"/></feMerge>')
    return ic.filt(("inner", dy, blur, col, op), body)


def dropped(ic, dy=4.0, blur=4.0, op=0.38):
    """the soft shadow a thing lays on the desk below it"""
    body = (f'<feGaussianBlur in="SourceAlpha" stdDeviation="{blur:g}"/>'
            f'<feOffset dx="0" dy="{dy:g}" result="o"/>'
            f'<feFlood flood-color="#1a1006" flood-opacity="{op:g}"/>'
            '<feComposite in2="o" operator="in" result="s"/>'
            '<feMerge><feMergeNode in="s"/><feMergeNode in="SourceGraphic"/></feMerge>')
    return ic.filt(("drop", dy, blur, op), body,
                   'filterUnits="userSpaceOnUse" x="-16" y="-16" width="288" height="288"')


def inked(ic, seed=4):
    """ink as a rubber stamp leaves it: heavy here, hardly there"""
    body = (f'<feTurbulence type="fractalNoise" baseFrequency="0.3" numOctaves="2" '
            f'seed="{seed}" result="n"/>'
            '<feColorMatrix in="n" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 '
            '3.4 0 0 0 -0.62" result="a"/>'
            '<feComposite in="SourceGraphic" in2="a" operator="in"/>')
    return ic.filt(("ink", seed), body)


def contact(ic, cx, cy, rx, ry, op=0.32):
    """the dark right under a thing, where it meets the desk"""
    return (f'<ellipse cx="{cx:g}" cy="{cy:g}" rx="{rx:g}" ry="{ry:g}" fill="'
            f'{ic.rad([(0, "#1a1006", op), (0.6, "#1a1006", op * 0.42), (1, "#1a1006", 0)])}"/>')


def place(ic, inner, cx, cy, rx, ry, op=0.3, drop=(3.0, 4.0, 0.34)):
    """a thing set down: its contact shadow, then it, with its own shadow"""
    ic.add(contact(ic, cx, cy, rx, ry, op))
    ic.add(g(inner, dropped(ic, *drop)))


def typed(x0, x1, y0, rows, gap=11.0, h=4.4, col="#6a6457", seed=1, last_short=True, op=1.0):
    """lines of typing: words of every length, a ragged right edge"""
    rnd = random.Random(seed)
    parts = []
    for i in range(rows):
        y = y0 + i * gap
        end = x1 - rnd.uniform(0, 6)
        if last_short and i == rows - 1:
            end = x0 + (x1 - x0) * rnd.uniform(0.35, 0.6)
        elif rows > 3 and rnd.random() < 0.12:
            end = x0 + (x1 - x0) * rnd.uniform(0.55, 0.85)
        x = x0
        while x < end - 4:
            w = min(rnd.uniform(7, 24), end - x)
            parts.append(f'<rect x="{x:.1f}" y="{y - h / 2:.1f}" width="{w:.1f}" '
                         f'height="{h:g}" rx="{h / 2:g}"/>')
            x += w + rnd.uniform(4, 6)
    return f'<g fill="{col}" opacity="{op:g}">{"".join(parts)}</g>'


# ---- materials ----------------------------------------------------------

# under water nothing is paper or card: sheets are the diver's white
# plastic the windows are, and a folder is sea-green plastic, the kind a
# diver's slate is kept in
PAPER = [(0, "#fbfefe"), (1, "#e3eeee")]
PAPER_EDGE = "#9fb7b6"
MANILA_BACK = [(0, "#5aaea2"), (1, "#3a867c")]
MANILA_FRONT = [(0, "#a8e6db"), (0.05, "#90d8cb"), (0.7, "#5fb4a7"), (1, "#4a9d91")]
MANILA_EDGE = "#2a6a62"
BRASS = [(0, "#fff0bf"), (0.3, "#e6c166"), (0.62, "#a77b2a"), (0.85, "#d8b257"), (1, "#8a6420")]
WALNUT = [(0, "#87532c"), (1, "#56311a")]
STEEL = [(0, "#fbfcfd"), (0.45, "#cfd4da"), (0.55, "#bcc2ca"), (1, "#e8ebee")]


def brass(ic, vertical=False):
    return ic.lin(BRASS, 0, 0, 0, 1) if vertical else ic.lin(BRASS, 0, 0, 1, 0)


# ---- folders ------------------------------------------------------------

BACK = ("M24,60 V46 Q24,34 36,34 H93 Q100,34 104.5,39.5 L111.5,48 H220 Q232,48 232,60 "
        "V204 Q232,216 220,216 H36 Q24,216 24,204 Z")


def front_path(xt0, xt1, top, xb0, xb1, bot, rt=6.0, rb=10.0):
    """the front of a folder: a little wider at the top, leaning out"""
    h = bot - top
    lr = xt1 - xb1
    ll = xb0 - xt0
    return (f"M{xt0 + rt:.1f},{top:.1f} H{xt1 - rt:.1f} "
            f"Q{xt1:.1f},{top:.1f} {xt1 - lr * rt / h:.1f},{top + rt:.1f} "
            f"L{xb1 + lr * rb / h:.1f},{bot - rb:.1f} Q{xb1:.1f},{bot:.1f} {xb1 - rb:.1f},{bot:.1f} "
            f"H{xb0 + rb:.1f} Q{xb0:.1f},{bot:.1f} {xb0 - ll * rb / h:.1f},{bot - rb:.1f} "
            f"L{xt0 + ll * rt / h:.1f},{top + rt:.1f} Q{xt0:.1f},{top:.1f} {xt0 + rt:.1f},{top:.1f} Z")


def loose_sheet(ic, x, y, w, h, angle, seed, rows=0):
    s = (f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="2.5" '
         f'fill="{ic.lin(PAPER)}" stroke="{PAPER_EDGE}" stroke-width="1.4"/>')
    if rows:
        s += typed(x + 16, x + w - 24, y + 16, rows, 10, 3.6, "#7b7466", seed, False)
    return g(s, tf=f"rotate({angle:g} {x + w / 2:g} {y + h / 2:g})")


def deboss(ic, shape, cx, cy, scale=1.0):
    """pressed into the card, as a blind stamp does: the hollow in shadow
    under its top edge, and a lip of light along the bottom of it"""
    if ic.small:
        return (f'<g transform="translate({cx:g} {cy:g}) scale({scale:g})" fill="#2a6a62" '
                f'stroke="#2a6a62" opacity=".85">{shape}</g>')
    lip = f'<g transform="translate(0 1.8)" fill="#dcfff9" stroke="#dcfff9" opacity=".9">{shape}</g>'
    sunk = g(f'<g fill="#3a8a80" stroke="#3a8a80">{shape}</g>',
             pressed(ic, 2.2, 1.5, "#0a2f2a", 0.65), op=0.86)
    return f'<g transform="translate({cx:g} {cy:g}) scale({scale:g})">{lip}{sunk}</g>'


def folder(ic, emblem=None, open_=False):
    sm = ic.small
    parts = []
    back = (f'<path d="{BACK}" fill="{ic.lin(MANILA_BACK)}" stroke="{MANILA_EDGE}" '
            f'stroke-width="{12 if sm else 2.2:g}" stroke-linejoin="round"/>')
    if not sm:
        back += ('<path d="M36,37.2 H92" stroke="#d6fff8" stroke-width="2.4" '
                 'stroke-linecap="round" opacity=".9"/>'
                 '<path d="M114,51.2 H219" stroke="#c4f4ec" stroke-width="2" '
                 'stroke-linecap="round" opacity=".7"/>')
    parts.append(g(back, grain(ic, 0.02, "0.85", 2, 3, "#0f3a35")))
    top = 124 if open_ else 98
    # the sheets in it, and how dark it is down among them
    if sm:
        parts.append('<rect x="38" y="58" width="180" height="96" fill="#f6fcfc" '
                     'stroke="#86a3a2" stroke-width="10"/>')
    else:
        inside = loose_sheet(ic, 38, 58, 182, 148, -2.2, 5)
        inside += loose_sheet(ic, 44, 64, 172, 140, 1.5, 7, 5 if open_ else 3)
        inside += (f'<rect x="24" y="{top - 40:g}" width="208" height="40" '
                   f'fill="{ic.lin([(0, "#0c2e2a", 0), (1, "#0c2e2a", 0.3)])}"/>')
        parts.append(g(inside, clip=ic.clip(f'<path d="{BACK}"/>')))
    # the front, leaning out a little -- open, further
    lean = 16 if open_ else 7
    xb0, xb1, bot = 22, 234, 220
    d = front_path(xb0 - lean, xb1 + lean, top, xb0, xb1, bot)
    fr = (f'<path d="{d}" fill="{ic.lin(MANILA_FRONT)}" stroke="{MANILA_EDGE}" '
          f'stroke-width="{12 if sm else 2:g}" stroke-linejoin="round"/>')
    if not sm:
        fr += (f'<path d="{d}" fill="'
               f'{ic.lin([(0, "#0f3a35", 0.16), (0.12, "#0f3a35", 0), (0.88, "#0f3a35", 0), (1, "#0f3a35", 0.16)], 0, 0, 1, 0)}"/>'
               f'<path d="M{xb0 - lean + 9:g},{top + 2.4:g} H{xb1 + lean - 9:g}" stroke="#e8fffb" '
               'stroke-width="2.6" stroke-linecap="round"/>'
               f'<path d="M{xb0 - lean + 9:g},{top + 5.4:g} H{xb1 + lean - 9:g}" stroke="#2f7d73" '
               'stroke-width="1.1" stroke-linecap="round" opacity=".45"/>'
               f'<path d="M{xb0 + 4:g},{bot - 20:g} H{xb1 - 4:g}" stroke="#2a7067" stroke-width="1.4" opacity=".5"/>'
               f'<path d="M{xb0 + 4:g},{bot - 18.4:g} H{xb1 - 4:g}" stroke="#d6fff8" stroke-width="1.4" opacity=".7"/>')
    if emblem:
        fr += deboss(ic, emblem(sm), 128, (top + bot) / 2 + (2 if sm else 3), 1.4 if sm else 1.0)
    parts.append(g(fr, grain(ic, 0.02, "0.85", 2, 9, "#0f3a35")))
    place(ic, "".join(parts), 128, 222, 114, 12, 0.3, (3, 4, 0.32))


def em_documents(sm):
    return (f'<path d="M-24,-16 H24 M-24,0 H24 M-24,16 H6" fill="none" '
            f'stroke-width="{18 if sm else 7:g}" stroke-linecap="round"/>')


def em_download(sm):
    return ('<path d="M0,-28 V8 M-15,-7 L0,8 L15,-7 M-27,14 V25 H27 V14" fill="none" '
            f'stroke-width="{18 if sm else 7.5:g}" stroke-linecap="round" stroke-linejoin="round"/>')


def em_music(sm):
    return ('<g stroke="none"><ellipse cx="-13" cy="20" rx="10" ry="7.5" transform="rotate(-22 -13 20)"/>'
            '<ellipse cx="17" cy="14" rx="10" ry="7.5" transform="rotate(-22 17 14)"/>'
            '<path d="M-5,17 V-22 L25,-30 V11 H20 V-17 L0,-12 V17 Z"/></g>')


def em_pictures(sm):
    return (f'<rect x="-28" y="-21" width="56" height="42" rx="3" fill="none" '
            f'stroke-width="{16 if sm else 6:g}"/>'
            '<path d="M-22,15 L-8,-2 L2,8 L10,0 L22,15 Z" stroke="none"/>'
            '<circle cx="12" cy="-9" r="5" stroke="none"/>')


def em_videos(sm):
    holes = "".join(f" M{x},-21 h6 v6 h-6 Z M{x},15 h6 v6 h-6 Z" for x in (-25, -13, -1, 11, 23))
    return (f'<path d="M-31,-26 H35 V26 H-31 Z{holes} M-21,-10 H25 V10 H-21 Z" '
            'fill-rule="evenodd" stroke="none" transform="translate(-2 0)"/>')


def em_templates(sm):
    return '<path d="M-26,24 V-26 L24,24 Z M-17,15 V-5 L3,15 Z" fill-rule="evenodd" stroke="none"/>'


def em_people(sm):
    return ('<g stroke="none"><circle cx="-11" cy="-12" r="9"/><circle cx="14" cy="-9" r="8"/>'
            '<path d="M-30,24 Q-30,0 -11,0 Q8,0 8,24 Z"/><path d="M5,24 Q5,3 14,3 Q31,3 31,24 Z"/></g>')


def em_globe(sm):
    return (f'<g fill="none" stroke-width="{14 if sm else 5:g}"><circle r="26"/><ellipse rx="11" ry="26"/>'
            '<path d="M-26,0 H26 M-22,-13 H22 M-22,13 H22"/></g>')


def em_clock(sm):
    return (f'<g fill="none" stroke-width="{14 if sm else 6:g}" stroke-linecap="round"><circle r="26"/>'
            '<path d="M0,-15 V0 L11,8"/></g>')


FOLDERS = {
    "folder": None,
    "inode-directory": None,
    "folder-documents": em_documents,
    "folder-download": em_download,
    "folder-music": em_music,
    "folder-pictures": em_pictures,
    "folder-videos": em_videos,
    "folder-templates": em_templates,
    "folder-publicshare": em_people,
    "folder-remote": em_globe,
    "folder-recent": em_clock,
}


# ---- a sheet and what is on it -------------------------------------------

SHEET = "M54,20 H172 L210,58 V228 Q210,236 202,236 H54 Q46,236 46,228 V28 Q46,20 54,20 Z"
FOLD = "M172,20 L210,58 H180 Q172,58 172,50 Z"


def sheet(ic, content="", tint=None, seed=1):
    """A page, its top right corner turned down; what is on it goes
    under the corner, as it would."""
    sm = ic.small
    body = (f'<path d="{SHEET}" fill="{ic.lin(tint or PAPER)}" stroke="{PAPER_EDGE}" '
            f'stroke-width="{12 if sm else 2:g}" stroke-linejoin="round"/>')
    page = g(body, grain(ic, 0.015, "0.9", 2, seed, "#1f3c3c")) + content
    if not sm:
        page += (f'<path d="M172,58 H210 V68 H172 Z" fill="{ic.lin([(0, "#1c3838", 0.22), (1, "#1c3838", 0)])}"/>'
                 f'<path d="M163,22 H172 V58 H163 Z" fill="'
                 f'{ic.lin([(0, "#1c3838", 0), (1, "#1c3838", 0.14)], 0, 0, 1, 0)}"/>')
    page += (f'<path d="{FOLD}" fill="{ic.lin([(0, "#fbffff"), (0.55, "#e3eded"), (1, "#c3d4d3")], 0.5, 0.5, 0, 1)}" '
             f'stroke="#97b0af" stroke-width="{10 if sm else 1.5:g}" stroke-linejoin="round"/>')
    place(ic, page, 128, 238, 82, 8, 0.28, (2.5, 3.5, 0.3))


def small_lines(ys, x0=78, x1=178, col="#8d8471", w=15, last=None):
    d = " ".join(f"M{x0},{y} H{x1 if (last is None or i < len(ys) - 1) else last}"
                 for i, y in enumerate(ys))
    return f'<path d="{d}" stroke="{col}" stroke-width="{w}"/>'


def m_text(ic):
    if ic.small:
        return sheet(ic, small_lines((96, 132, 168, 204), last=140))
    c = typed(66, 150, 80, 1, 0, 7, "#3c362c", 2, False)
    c += typed(66, 192, 100, 11, 11, 4.4, "#6a6457", 3)
    sheet(ic, c, seed=2)


def m_unknown(ic):
    """a blank page, a question pencilled on it"""
    q = ('<path d="M108,116 C108,92 150,90 150,114 C150,132 128,132 128,152" fill="none" '
         f'stroke="#6b6b6b" stroke-width="{16 if ic.small else 7:g}" stroke-linecap="round" opacity=".7"/>'
         f'<circle cx="128" cy="178" r="{10 if ic.small else 5:g}" fill="#6b6b6b" opacity=".7"/>')
    sheet(ic, g(q, grain(ic, 0.12, "1.2", 1, 12, "#000000", 1.0)), seed=6)


def mono(x0, y0, rows, seed, x1=196, col="#24402b", cw=5.6, gap=12.0, h=5.0):
    """a printout of a program: indented, every letter as wide as the next"""
    rnd = random.Random(seed)
    ind = [0, 0, 2, 2, 4, 4, 4, 2, 0, 0, 2, 4, 4, 2, 0, 0]
    parts = []
    last_x = x0
    for i in range(rows):
        y = y0 + i * gap
        x = x0 + ind[i % len(ind)] * cw
        if i == 0:
            parts.append(f'<rect x="{x:.1f}" y="{y - h / 2:.1f}" width="{cw - 1:.1f}" height="{h:g}"/>')
            x += 2 * cw
        elif rnd.random() < 0.12:
            continue
        room = int((x1 - x) / cw)
        n = min(rnd.randint(8, 20), room)
        used = 0
        while used < n:
            w = min(rnd.randint(2, 7), n - used)
            parts.append(f'<rect x="{x + used * cw:.1f}" y="{y - h / 2:.1f}" '
                         f'width="{w * cw - 1:.1f}" height="{h:g}"/>')
            used += w + 1
        last_x = x + used * cw
    y = y0 + (rows - 1) * gap
    cursor = (f'<rect x="{min(last_x + cw, x1 - cw):.1f}" y="{y - h / 2 - 1:.1f}" '
              f'width="{cw:.1f}" height="{h + 2:g}"/>')
    return f'<g fill="{col}" opacity=".88">{"".join(parts)}{cursor}</g>'


def m_script(ic):
    """the green-barred paper a line printer ran off, its feed holes down
    both sides"""
    sm = ic.small
    x0, x1, y0, y1 = 32, 224, 16, 238
    holes = "" if sm else "".join(
        f" M{x - 4.2:g},{y:g} a4.2,4.2 0 1 0 8.4,0 a4.2,4.2 0 1 0 -8.4,0 Z"
        for y in range(28, 232, 16) for x in (43, 213))
    d = (f"M{x0 + 3},{y0} H{x1 - 3} Q{x1},{y0} {x1},{y0 + 3} V{y1 - 3} Q{x1},{y1} {x1 - 3},{y1} "
         f"H{x0 + 3} Q{x0},{y1} {x0},{y1 - 3} V{y0 + 3} Q{x0},{y0} {x0 + 3},{y0} Z{holes}")
    body = (f'<path d="{d}" fill-rule="evenodd" fill="{ic.lin(PAPER)}" stroke="#c2b99f" '
            f'stroke-width="{12 if sm else 1.8:g}"/>')
    clip = ic.clip(f'<rect x="56" y="{y0}" width="144" height="{y1 - y0}"/>')
    if sm:
        bands = "".join(f'<rect x="56" y="{y}" width="144" height="36" fill="#c9e3c4"/>' for y in (40, 112, 184))
        code = small_lines((76, 112, 148, 184, 214), 76, 170, "#2c4a33", 14, 120)
        page = body + g(bands, clip=clip) + code
    else:
        bands = "".join(f'<rect x="56" y="{y}" width="144" height="{min(20, y1 - 1 - y)}" fill="#d6ecd2"/>'
                        for y in range(22, 238, 40))
        perf = ('<path d="M56,17 V237 M200,17 V237" stroke="#b5ac94" stroke-width="1.2" '
                'stroke-dasharray="3 3"/>')
        page = g(body + g(bands, clip=clip) + perf, grain(ic, 0.035, "0.9", 2, 13, "#4a3a20"))
        page += mono(66, 34, 16, 17)
    place(ic, page, 128, 240, 98, 8, 0.26, (2.5, 3.5, 0.3))


def scene(ic, x, y, w, h, sunset=False):
    """a photograph: sky, sun, far hills, near ones, water"""
    def X(f):
        return x + w * f

    def Y(f):
        return y + h * f
    if sunset:
        sky = ic.lin([(0, "#3d4f8f"), (0.45, "#e2795b"), (0.7, "#f6c26b"), (1, "#fbe3a1")])
        sun, sea = "#fff1c4", ic.lin([(0, "#e79c62"), (1, "#3d4f7a")])
    else:
        sky = ic.lin([(0, "#3f8edb"), (0.62, "#a6d4f3"), (1, "#eaf4f2")])
        sun, sea = "#fffbe6", ic.lin([(0, "#5e9fd2"), (1, "#25598f")])
    sy = Y(0.5 if sunset else 0.26)
    s = f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" fill="{sky}"/>'
    s += (f'<circle cx="{X(0.72):g}" cy="{sy:g}" r="{w * 0.22:g}" fill="'
          f'{ic.rad([(0, "#fff8d8", 0.9), (0.35, "#fff1b0", 0.35), (1, "#fff1b0", 0)])}"/>'
          f'<circle cx="{X(0.72):g}" cy="{sy:g}" r="{w * 0.075:g}" fill="{sun}"/>')
    if not sunset:
        far = poly((X(0), Y(0.66)), (X(0.18), Y(0.42)), (X(0.3), Y(0.52)), (X(0.46), Y(0.34)),
                   (X(0.64), Y(0.58)), (X(0.8), Y(0.46)), (X(1), Y(0.62)), (X(1), Y(0.75)), (X(0), Y(0.75)))
        snow = poly((X(0.46), Y(0.34)), (X(0.52), Y(0.41)), (X(0.48), Y(0.40)), (X(0.42), Y(0.39)))
        s += (f'<path d="{far}" fill="{ic.lin([(0, "#8597bb"), (1, "#b4c0d6")])}"/>'
              f'<path d="{snow}" fill="#f4f7fb" opacity=".9"/>'
              f'<path d="M{X(0):g},{Y(0.78):g} Q{X(0.25):g},{Y(0.58):g} {X(0.55):g},{Y(0.7):g} '
              f'T{X(1):g},{Y(0.64):g} V{Y(0.82):g} H{X(0):g} Z" '
              f'fill="{ic.lin([(0, "#7fbf55"), (1, "#3e7a2a")])}"/>')
    s += f'<rect x="{x:g}" y="{Y(0.8):g}" width="{w:g}" height="{h * 0.2:g}" fill="{sea}"/>'
    s += (f'<path d="M{X(0.55):g},{Y(0.84):g} h{w * 0.3:g} M{X(0.62):g},{Y(0.89):g} h{w * 0.2:g} '
          f'M{X(0.66):g},{Y(0.94):g} h{w * 0.12:g}" stroke="#fff6dc" '
          f'stroke-width="{max(1.2, h * 0.012):g}" stroke-linecap="round" opacity=".7"/>')
    return g(s, clip=ic.clip(f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}"/>'))


def m_html(ic):
    """a web page as it prints: the site's band across the top, a picture,
    the words, the links in blue"""
    if ic.small:
        c = ('<rect x="62" y="72" width="134" height="40" fill="#3d73b3"/>'
             + small_lines((144, 176, 208), 72, 184, "#8d8471", 14, 140))
        return sheet(ic, c)
    c = (f'<rect x="62" y="70" width="132" height="24" rx="3" fill="{ic.lin([(0, "#5b90cf"), (1, "#2f5f9b")])}"/>'
         '<circle cx="75" cy="82" r="6" fill="#fff" opacity=".92"/>'
         '<g fill="#fff" opacity=".75"><rect x="88" y="79.5" width="20" height="5" rx="2.5"/>'
         '<rect x="112" y="79.5" width="16" height="5" rx="2.5"/>'
         '<rect x="132" y="79.5" width="22" height="5" rx="2.5"/></g>')
    c += scene(ic, 62, 102, 62, 46)
    c += ('<rect x="62" y="102" width="62" height="46" fill="none" stroke="#7d7462" '
          'stroke-width=".8" opacity=".6"/>')
    c += typed(132, 194, 107, 4, 10, 4, "#6a6457", 21)
    c += typed(62, 194, 160, 4, 10, 4, "#6a6457", 22)
    c += ('<g fill="#2a5db0"><rect x="62" y="202" width="40" height="4" rx="2"/>'
          '<rect x="110" y="202" width="30" height="4" rx="2"/><rect x="148" y="202" width="34" height="4" rx="2"/></g>'
          '<path d="M62,208.5 H102 M110,208.5 H140 M148,208.5 H182" stroke="#2a5db0" stroke-width="1.1"/>')
    sheet(ic, c, seed=4)


def seal(ic, cx, cy, r):
    """a little brass seal, struck"""
    return (f'<circle cx="{cx:g}" cy="{cy:g}" r="{r:g}" fill="{ic.rad(BRASS[::-1], 0.38, 0.32, 0.75)}" '
            f'stroke="#7a5418" stroke-width="1"/>'
            f'<circle cx="{cx:g}" cy="{cy:g}" r="{r * 0.68:g}" fill="none" stroke="#fff3c6" '
            'stroke-width="1" opacity=".7"/>')


def m_doc(ic):
    """a letter: its letterhead, the date, the words, signed in blue ink"""
    if ic.small:
        c = ('<circle cx="80" cy="84" r="14" fill="#c99a3c"/>'
             + small_lines((132, 164, 196), 72, 184, "#8d8471", 14, 150)
             + '<path d="M112,220 q12,-18 22,-4 t24,-6" fill="none" stroke="#1f3f8a" stroke-width="12"/>')
        return sheet(ic, c)
    c = g(seal(ic, 77, 79, 10), edge_light(ic, 1.2, 2, 0.8, 20, 50, 0.6))
    c += ('<rect x="93" y="72" width="62" height="6.5" rx="3" fill="#3c362c"/>'
          '<rect x="93" y="83" width="42" height="3.6" rx="1.8" fill="#9a9284"/>')
    c += typed(150, 194, 106, 1, 0, 4, "#6a6457", 31, False)
    c += typed(66, 112, 124, 1, 0, 4, "#6a6457", 32, False)
    c += typed(66, 194, 140, 6, 10, 4, "#6a6457", 33)
    c += ('<path d="M120,214 c4,-10 9,-16 12,-8 c2,6 -4,12 2,8 c5,-4 7,-14 11,-10 c3,3 -1,10 3,8 '
          'c6,-3 9,-8 14,-6 c4,2 6,4 14,-2" fill="none" stroke="#1f3f8a" stroke-width="2.4" '
          'stroke-linecap="round" stroke-linejoin="round" opacity=".9"/>')
    sheet(ic, c, seed=5)


def m_sheet(ic):
    """a ledger: green rules, a column for the date, two for the money"""
    tint = [(0, "#f7fbf1"), (1, "#e6eed9")]
    if ic.small:
        c = ('<rect x="62" y="70" width="134" height="30" fill="#8fbf86"/>'
             '<path d="M62,132 H196 M62,168 H196 M62,204 H196 M128,100 V220" stroke="#5f9a58" stroke-width="12"/>')
        return sheet(ic, c, tint)
    rnd = random.Random(51)
    rows = [82 + 11 * i for i in range(13)]
    c = '<rect x="60" y="68" width="136" height="14" fill="#8fbf86" opacity=".55"/>'
    c += '<path d="' + " ".join(f"M60,{y} H196" for y in rows) + '" stroke="#9cc596" stroke-width="1"/>'
    c += ('<path d="M60,68 V218 M84,68 V218 M138,68 V218 M141,68 V218 M166,68 V218 M169,68 V218 M196,68 V218" '
          'stroke="#7fb178" stroke-width="1"/>')
    ent = []
    for y in rows[:-2]:
        cy = y + 5.5
        ent.append(f'<rect x="64" y="{cy - 2:g}" width="{rnd.uniform(9, 15):.1f}" height="4" rx="2"/>')
        x = 88
        end = rnd.uniform(108, 134)
        while x < end - 4:
            w = min(rnd.uniform(6, 16), end - x)
            ent.append(f'<rect x="{x:.1f}" y="{cy - 2:g}" width="{w:.1f}" height="4" rx="2"/>')
            x += w + 4
        for c1 in (166, 196):
            if rnd.random() < 0.75:
                w = rnd.uniform(10, 19)
                ent.append(f'<rect x="{c1 - 3 - w:.1f}" y="{cy - 2:g}" width="{w:.1f}" height="4" rx="1"/>')
    c += f'<g fill="#3f4a3a" opacity=".8">{"".join(ent)}</g>'
    yt = rows[-2] + 5.5
    c += (f'<g fill="#2c3528"><rect x="146" y="{yt - 2.2:g}" width="17" height="4.4" rx="1"/>'
          f'<rect x="172" y="{yt - 2.2:g}" width="21" height="4.4" rx="1"/></g>'
          f'<path d="M143,{yt + 5:g} H165 M143,{yt + 7.4:g} H165 M171,{yt + 5:g} H195 M171,{yt + 7.4:g} H195" '
          'stroke="#c0392b" stroke-width="1"/>')
    sheet(ic, c, tint, seed=8)


def m_pdf(ic):
    """typing, and over it the red of a rubber stamp"""
    if ic.small:
        c = (small_lines((80, 112, 208), 72, 184, "#8d8471", 14)
             + '<rect x="70" y="132" width="118" height="54" fill="none" stroke="#c42b22" stroke-width="14"/>')
        return sheet(ic, c)
    c = typed(66, 194, 80, 13, 11, 4.2, "#6a6457", 41)
    stamp = ('<rect x="74" y="122" width="112" height="54" rx="6" fill="none" stroke="#c42b22" stroke-width="5"/>'
             '<rect x="81.5" y="129.5" width="97" height="39" rx="3" fill="none" stroke="#c42b22" stroke-width="1.6"/>'
             '<text x="130" y="161" font-family="DejaVu Sans, Liberation Sans, Arial, sans-serif" '
             'font-weight="bold" font-size="31" letter-spacing="3" text-anchor="middle" fill="#c42b22">PDF</text>')
    c += g(g(stamp, inked(ic, 7)), tf="rotate(-12 130 149)", op=0.9)
    sheet(ic, c, seed=9)


def mount(ic, tf, chart):
    """a slide in its mount, the light coming through it"""
    sm = ic.small
    d = "M48,30 H208 Q226,30 226,48 V208 Q226,226 208,226 H48 Q30,226 30,208 V48 Q30,30 48,30 Z"
    win = "M62,74 H194 V170 H62 Z"
    film = f'<rect x="58" y="70" width="140" height="104" fill="{ic.lin([(0, "#3a74b0"), (1, "#173d63")])}"/>'
    if chart:
        bars = ((80, 132, "#f2c14e"), (104, 112, "#f08a5d"), (128, 122, "#5ec2b7"), (152, 98, "#e8eef5"))
        film += ('<rect x="74" y="82" width="70" height="6" rx="3" fill="#fff" opacity=".9"/>'
                 '<rect x="74" y="92" width="44" height="4" rx="2" fill="#cfe0f2" opacity=".75"/>')
        film += "".join(f'<rect x="{x}" y="{y}" width="16" height="{160 - y}" fill="{c}"/>' for x, y, c in bars)
        film += '<path d="M72,160.5 H184" stroke="#ffffff" stroke-width="1.6" opacity=".8"/>'
    else:
        film += scene(ic, 58, 70, 140, 104, True)
    body = (f'<path d="{d} {win}" fill-rule="evenodd" fill="{ic.lin([(0, "#fbfaf6"), (1, "#dcd6ca")])}" '
            f'stroke="#a39c8c" stroke-width="{12 if sm else 1.8:g}"/>')
    if not sm:
        body = g(body, edge_light(ic, 3, 3, 0.8, 24, 50, 0.6))
        body += (f'<path d="M62,74 H194 V81 H62 Z" fill="{ic.lin([(0, "#000", 0.45), (1, "#000", 0)])}"/>'
                 f'<path d="M62,74 H69 V170 H62 Z" fill="{ic.lin([(0, "#000", 0.35), (1, "#000", 0)], 0, 0, 1, 0)}"/>'
                 '<g fill="#8f887a" opacity=".8"><rect x="62" y="186" width="58" height="5" rx="2.5"/>'
                 '<rect x="62" y="196" width="38" height="4" rx="2"/><rect x="176" y="186" width="18" height="12" rx="2"/></g>'
                 '<circle cx="46" cy="46" r="3" fill="#b3ab9a"/>')
    return g(film + body, tf=tf)


def m_slides(ic):
    """slides in their mounts, the kind a projector takes"""
    inner = mount(ic, "translate(12 -8) rotate(8 128 128)", False)
    inner += mount(ic, "rotate(-5 128 128)", True)
    place(ic, inner, 128, 234, 96, 10, 0.3, (3, 4, 0.34))


def m_font(ic):
    """a block of wood type: an A, inked, standing out of its face. An A
    reads the same either way round, as type has to be cut."""
    sm = ic.small
    face = "M50,24 H206 Q216,24 216,34 V190 Q216,200 206,200 H50 Q40,200 40,190 V34 Q40,24 50,24 Z"
    side = ("M42,196 H214 Q214,200 213,204 L210,226 Q209,232 203,232 H53 Q47,232 46,226 "
            "L43,204 Q42,200 42,196 Z")
    blk = (f'<path d="{side}" fill="{ic.lin([(0, "#b27c45"), (1, "#7d5228")])}" stroke="#5f3c1b" '
           f'stroke-width="{12 if sm else 1.6:g}"/>')
    if not sm:
        blk = g(blk, grain(ic, 0.16, "0.12 0.012", 3, 21, "#3e230b", 0.5))
    fc = (f'<path d="{face}" fill="{ic.lin([(0, "#efd6a8"), (1, "#d3aa6e")])}" stroke="#8a5d2c" '
          f'stroke-width="{12 if sm else 1.6:g}"/>')
    if not sm:
        fc = g(g(fc, grain(ic, 0.14, "0.01 0.1", 3, 22, "#6b4316", 0.6)),
               edge_light(ic, 4, 4, 0.9, 28, 50, 0.55))
    a = ("M119,50 L137,50 L190,182 H202 V192 H150 V182 H160 L147.2,150 H97.9 L86,182 H96 V192 "
         "H56 V182 H70 Z M121.6,86.2 L141.5,136 H103.1 Z")
    letter = (f'<path d="{a}" fill-rule="evenodd" fill="{ic.lin([(0, "#3a312a"), (1, "#14100c")])}" '
              f'stroke="#0b0806" stroke-width="{8 if sm else 1:g}" transform="translate(0 -6)"/>')
    if not sm:
        letter = g(g(letter, edge_light(ic, 2, 3, 1.1, 30, 52, 0.9)), dropped(ic, 3, 2.5, 0.5))
    place(ic, blk + fc + letter, 128, 234, 96, 9, 0.32, (2, 3, 0.3))


def polaroid(ic, x, y, angle, sunset, seed):
    w, h = 156, 184
    fr = (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="2.5" '
          f'fill="{ic.lin([(0, "#fbfaf6"), (1, "#e9e5db")])}" stroke="#cdc6b5" '
          f'stroke-width="{12 if ic.small else 1.4:g}"/>')
    fr = g(fr, grain(ic, 0.04, "0.9", 2, seed, "#4a3a20"))
    px, py, pw, ph = x + 12, y + 12, w - 24, h - 56
    pic = scene(ic, px, py, pw, ph, sunset)
    if not ic.small:
        pic += (f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" fill="none" stroke="#000" '
                'stroke-width="1" opacity=".25"/>'
                f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" fill="'
                f'{ic.lin([(0, "#fff", 0.38), (0.45, "#fff", 0.06), (0.5, "#fff", 0)], 0, 0, 1, 1)}"/>')
    return g(g(fr + pic, dropped(ic, 2, 3, 0.3)),
             tf=f"rotate({angle:g} {x + w / 2:g} {y + h / 2:g})")


def m_image(ic):
    """two instant photographs, one fallen across the other"""
    inner = polaroid(ic, 62, 30, 9, True, 31) + polaroid(ic, 42, 38, -7, False, 32)
    ic.add(contact(ic, 128, 234, 96, 10, 0.28))
    ic.add(inner)


def record(ic, cx, cy, r):
    sm = ic.small
    s = f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{ic.rad([(0, "#303030"), (0.45, "#161616"), (1, "#080808")])}"/>'
    if not sm:
        grooves = []
        for i, rr in enumerate(30 + 3.1 * k for k in range(int((r - 34) / 3.1))):
            col, op = ("#000000", ".55") if i % 2 else ("#3a3a3a", ".4")
            grooves.append(f'<circle cx="{cx}" cy="{cy}" r="{rr:.1f}" fill="none" stroke="{col}" '
                           f'stroke-width=".9" opacity="{op}"/>')
        s += "".join(grooves)
        for a0 in (-62, 118):
            a1 = a0 + 34
            pts = [(cx + rr * math.cos(math.radians(a)), cy + rr * math.sin(math.radians(a)))
                   for a, rr in ((a0, r - 3), (a1, r - 3), (a1, 30), (a0, 30))]
            sheen = ic.rad([(0, "#fff", 0), (0.55, "#fff", 0.16), (1, "#fff", 0.02)], cx, cy, r, user=True)
            s += (f'<path d="M{pts[0][0]:.1f},{pts[0][1]:.1f} A{r - 3},{r - 3} 0 0 1 '
                  f'{pts[1][0]:.1f},{pts[1][1]:.1f} L{pts[2][0]:.1f},{pts[2][1]:.1f} '
                  f'A30,30 0 0 0 {pts[3][0]:.1f},{pts[3][1]:.1f} Z" fill="{sheen}"/>')
        s += (f'<circle cx="{cx}" cy="{cy}" r="{r - 1}" fill="none" stroke="#5a5a5a" '
              'stroke-width="1.2" opacity=".7"/>')
    s += f'<circle cx="{cx}" cy="{cy}" r="27" fill="{ic.rad([(0, "#f58a5e"), (1, "#bd3b1c")], 0.4, 0.35, 0.7)}"/>'
    if not sm:
        s += (f'<circle cx="{cx}" cy="{cy}" r="21" fill="none" stroke="#ffe6c8" stroke-width="1" opacity=".55"/>'
              f'<rect x="{cx - 12}" y="{cy - 14}" width="24" height="3.5" rx="1.7" fill="#ffe6c8" opacity=".8"/>'
              f'<rect x="{cx - 8}" y="{cy + 9}" width="16" height="3" rx="1.5" fill="#ffe6c8" opacity=".7"/>')
    s += f'<circle cx="{cx}" cy="{cy}" r="{8 if sm else 3.5}" fill="#0a0a0a"/>'
    return s


def m_audio(ic):
    """a record, half out of its paper sleeve"""
    sm = ic.small
    rec = record(ic, 172, 118, 78)
    sl = "M24,54 H170 V202 H24 Z"
    sleeve = (f'<path d="{sl}" fill="{ic.lin([(0, "#f1e4c6"), (1, "#d6be90")])}" stroke="#9a7f52" '
              f'stroke-width="{12 if sm else 1.6:g}"/>')
    art = "".join(f'<circle cx="97" cy="114" r="{r}" fill="{c}" opacity=".92"/>'
                  for r, c in ((58, "#d9662f"), (44, "#e9a23b"), (30, "#2f6f73"), (16, "#f1e4c6")))
    if not sm:
        art += ('<rect x="40" y="180" width="70" height="6" rx="3" fill="#5b3a1e" opacity=".8"/>'
                '<rect x="40" y="190" width="44" height="4" rx="2" fill="#5b3a1e" opacity=".55"/>')
        sleeve = g(sleeve + g(art, clip=ic.clip(f'<path d="{sl}"/>')),
                   grain(ic, 0.07, "0.7", 2, 41, "#4a2e10"))
        sleeve += '<path d="M170,56 V200" stroke="#fff6e0" stroke-width="1.6" opacity=".6"/>'
        shade = (f'<rect x="170" y="40" width="16" height="156" '
                 f'fill="{ic.lin([(0, "#000", 0.45), (1, "#000", 0)], 0, 0, 1, 0)}"/>')
        rec = g(rec + shade, clip=ic.clip('<circle cx="172" cy="118" r="78"/>'))
    else:
        sleeve += g(art, clip=ic.clip(f'<path d="{sl}"/>'))
    place(ic, rec + sleeve, 128, 214, 104, 10, 0.3, (3, 4, 0.34))


def slab(pts, x0, x1):
    """a convex shape cut to lie between x0 and x1 (Sutherland-Hodgman):
    what a clip would do, for an SVG drawn where clips are not"""
    for edge, inside in ((x0, lambda p: p[0] >= x0), (x1, lambda p: p[0] <= x1)):
        out = []
        for i, cur in enumerate(pts):
            prev = pts[i - 1]
            if inside(cur) != inside(prev):
                t = (edge - prev[0]) / (cur[0] - prev[0])
                out.append((edge, prev[1] + t * (cur[1] - prev[1])))
            if inside(cur):
                out.append(cur)
        pts = out
    return pts


def stripes(ic, x, y, w, h, slant=12, width=26):
    """a clapper stick: black and white, slanted"""
    s = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="#f4f2ec"/>'
    for i in range(-1, int(w / width) + 2, 2):
        x0 = x + i * width
        cut = slab([(x0, y), (x0 + width, y), (x0 + width - slant, y + h), (x0 - slant, y + h)], x, x + w)
        if len(cut) >= 3:
            s += f'<path d="{poly(*cut)}" fill="#1b1b1d"/>'
    return s


def m_video(ic):
    """a film slate: chalk on black, and its sticks about to clap"""
    sm = ic.small
    slate = (f'<rect x="30" y="108" width="196" height="120" rx="8" '
             f'fill="{ic.lin([(0, "#3e4248"), (1, "#1c1e22")])}" stroke="#0d0e10" '
             f'stroke-width="{12 if sm else 2:g}"/>')
    if not sm:
        slate = g(slate, edge_light(ic, 3, 3, 0.7, 24, 50, 0.45))
        chalk = ('<g stroke="#f4f2ea" stroke-width="2.2" opacity=".75" fill="none">'
                 '<path d="M40,150 H216 M40,182 H216 M40,206 H216 M128,150 V206 M86,182 V206"/></g>')
        rnd = random.Random(61)
        marks = []
        for (x0, x1, y) in ((46, 120, 130), (136, 208, 130), (46, 120, 166), (136, 208, 166),
                            (46, 80, 194), (92, 120, 194), (136, 208, 194), (46, 150, 218)):
            x = x0
            end = x0 + (x1 - x0) * rnd.uniform(0.5, 0.9)
            while x < end:
                w = min(rnd.uniform(6, 16), end - x)
                marks.append(f'<rect x="{x:.1f}" y="{y - 2.5:g}" width="{w:.1f}" height="5" rx="2.5"/>')
                x += w + 4
        slate += chalk + f'<g fill="#f4f2ea" opacity=".6">{"".join(marks)}</g>'
    else:
        slate += '<path d="M44,160 H212 M44,196 H212" stroke="#d8d6ce" stroke-width="12"/>'
    low = stripes(ic, 30, 88, 196, 22)
    top = g(stripes(ic, 30, 64, 196, 22), tf="rotate(-17 36 86)")
    if not sm:
        low = g(low, edge_light(ic, 2, 2, 0.6, 24, 50, 0.4))
        top = g(top, dropped(ic, 3, 2.5, 0.35))
    hinge = (f'<circle cx="38" cy="88" r="8" fill="{ic.rad(STEEL, 0.35, 0.3, 0.7)}" stroke="#5d6168" '
             f'stroke-width="{6 if sm else 1.4:g}"/>')
    place(ic, slate + low + top + hinge, 128, 232, 104, 9, 0.32, (3, 4, 0.36))


def m_package(ic):
    """a package: a treasure chest, its lid lifted on what is inside"""
    sm = ic.small
    sw = 12 if sm else 1.6
    oak = [(0, "#7a4a28"), (1, "#4a2a14")]
    body = (f'<rect x="34" y="122" width="188" height="104" rx="8" fill="{ic.lin(oak)}" stroke="#2a1608" '
            f'stroke-width="{sw}"/>')
    if not sm:
        body += ('<path d="M36,156 H220 M36,190 H220" stroke="#2a1608" stroke-width="2" opacity=".6"/>'
                 '<path d="M36,158 H220 M36,192 H220" stroke="#a87048" stroke-width="1.2" opacity=".5"/>')
        body = g(body, grain(ic, 0.16, "0.01 0.12", 3, 71, "#1a0c04", 0.5))
        # weed and wear: it has been down here a while
        body += ('<path d="M40,224 q10,-14 22,-4 t24,-2" fill="none" stroke="#3f7a32" stroke-width="5" '
                 'stroke-linecap="round" opacity=".8"/>')
    # what is inside, catching the light under the lid
    gold = (f'<path d="M44,124 Q128,96 212,124 Z" fill="{ic.rad([(0, "#fff6c0"), (0.5, "#f0c040"), (1, "#a8741a")], 0.5, 0.8, 0.7)}"/>')
    if not sm:
        for (x, y) in ((86, 116), (112, 108), (140, 106), (166, 112), (124, 118)):
            gold += (f'<ellipse cx="{x}" cy="{y}" rx="11" ry="5" fill="{ic.lin(BRASS, 0, 0, 0, 1)}" '
                     'stroke="#8a6420" stroke-width="1"/>')
    # the lid, domed, tipped up and back
    lid = (f'<path d="M30,106 Q128,20 226,106 L222,118 Q128,40 34,118 Z" fill="{ic.lin(oak)}" '
           f'stroke="#2a1608" stroke-width="{sw}" stroke-linejoin="round"/>'
           f'<path d="M30,106 Q128,20 226,106 Q128,62 30,106 Z" fill="{ic.lin([(0, "#8f5a32"), (1, "#5c341a")])}" '
           f'stroke="#2a1608" stroke-width="{sw}" stroke-linejoin="round"/>')
    if not sm:
        lid = g(lid, grain(ic, 0.16, "0.01 0.12", 3, 72, "#1a0c04", 0.5))
    # iron straps, rusted, and the brass hasp
    iron = ""
    for x in (70, 186):
        iron += (f'<rect x="{x - 9}" y="120" width="18" height="108" fill="{ic.lin([(0, "#5a4a42"), (1, "#2e2622")], 0, 0, 1, 0)}" '
                 f'stroke="#1a1410" stroke-width="{6 if sm else 1:g}"/>')
        if not sm:
            iron += "".join(f'<circle cx="{x}" cy="{y}" r="2.6" fill="#8a7a6a"/>' for y in (134, 174, 214))
            iron += f'<path d="M{x - 6},150 v30" stroke="#a0461e" stroke-width="3" opacity=".5"/>'
    hasp = (f'<rect x="114" y="130" width="28" height="34" rx="4" fill="{ic.lin(BRASS, 0, 0, 0, 1)}" '
            f'stroke="#6b4c18" stroke-width="{6 if sm else 1.2:g}"/>')
    if not sm:
        hasp += '<path d="M128,140 v10" stroke="#3a2a10" stroke-width="4" stroke-linecap="round"/>'
        hasp = g(hasp, edge_light(ic, 1.5, 2, 0.8, 22, 50, 0.6))
    place(ic, gold + lid + body + iron + hasp, 128, 228, 104, 10, 0.3, (3, 4, 0.34))


def card(ic, x, y, angle, seed, back=False):
    """a punched card: the clipped corner, the printed columns, the holes"""
    sm = ic.small
    w, h = 210, 104
    d = (f"M{x + 18},{y} H{x + w - 4} Q{x + w},{y} {x + w},{y + 4} V{y + h - 4} "
         f"Q{x + w},{y + h} {x + w - 4},{y + h} H{x + 4} Q{x},{y + h} {x},{y + h - 4} V{y + 18} Z")
    tone = [(0, "#efe4c6"), (1, "#dccb9f")] if back else [(0, "#f8f0d9"), (1, "#e8dab6")]
    s = (f'<path d="{d}" fill="{ic.lin(tone)}" stroke="#b9a77c" stroke-width="{12 if sm else 1.4:g}" '
         'stroke-linejoin="round"/>')
    if sm:
        s += "".join(f'<rect x="{x + 30 + 26 * i}" y="{y + 30 + (i * 37) % 50}" width="12" height="22" '
                     'fill="#3b2f1f"/>' for i in range(7))
        return g(s, tf=f"rotate({angle:g} {x + w / 2:g} {y + h / 2:g})")
    s = g(s, grain(ic, 0.04, "0.9", 2, seed, "#4a3a20"))
    rnd = random.Random(seed)
    cols = [x + 22 + 6.2 * i for i in range(30)]
    rows = [y + 22 + 8.4 * j for j in range(9)]
    s += f'<path d="M{x + 20},{y + 13} H{x + w - 8}" stroke="#c0533f" stroke-width="1.2" opacity=".6"/>'
    digits = "".join(f"M{cx:.1f},{ry - 1.6:.1f}h2v3.2h-2z" for cx in cols for ry in rows)
    s += f'<path d="{digits}" fill="#8a7a5a" opacity=".45"/>'
    holes = []
    for cx in cols:
        if rnd.random() < 0.7:
            for _ in range(rnd.choice((1, 1, 2))):
                ry = rnd.choice(rows)
                holes.append(f"M{cx - 0.8:.1f},{ry - 4:.1f}h3.6v8h-3.6z")
    s += f'<path d="{"".join(holes)}" fill="#3b2f1f" opacity=".88"/>'
    return g(g(s, dropped(ic, 2, 2.5, 0.28)), tf=f"rotate({angle:g} {x + w / 2:g} {y + h / 2:g})")


def m_exec(ic):
    """a program, as one once was: a deck of punched cards"""
    inner = card(ic, 30, 62, 7, 81, True) + card(ic, 18, 96, -6, 82)
    ic.add(contact(ic, 128, 222, 108, 12, 0.28))
    ic.add(inner)


MIMES = {
    "text-x-generic": m_text,
    "text-plain": m_text,
    "text-x-script": m_script,
    "application-x-shellscript": m_script,
    "text-html": m_html,
    "x-office-document": m_doc,
    "x-office-spreadsheet": m_sheet,
    "x-office-presentation": m_slides,
    "application-pdf": m_pdf,
    "font-x-generic": m_font,
    "image-x-generic": m_image,
    "audio-x-generic": m_audio,
    "video-x-generic": m_video,
    "package-x-generic": m_package,
    "application-x-executable": m_exec,
    "application-x-generic": m_unknown,
    "unknown": m_unknown,
}


# ---- places and things ---------------------------------------------------

def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def p_home(ic):
    """home: a spiral shell, and the hermit crab who lives in it looking out"""
    sm = ic.small
    sw = 12 if sm else 1.6
    shell = ""
    # the whorls, the tip of the spire first and the body whorl last, over it
    whorls = ((206, 54, 15), (190, 76, 26), (164, 108, 42), (118, 156, 74))
    for i, (cx, cy, r) in enumerate(whorls):
        fill = ic.rad([(0, "#fff4e6"), (0.55, "#f2c9a4"), (1, "#c9805a")], 0.38, 0.32, 0.75)
        shell += (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}" stroke="#8a4a2e" '
                  f'stroke-width="{sw}"/>')
        if not sm:
            # the bands that go round with the growth of it
            for k in (0.45, 0.72):
                shell += (f'<path d="M{cx - r * 0.86:.1f},{cy + r * 0.30:.1f} '
                          f'A{r * 0.95:.1f},{r * k:.1f} 0 0 0 {cx + r * 0.86:.1f},{cy - r * 0.30:.1f}" '
                          f'fill="none" stroke="#b5643f" stroke-width="{2.6 if i == 3 else 1.6:g}" opacity=".55"/>')
    if not sm:
        shell = g(shell, grain(ic, 0.05, "0.7", 2, 95, "#5a2a12"))
        # the light from above on its back
        shell += (f'<ellipse cx="104" cy="118" rx="34" ry="18" fill="'
                  f'{ic.rad([(0, "#fff", 0.55), (1, "#fff", 0)])}" transform="rotate(-30 104 118)"/>')
    # the mouth, turned toward us, pink inside
    mouth = (f'<ellipse cx="88" cy="190" rx="40" ry="30" transform="rotate(-24 88 190)" '
             f'fill="{ic.rad([(0, "#5a1e1e"), (0.55, "#c25a5a"), (1, "#f4b4a4")], 0.55, 0.5, 0.6)}" '
             f'stroke="#8a4a2e" stroke-width="{sw}"/>')
    # and who is at home: two eyes on stalks and a claw
    crab = ""
    for (ex, ey) in ((70, 160), (92, 152)):
        crab += (f'<path d="M{ex + 4},{ey + 26} L{ex},{ey}" stroke="#e2551c" '
                 f'stroke-width="{10 if sm else 4:g}" stroke-linecap="round"/>'
                 f'<circle cx="{ex}" cy="{ey}" r="{9 if sm else 7}" fill="#111"/>')
        if not sm:
            crab += f'<circle cx="{ex - 2}" cy="{ey - 2.5}" r="2.4" fill="#fff"/>'
    crab += (f'<path d="M62,200 Q40,196 36,214 Q46,206 54,214 Q48,222 58,226 Q74,222 74,206 Z" '
             f'fill="{ic.lin([(0, "#ff7a3a"), (1, "#c8400e")])}" stroke="#8a2a08" '
             f'stroke-width="{8 if sm else 1.4:g}" stroke-linejoin="round"/>')
    place(ic, shell + mouth + crab, 128, 228, 100, 10, 0.3, (3, 4, 0.32))


def pencil(ic):
    """a yellow pencil, sharpened, its eraser in a brass ferrule"""
    return (f'<path d="M0,-6 H96 V6 H0 Z" fill="'
            f'{ic.lin([(0, "#ffd75e"), (0.45, "#f3b72a"), (0.55, "#e2a21e"), (1, "#c4840f")])}"/>'
            '<path d="M0,-2 H96 M0,2 H96" stroke="#b07a10" stroke-width=".8" opacity=".6"/>'
            f'<path d="M96,-6 L118,0 L96,6 Z" fill="{ic.lin([(0, "#f2d3a6"), (1, "#c99b62")])}"/>'
            '<path d="M111,-2 L120,0 L111,2 Z" fill="#3a3a3a"/>'
            f'<rect x="-14" y="-6" width="14" height="12" fill="{ic.lin(BRASS)}"/>'
            '<path d="M-10,-6 V6 M-5,-6 V6" stroke="#8a6420" stroke-width=".8"/>'
            f'<path d="M-14,-6 H-24 Q-28,-6 -28,0 Q-28,6 -24,6 H-14 Z" '
            f'fill="{ic.lin([(0, "#f4a3a0"), (1, "#d4706c")])}"/>')


def p_desktop(ic):
    """the tank floor: a patch of rippled sand, a sheet lying on it, a
    pebble and a sprig of weed"""
    sm = ic.small
    sw = 12 if sm else 1.6
    topq = poly((58, 82), (198, 82), (246, 174), (10, 174))
    frontq = poly((10, 174), (246, 174), (246, 198), (10, 198))
    sand = (f'<path d="{topq}" fill="{ic.lin([(0, "#d9cfa8"), (1, "#efe6c4")])}" stroke="#8f8463" '
            f'stroke-width="{sw}" stroke-linejoin="round"/>')
    if not sm:
        sand = g(sand, grain(ic, 0.12, "1.1", 2, 101, "#5a5030", 0.9))
        rip = []
        for t in (0.18, 0.34, 0.5, 0.66, 0.82):
            p, q = lerp((58, 82), (10, 174), t), lerp((198, 82), (246, 174), t)
            mx, my = (p[0] + q[0]) / 2, (p[1] + q[1]) / 2
            rip.append(f"M{p[0]:.1f},{p[1]:.1f} Q{mx:.1f},{my - 5:.1f} {q[0]:.1f},{q[1]:.1f}")
        sand += f'<path d="{" ".join(rip)}" fill="none" stroke="#b7a97c" stroke-width="2" opacity=".6"/>'
        # the light, through the water, in a net on the sand
        sand += (f'<path d="M70,110 q20,-10 36,4 t40,-2 M40,150 q26,-12 50,2 t56,0 t50,-4" fill="none" '
                 'stroke="#fff" stroke-width="2.4" opacity=".35"/>')
    front = (f'<path d="{frontq}" fill="{ic.lin([(0, "#a99a6c"), (1, "#7a6d48")])}" stroke="#5a5030" '
             f'stroke-width="{sw}" stroke-linejoin="round"/>')
    if not sm:
        front = g(front, grain(ic, 0.12, "1.2", 2, 102, "#3a3018", 0.6))
    sheetq = poly((76, 102), (150, 96), (166, 150), (82, 158))
    sheet_ = f'<path d="{sheetq}" fill="{ic.lin(PAPER)}" stroke="{PAPER_EDGE}" stroke-width="{8 if sm else 1.2:g}"/>'
    if not sm:
        rows = []
        for t in (0.22, 0.36, 0.5, 0.64, 0.78):
            p, q = lerp((76, 102), (82, 158), t), lerp((150, 96), (166, 150), t)
            q = lerp(p, q, 0.8 if t < 0.7 else 0.5)
            p = lerp(p, q, 0.12)
            rows.append(f"M{p[0]:.1f},{p[1]:.1f} L{q[0]:.1f},{q[1]:.1f}")
        sheet_ += (f'<path d="{" ".join(rows)}" stroke="#5f7676" stroke-width="2.6" stroke-linecap="round" '
                   'opacity=".7"/>')
        sheet_ = g(sheet_, dropped(ic, 2, 2, 0.35))
    pebble = (f'<ellipse cx="192" cy="146" rx="18" ry="11" fill="{ic.rad([(0, "#8fa3a3"), (1, "#3f5656")], 0.35, 0.3, 0.8)}" '
              f'stroke="#2a3a3a" stroke-width="{8 if sm else 1.2:g}"/>')
    weed = ""
    for (x, lean, h) in ((40, -14, 64), (50, 6, 80), (60, 20, 58)):
        weed += (f'<path d="M{x},168 Q{x + lean * 0.3},{168 - h * 0.6} {x + lean},{168 - h}" fill="none" '
                 f'stroke="#2f8a3e" stroke-width="{12 if sm else 6:g}" stroke-linecap="round"/>')
    place(ic, front + sand + sheet_ + pebble + weed, 128, 206, 118, 8, 0.32, (2, 3, 0.3))


def helix(theta0, k, y0=62.0, y1=224.0, step=4.0):
    """a wire of the basket's mesh, round the cylinder as it goes down:
    its points, each with whether it is on the near side"""
    pts = []
    y = y0
    while y <= y1 + 0.01:
        t = (y - y0) / (y1 - y0)
        rr, ry = 82 + (60 - 82) * t, 20 + (13 - 20) * t
        th = theta0 + k * (y - y0)
        pts.append((128 + rr * math.sin(th), y + ry * math.cos(th), math.cos(th) >= 0))
        y += step
    return pts


def balls(ic, spec, sm):
    """screwed-up paper"""
    out = []
    for cx, cy, r, seed in spec:
        rnd = random.Random(seed * 7 + 1)
        n = 11
        pts = []
        for i in range(n):
            a = 2 * math.pi * i / n + rnd.uniform(-0.15, 0.15)
            rr = r * rnd.uniform(0.82, 1.06)
            pts.append((cx + rr * math.cos(a), cy + rr * 0.86 * math.sin(a)))
        out.append(f'<path d="{poly(*pts)}" '
                   f'fill="{ic.rad([(0, "#ffffff"), (0.6, "#ecebe6"), (1, "#c9c7c0")], 0.38, 0.32, 0.7)}" '
                   f'stroke="#9b998f" stroke-width="{8 if sm else 1.2:g}" stroke-linejoin="round"/>')
        if not sm:
            creases = []
            for i in range(0, n, 2):
                p = pts[i]
                q = lerp((cx, cy), pts[(i + 5) % n], rnd.uniform(0.1, 0.5))
                creases.append(f"M{p[0]:.1f},{p[1]:.1f} L{q[0]:.1f},{q[1]:.1f}")
            out.append(f'<path d="{" ".join(creases)}" stroke="#a9a69c" stroke-width="1.1" opacity=".75"/>')
            facet = poly(pts[1], pts[3], (cx + rnd.uniform(-4, 4), cy + rnd.uniform(-4, 4)))
            out.append(f'<path d="{facet}" fill="#ffffff" opacity=".55"/>')
    return "".join(out)


def basket(ic, full):
    """the trash: a crab pot, a net of rope on hoops of weathered wood,
    you see through to its far side, and its float tied on"""
    sm = ic.small
    body = "M46,62 L68,224 A60,13 0 0 0 188,224 L210,62 A82,20 0 0 1 46,62 Z"
    near, far = [], []
    if not sm:
        # each wire a line of its own, broken where it goes round the back
        for i in range(24):
            th0 = 2 * math.pi * i / 24
            for k in (0.0105, -0.0105):
                pts = helix(th0, k)
                run = [pts[0]]
                for p in pts[1:]:
                    if p[2] != run[-1][2]:
                        if len(run) > 1:
                            (near if run[0][2] else far).append(
                                "M" + " ".join(f"{q[0]:.1f},{q[1]:.1f}" for q in run))
                        run = [run[-1][:2] + (p[2],)]
                    run.append(p)
                if len(run) > 1:
                    (near if run[0][2] else far).append("M" + " ".join(f"{q[0]:.1f},{q[1]:.1f}" for q in run))
    s = f'<path d="{body}" fill="#0e2e2c" opacity="{0.55 if sm else 0.30:g}"/>'
    s += f'<ellipse cx="128" cy="62" rx="82" ry="20" fill="{ic.lin([(0, "#3f6a66"), (1, "#0e2e2c")])}"/>'
    if not sm:
        s += f'<path d="{" ".join(far)}" stroke="#b8a47c" stroke-width="1.3" opacity=".6"/>'
    if full:
        s += balls(ic, ((92, 56, 30, 1), (150, 48, 34, 2), (122, 70, 26, 3)), sm)
        if not sm:
            s += g(balls(ic, ((104, 150, 30, 4), (152, 176, 26, 5)), sm), op=0.55)
    if not sm:
        s += f'<path d="{" ".join(near)}" stroke="#8a7448" stroke-width="3.4" stroke-linecap="round"/>'
        s += (f'<path d="{" ".join(near)}" stroke="#f0e2bc" stroke-width="1" stroke-linecap="round" '
              'opacity=".55" transform="translate(-.6 -.8)"/>')
        s += (f'<path d="{body}" fill="'
              f'{ic.lin([(0, "#000", 0.45), (0.22, "#000", 0), (0.36, "#fff", 0.22), (0.5, "#fff", 0), (0.78, "#000", 0.12), (1, "#000", 0.5)], 0, 0, 1, 0)}"/>')
    else:
        s += '<path d="M76,96 L96,214 M128,100 V222 M180,96 L160,214" stroke="#8a7448" stroke-width="12"/>'
    rim = ic.lin([(0, "#c8a678"), (0.4, "#7a5a36"), (0.6, "#a8865a"), (1, "#5a4024")])
    s += (f'<ellipse cx="128" cy="62" rx="82" ry="20" fill="none" stroke="#3a2a16" stroke-width="{16 if sm else 8.5:g}"/>'
          f'<ellipse cx="128" cy="62" rx="82" ry="20" fill="none" stroke="{rim}" stroke-width="{8 if sm else 5.5:g}"/>'
          f'<path d="M68,224 A60,13 0 0 0 188,224" fill="none" stroke="#3a2a16" stroke-width="{14 if sm else 8:g}"/>'
          f'<path d="M68,224 A60,13 0 0 0 188,224" fill="none" stroke="{rim}" stroke-width="{7 if sm else 5:g}"/>')
    # its float, on a line from the rim
    s += (f'<path d="M200,64 Q222,70 224,92" fill="none" stroke="#c8b48a" stroke-width="{8 if sm else 2.2:g}"/>'
          f'<circle cx="224" cy="106" r="{18 if sm else 15}" fill="{ic.rad([(0, "#ffb0a0"), (0.6, "#e0402a"), (1, "#9a1a10")], 0.35, 0.3, 0.8)}" '
          f'stroke="#6a1008" stroke-width="{8 if sm else 1.2:g}"/>')
    if not sm:
        s += '<path d="M209,106 H239" stroke="#fff" stroke-width="5" opacity=".9"/>'
    place(ic, s, 128, 230, 72, 10, 0.34, (2, 3, 0.3))


def p_trash(ic):
    basket(ic, False)


def p_trash_full(ic):
    basket(ic, True)


def p_globe(ic):
    """a globe of the world on a brass stand"""
    sm = ic.small
    cx, cy, r = 128, 104, 76
    sea = (f'<circle cx="{cx}" cy="{cy}" r="{r}" '
           f'fill="{ic.rad([(0, "#9ad6f6"), (0.5, "#3687cb"), (1, "#0e3c70")], 0.36, 0.3, 0.75)}"/>')
    land = ("M70,70 C80,52 104,46 112,58 C118,68 106,76 110,86 C114,96 128,98 124,112 C120,124 104,122 100,136 "
            "C96,150 104,166 96,174 C86,170 80,150 74,136 C68,120 58,108 62,92 C64,82 66,78 70,70 Z "
            "M140,44 C152,38 170,42 178,52 C186,62 180,70 172,72 C164,74 160,82 166,90 C174,100 192,96 198,108 "
            "C202,120 192,138 180,148 C170,156 160,150 156,138 C152,126 158,118 150,110 C142,102 134,100 136,88 "
            "C138,76 150,72 148,62 C146,56 136,52 140,44 Z")
    earth = (f'<path d="{land}" fill="{ic.rad([(0, "#a9cf72"), (1, "#4f7d32")], 0.4, 0.35, 0.8)}" '
             f'stroke="#3d6426" stroke-width="{0 if sm else 1:g}"/>')
    lines = ""
    if not sm:
        earth = g(earth, grain(ic, 0.12, "0.25", 2, 111, "#2e4a1a", 0.6))
        lines = (f'<g fill="none" stroke="#ffffff" stroke-width=".9" opacity=".22">'
                 f'<ellipse cx="{cx}" cy="{cy}" rx="{r * 0.45:g}" ry="{r}"/>'
                 f'<ellipse cx="{cx}" cy="{cy}" rx="{r * 0.85:g}" ry="{r}"/>'
                 f'<path d="M{cx - r},{cy} H{cx + r} M{cx - r * 0.86:g},{cy - 38} H{cx + r * 0.86:g} '
                 f'M{cx - r * 0.86:g},{cy + 38} H{cx + r * 0.86:g}"/></g>')
    turned = g(earth + lines, tf=f"rotate(-18 {cx} {cy})",
               clip=ic.clip(f'<circle cx="{cx}" cy="{cy}" r="{r}"/>'))
    shade = (f'<circle cx="{cx}" cy="{cy}" r="{r}" '
             f'fill="{ic.rad([(0, "#001a33", 0), (0.62, "#001a33", 0.05), (1, "#001a33", 0.55)], 0.38, 0.32, 0.8)}"/>')
    if not sm:
        shade += (f'<ellipse cx="{cx - 24}" cy="{cy - 34}" rx="34" ry="22" '
                  f'transform="rotate(-30 {cx - 24} {cy - 34})" fill="{ic.rad([(0, "#fff", 0.75), (1, "#fff", 0)])}"/>')
    arc = f"M{cx},{cy - r - 12} A{r + 12},{r + 12} 0 0 1 {cx},{cy + r + 12}"
    meridian = (f'<path d="{arc}" fill="none" stroke="#6b4c18" stroke-width="{18 if sm else 10:g}" '
                f'transform="rotate(-18 {cx} {cy})"/>'
                f'<path d="{arc}" fill="none" stroke="{brass(ic)}" stroke-width="{10 if sm else 7:g}" '
                f'transform="rotate(-18 {cx} {cy})"/>')
    stand = (f'<path d="M120,190 H136 L140,214 H116 Z" fill="{brass(ic)}" stroke="#6b4c18" '
             f'stroke-width="{8 if sm else 1.2:g}"/>'
             f'<ellipse cx="128" cy="224" rx="56" ry="13" fill="{ic.lin(WALNUT)}" stroke="#2e1809" '
             f'stroke-width="{10 if sm else 1.4:g}"/>'
             f'<ellipse cx="128" cy="219" rx="52" ry="10" fill="{ic.lin([(0, "#9a653b"), (1, "#6b3f1f")])}"/>')
    if not sm:
        stand += f'<ellipse cx="128" cy="219" rx="52" ry="10" fill="none" stroke="{brass(ic)}" stroke-width="2.4"/>'
        stand = g(stand, edge_light(ic, 2, 2, 0.7, 24, 50, 0.4))
    place(ic, stand + sea + turned + shade + meridian, 128, 232, 70, 9, 0.34, (2, 3, 0.3))


def p_computer(ic):
    """the computer: a sonar set in a riveted steel case, like the tank's
    own Control Panel"""
    sm = ic.small
    cab = (f'<rect x="28" y="30" width="200" height="178" rx="22" '
           f'fill="{ic.lin([(0, "#4f8f8a"), (1, "#2a5753")])}" stroke="#123230" '
           f'stroke-width="{12 if sm else 1.8:g}"/>')
    if not sm:
        cab = g(g(cab, grain(ic, 0.05, "0.6", 2, 121, "#0c2422", 0.4)),
                edge_light(ic, 4, 4, 0.8, 26, 50, 0.5))
        for (x, y) in ((42, 44), (214, 44), (42, 194), (214, 194), (128, 40)):
            cab += (f'<circle cx="{x}" cy="{y}" r="4.2" fill="{ic.rad([(0, "#9fd0ca"), (1, "#2a5753")], 0.35, 0.3, 0.8)}" '
                    'stroke="#123230" stroke-width="1"/>')
        cab += '<path d="M150,64 v40 M84,170 v26" stroke="#8a4a24" stroke-width="2.4" opacity=".35"/>'
    bezel = (f'<circle cx="128" cy="106" r="66" fill="{ic.lin(BRASS, 0, 0, 0, 1)}" stroke="#6b4c18" '
             f'stroke-width="{8 if sm else 1.2:g}"/>')
    scr = (f'<circle cx="128" cy="106" r="56" '
           f'fill="{ic.rad([(0, "#2a6a3c"), (0.7, "#11301b"), (1, "#081a0e")], 0.5, 0.45, 0.65)}"/>')
    if not sm:
        rings = "".join(f'<circle cx="128" cy="106" r="{r}" fill="none"/>' for r in (14, 28, 42))
        scr += (f'<g stroke="#7dff9a" stroke-width="1.2" opacity=".45">{rings}'
                '<path d="M72,106 H184 M128,50 V162"/></g>'
                f'<path d="M128,106 L176,78 A56,56 0 0 1 184,106 Z" fill="{ic.lin([(0, "#7dff9a", 0.0), (1, "#7dff9a", 0.55)], 0, 1, 0, 0)}"/>'
                '<path d="M128,106 L176,78" stroke="#c8ffd4" stroke-width="2.4"/>'
                '<circle cx="160" cy="96" r="4" fill="#c8ffd4"/><circle cx="104" cy="128" r="3" fill="#7dff9a" opacity=".6"/>')
        scr += (f'<path d="M86,70 Q128,54 170,70 Q160,90 128,92 Q96,90 86,70 Z" '
                f'fill="{ic.lin([(0, "#fff", 0.30), (1, "#fff", 0)])}"/>')
        scr = g(scr, pressed(ic, 3, 3, "#000", 0.7), clip=ic.clip('<circle cx="128" cy="106" r="56"/>'))
    else:
        scr += '<circle cx="128" cy="106" r="28" fill="none" stroke="#7dff9a" stroke-width="10"/>'
    knobs = ""
    for x in (66, 190):
        knobs += (f'<circle cx="{x}" cy="188" r="{14 if sm else 9}" '
                  f'fill="{ic.rad([(0, "#5a5a5a"), (1, "#1a1a1a")], 0.4, 0.35, 0.7)}" stroke="#0a0a0a" '
                  f'stroke-width="{6 if sm else 1:g}"/>')
        if not sm:
            knobs += f'<path d="M{x},181 V187" stroke="#e8e8e8" stroke-width="1.6" stroke-linecap="round"/>'
    plate = (f'<rect x="98" y="181" width="60" height="14" rx="2" fill="{ic.lin(BRASS, 0, 0, 0, 1)}" '
             f'stroke="#6b4c18" stroke-width="{6 if sm else 1:g}"/>')
    if not sm:
        plate = g(plate + '<rect x="108" y="186" width="40" height="3.2" rx="1.6" fill="#6b4c18" opacity=".7"/>',
                  edge_light(ic, 1.5, 2, 0.8, 22, 50, 0.6))
    feet = ('<rect x="48" y="206" width="26" height="10" rx="5" fill="#111"/>'
            '<rect x="182" y="206" width="26" height="10" rx="5" fill="#111"/>')
    place(ic, feet + cab + bezel + scr + knobs + plate, 128, 220, 102, 9, 0.32, (3, 4, 0.34))


def screw(ic, x, y, r=5.0):
    return (f'<circle cx="{x}" cy="{y}" r="{r}" fill="{ic.rad(STEEL, 0.35, 0.3, 0.8)}" stroke="#6d737b" '
            'stroke-width="1"/>'
            f'<path d="M{x - r * 0.55:g},{y} H{x + r * 0.55:g} M{x},{y - r * 0.55:g} V{y + r * 0.55:g}" '
            'stroke="#555b63" stroke-width="1.4"/>')


def p_drive(ic, system=False):
    """a drive in its aluminium case: the bulge over the platters, the
    screws, the maker's label"""
    sm = ic.small
    case = (f'<rect x="22" y="48" width="212" height="152" rx="12" fill="{ic.lin(STEEL, 0, 0, 1, 1)}" '
            f'stroke="#6f757d" stroke-width="{12 if sm else 1.6:g}"/>')
    edge = (f'<path d="M22,190 V202 Q22,214 34,214 H222 Q234,214 234,202 V190 Z" '
            f'fill="{ic.lin([(0, "#9aa1a9"), (1, "#5d636b")])}" stroke="#4d525a" '
            f'stroke-width="{10 if sm else 1.4:g}"/>')
    if not sm:
        case = g(g(case, grain(ic, 0.08, "0.003 0.7", 2, 131, "#3a4048", 0.9)),
                 edge_light(ic, 3, 3, 0.8, 26, 50, 0.5))
        edge += "".join(f'<rect x="{x}" y="200" width="{w}" height="7" rx="1" fill="#2b2f34"/>'
                        for x, w in ((150, 34), (190, 22)))
    dome = (f'<circle cx="88" cy="122" r="54" '
            f'fill="{ic.rad([(0, "#f2f4f6"), (0.8, "#c3c9d0"), (1, "#a2a9b1")], 0.4, 0.35, 0.7)}" '
            f'stroke="#8a9098" stroke-width="{8 if sm else 1.2:g}"/>')
    if not sm:
        dome = g(dome + "".join(f'<circle cx="88" cy="122" r="{rr}" fill="none" stroke="#fff" '
                                'stroke-width=".7" opacity=".25"/>' for rr in range(12, 54, 6)),
                 edge_light(ic, 5, 5, 0.9, 28, 52, 0.55))
        dome += screw(ic, 88, 122, 7)
    label = (f'<rect x="146" y="60" width="78" height="120" rx="4" '
             f'fill="{ic.lin([(0, "#fdfdfb"), (1, "#e9e7e0")])}" stroke="#b5b2a8" '
             f'stroke-width="{8 if sm else 1:g}"/>')
    band = "#a8783a" if system else "#3d6ea8"
    if not sm:
        rnd = random.Random(133)
        bars, x = [], 154
        while x < 214:
            w = rnd.choice((1.1, 1.1, 2, 3))
            bars.append(f'<rect x="{x:.1f}" y="156" width="{w:g}" height="14"/>')
            x += w + rnd.choice((1.1, 1.8, 2.4))
        label += (f'<rect x="146" y="60" width="78" height="14" rx="4" fill="{band}"/>'
                  '<rect x="146" y="70" width="78" height="4" fill="#fdfdfb"/>'
                  + typed(154, 214, 86, 6, 9, 3.2, "#55524a", 134)
                  + f'<g fill="#2b2a26">{"".join(bars)}</g>')
        if system:
            label += seal(ic, 210, 92, 9)
    else:
        label += f'<rect x="146" y="60" width="78" height="30" fill="{band}"/>'
    screws = "" if sm else "".join(screw(ic, x, y) for x, y in ((36, 62), (220, 62), (36, 176), (130, 176)))
    place(ic, edge + case + dome + label + screws, 128, 218, 108, 9, 0.3, (3, 4, 0.34))


def p_drive_system(ic):
    p_drive(ic, True)


def p_usb(ic):
    """a memory stick in a rubber case, for the wet: the steel plug out"""
    sm = ic.small
    body = (f'<rect x="-70" y="-30" width="128" height="60" rx="16" '
            f'fill="{ic.lin([(0, "#3f6a66"), (0.5, "#24423f"), (1, "#122624")])}" stroke="#0a1817" '
            f'stroke-width="{12 if sm else 1.6:g}"/>')
    if not sm:
        body = g(body, edge_light(ic, 4, 4, 0.6, 18, 50, 0.4))
        body += ('<path d="M-44,-22 V22 M-30,-22 V22 M-16,-22 V22" stroke="#0a1817" stroke-width="3" '
                 'opacity=".45"/>')
        body += '<circle cx="-56" cy="0" r="7" fill="#0a1817" stroke="#000" stroke-width="1.2"/>'
    collar = (f'<rect x="54" y="-26" width="14" height="52" rx="2" fill="{brass(ic, True)}" stroke="#6b4c18" '
              f'stroke-width="{8 if sm else 1.2:g}"/>')
    plug = (f'<rect x="66" y="-22" width="52" height="44" rx="2" fill="{ic.lin(STEEL)}" stroke="#5d636b" '
            f'stroke-width="{8 if sm else 1.4:g}"/>')
    if not sm:
        plug += ('<rect x="80" y="-14" width="10" height="9" fill="#2b2f34"/>'
                 '<rect x="98" y="-14" width="10" height="9" fill="#2b2f34"/>'
                 '<path d="M68,-19 H116" stroke="#fff" stroke-width="1.4" opacity=".8"/>')
        plug = g(plug, edge_light(ic, 2, 2, 0.8, 24, 50, 0.5))
    place(ic, g(body + collar + plug, tf="translate(124 130) rotate(-32)"), 128, 206, 92, 12, 0.3, (3, 4, 0.36))


def p_optical(ic):
    """a disc, the light breaking into colours across it"""
    sm = ic.small
    cx, cy, r = 128, 122, 104
    disc_d = (f"M{cx - r},{cy} A{r},{r} 0 1 0 {cx + r},{cy} A{r},{r} 0 1 0 {cx - r},{cy} Z "
              f"M{cx - 10},{cy} A10,10 0 1 1 {cx + 10},{cy} A10,10 0 1 1 {cx - 10},{cy} Z")
    s = (f'<path d="{disc_d}" fill-rule="evenodd" '
         f'fill="{ic.rad([(0, "#f5f7fa"), (0.7, "#d0d5dc"), (1, "#b2b8c1")])}" stroke="#8d949e" '
         f'stroke-width="{12 if sm else 1.6:g}"/>')
    clip = ic.clip(f'<path d="{disc_d}" clip-rule="evenodd"/>')

    def wedge(a0, a1, fill, r0=38, r1=r - 2):
        p = [(cx + rr * math.cos(math.radians(a)), cy + rr * math.sin(math.radians(a)))
             for a, rr in ((a0, r1), (a1, r1), (a1, r0), (a0, r0))]
        return (f'<path d="M{p[0][0]:.1f},{p[0][1]:.1f} A{r1},{r1} 0 0 1 {p[1][0]:.1f},{p[1][1]:.1f} '
                f'L{p[2][0]:.1f},{p[2][1]:.1f} A{r0},{r0} 0 0 0 {p[3][0]:.1f},{p[3][1]:.1f} Z" fill="{fill}"/>')
    rain = [(0, "#ff5b5b", 0.0), (0.2, "#ffb347", 0.42), (0.4, "#f8f06a", 0.45), (0.6, "#6be38a", 0.45),
            (0.8, "#5aa8ff", 0.42), (1, "#b06bff", 0.0)]
    if not sm:
        colours = ""
        for a0 in (-80, 100):
            colours += wedge(a0, a0 + 46, ic.lin(rain, 0, 0, 1, 1))
            colours += wedge(a0 + 6, a0 + 34, ic.lin(rain[::-1], 1, 0, 0, 1))
        for a0 in (-30, 150):
            colours += wedge(a0, a0 + 22, ic.lin([(0, "#fff", 0), (0.5, "#fff", 0.6), (1, "#fff", 0)], 0, 0, 1, 0))
        colours = g(colours, clip=clip)
    else:
        colours = wedge(-70, -20, "#7fc4ff") + wedge(110, 160, "#ffd27f")
    ring = (f"M{cx - 36},{cy} A36,36 0 1 0 {cx + 36},{cy} A36,36 0 1 0 {cx - 36},{cy} Z "
            f"M{cx - 10},{cy} A10,10 0 1 1 {cx + 10},{cy} A10,10 0 1 1 {cx - 10},{cy} Z")
    hub = (f'<path d="{ring}" fill-rule="evenodd" fill="#dfe3e8" opacity=".6"/>'
           f'<circle cx="{cx}" cy="{cy}" r="36" fill="none" stroke="#9aa1ab" stroke-width="{6 if sm else 1:g}"/>')
    if not sm:
        hub += (f'<circle cx="{cx}" cy="{cy}" r="27" fill="none" stroke="#ffffff" stroke-width="2" opacity=".8"/>'
                f'<circle cx="{cx}" cy="{cy}" r="10" fill="none" stroke="#7d848e" stroke-width="1.2"/>')
    place(ic, s + colours + hub, 128, 230, 92, 9, 0.3, (3, 4, 0.32))


def p_floppy(ic):
    """a 3.5-inch diskette: its steel shutter, a label written on in ink"""
    sm = ic.small
    d = "M42,30 H200 L226,56 V214 Q226,226 214,226 H42 Q30,226 30,214 V42 Q30,30 42,30 Z"
    body = (f'<path d="{d}" fill="{ic.lin([(0, "#4a5464"), (1, "#262b33")])}" stroke="#15181d" '
            f'stroke-width="{12 if sm else 1.6:g}" stroke-linejoin="round"/>')
    if not sm:
        body = g(body, edge_light(ic, 4, 3, 0.8, 26, 50, 0.45))
        body += ('<rect x="62" y="31" width="132" height="74" fill="#1f242b" opacity=".55"/>'
                 '<path d="M44,40 L53,49 H44 Z" fill="#8c95a3" opacity=".7"/>')
    shutter = (f'<rect x="80" y="30" width="96" height="72" fill="{ic.lin(STEEL, 0, 0, 1, 0)}" stroke="#6f757d" '
               f'stroke-width="{8 if sm else 1.2:g}"/>'
               '<rect x="136" y="40" width="22" height="50" rx="2" fill="#2a221c"/>')
    if not sm:
        shutter = g(g(shutter, grain(ic, 0.08, "0.003 0.7", 2, 151, "#3a4048", 0.9)),
                    edge_light(ic, 2, 2, 0.8, 24, 50, 0.5))
    label = (f'<rect x="52" y="124" width="152" height="94" rx="4" '
             f'fill="{ic.lin([(0, "#fdfcf8"), (1, "#ebe7dc")])}" stroke="#b9b3a3" stroke-width="{8 if sm else 1:g}"/>')
    if not sm:
        label += ('<rect x="52" y="124" width="152" height="12" rx="4" fill="#c8553f" opacity=".85"/>'
                  '<rect x="52" y="132" width="152" height="4" fill="#fdfcf8"/>'
                  '<path d="M60,168 H196 M60,192 H196 M60,208 H196" stroke="#9fb3d6" stroke-width=".8" opacity=".8"/>'
                  '<path d="M64,160 c6,-8 12,-8 14,-2 s4,8 10,2 s8,-10 12,-4 s6,6 12,0 s8,-6 14,-1 '
                  'M64,184 c5,-6 10,-6 12,-1 s5,6 10,1 s8,-8 12,-2" fill="none" stroke="#1f3f8a" '
                  'stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>')
        label = g(label, grain(ic, 0.04, "0.9", 2, 152, "#4a3a20"))
    holes = ('<rect x="37" y="204" width="11" height="12" rx="1.5" fill="#0d0f12"/>'
             '<rect x="208" y="204" width="11" height="12" rx="1.5" fill="#0d0f12"/>')
    place(ic, body + shutter + label + holes, 128, 230, 104, 9, 0.32, (3, 4, 0.34))


def p_flash(ic):
    """a memory card: the clipped corner, its label, the little lock"""
    sm = ic.small
    d = "M70,30 H168 L196,58 V214 Q196,226 184,226 H70 Q58,226 58,214 V42 Q58,30 70,30 Z"
    card = (f'<path d="{d}" fill="{ic.lin([(0, "#3a404b"), (1, "#1a1d23")])}" stroke="#0f1115" '
            f'stroke-width="{12 if sm else 1.6:g}" stroke-linejoin="round"/>')
    if not sm:
        card = g(card, edge_light(ic, 4, 3, 0.8, 26, 50, 0.45))
        card += '<path d="M76,40 H150 M76,47 H150 M76,54 H150" stroke="#5d6571" stroke-width="2.2" opacity=".85"/>'
    label = f'<rect x="70" y="72" width="114" height="140" rx="6" fill="{ic.lin([(0, "#3a7cc0"), (1, "#173f6e")])}"/>'
    label += f'<rect x="70" y="150" width="114" height="22" fill="{ic.lin(BRASS, 0, 0, 0, 1)}"/>'
    if not sm:
        label += ('<rect x="84" y="90" width="58" height="16" rx="3" fill="#ffffff" opacity=".92"/>'
                  '<rect x="84" y="114" width="80" height="6" rx="3" fill="#ffffff" opacity=".6"/>'
                  '<rect x="84" y="126" width="52" height="6" rx="3" fill="#ffffff" opacity=".45"/>'
                  '<rect x="84" y="186" width="44" height="7" rx="3.5" fill="#ffffff" opacity=".6"/>'
                  f'<rect x="70" y="72" width="114" height="140" rx="6" '
                  f'fill="{ic.lin([(0, "#fff", 0.32), (0.4, "#fff", 0.04), (0.45, "#fff", 0)], 0, 0, 1, 1)}"/>')
    lock = (f'<rect x="50" y="94" width="12" height="28" rx="2" fill="{ic.lin([(0, "#f4f4f2"), (1, "#c9c9c4")])}" '
            f'stroke="#55595f" stroke-width="{6 if sm else 1:g}"/>')
    place(ic, card + label + lock, 128, 230, 76, 9, 0.32, (3, 4, 0.34))


PLACES = {
    "user-home": p_home,
    "user-desktop": p_desktop,
    "user-trash": p_trash,
    "user-trash-full": p_trash_full,
    "network-workgroup": p_globe,
    "network-server": p_globe,
}
DEVICES = {
    "computer": p_computer,
    "drive-harddisk": p_drive,
    "drive-harddisk-system": p_drive_system,
    "drive-removable-media": p_usb,
    "drive-removable-media-usb": p_usb,
    "media-removable": p_usb,
    "media-optical": p_optical,
    "drive-optical": p_optical,
    "media-floppy": p_floppy,
    "media-flash": p_flash,
}
# what GIO asks for by a more particular name, with no general one after it
# that the theme has: a disc image, a Windows program
MORE_MIMES = {
    "application-x-cd-image": p_optical,
    "application-x-ms-dos-executable": m_exec,
}


def write(sub, name, draw):
    for small, top in ((False, "scalable"), (True, "16x16")):
        ic = Icon(small)
        draw(ic)
        d = os.path.join(OUT, top, sub)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, name + ".svg"), "w", encoding="utf-8") as f:
            f.write(ic.svg())


INDEX = """[Icon Theme]
Name=AquaDE
Comment=Sea-green folders, plastic sheets, shells and brass: the icons of the AquaDE tank
Inherits=Adwaita,AdwaitaLegacy,hicolor
Example=folder
Directories=16x16/places,16x16/mimetypes,16x16/devices,scalable/places,scalable/mimetypes,scalable/devices

[16x16/places]
Context=Places
Size=16
MinSize=8
MaxSize=31
Type=Scalable

[16x16/mimetypes]
Context=MimeTypes
Size=16
MinSize=8
MaxSize=31
Type=Scalable

[16x16/devices]
Context=Devices
Size=16
MinSize=8
MaxSize=31
Type=Scalable

[scalable/places]
Context=Places
Size=64
MinSize=32
MaxSize=512
Type=Scalable

[scalable/mimetypes]
Context=MimeTypes
Size=64
MinSize=32
MaxSize=512
Type=Scalable

[scalable/devices]
Context=Devices
Size=64
MinSize=32
MaxSize=512
Type=Scalable
"""


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    n = 0
    for name, em in FOLDERS.items():
        write("places", name, lambda ic, em=em: folder(ic, em))
        n += 1
    # open, and as one is while something is dragged over it
    for name in ("folder-open", "folder-drag-accept"):
        write("places", name, lambda ic: folder(ic, None, open_=True))
        n += 1
    for name, draw in PLACES.items():
        write("places", name, draw)
        n += 1
    for name, draw in DEVICES.items():
        write("devices", name, draw)
        n += 1
    for name, draw in list(MIMES.items()) + list(MORE_MIMES.items()):
        write("mimetypes", name, draw)
        n += 1
    with open(os.path.join(OUT, "index.theme"), "w", encoding="utf-8") as f:
        f.write(INDEX)
    print("wrote %d icons, twice each, to %s" % (n, OUT))


if __name__ == "__main__":
    main()

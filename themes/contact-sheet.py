#!/usr/bin/env python3
"""contact-sheet.py OUT.png [SIZE...] -- every icon of themes/icons/AquaDE at
each size (64 48 32 22 16 by default), drawn by gdk-pixbuf (librsvg, as GTK
draws them) on the desk's paper, for looking at them all at once. The
16x16 drawings are used up to 31 px, as the theme says."""

import os
import sys

import cairo
import gi

gi.require_version("GdkPixbuf", "2.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GdkPixbuf  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
THEME = os.path.join(HERE, "icons", "AquaDE")


def main():
    out = sys.argv[1]
    sizes = [int(a) for a in sys.argv[2:]] or [64, 48, 32, 22, 16]
    names = []
    for ctx in ("places", "devices", "mimetypes"):
        d = os.path.join(THEME, "scalable", ctx)
        names += [(ctx, f[:-4]) for f in sorted(os.listdir(d)) if f.endswith(".svg")]
    cols = 8
    cell = sum(sizes) + 12 * len(sizes) + 8
    rows = (len(names) + cols - 1) // cols
    cw, ch = 210, max(sizes) + 30
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, cols * cw, rows * ch + 10)
    cr = cairo.Context(surf)
    cr.set_source_rgb(0.95, 0.92, 0.85)
    cr.paint()
    for i, (ctx, name) in enumerate(names):
        x0, y0 = (i % cols) * cw + 6, (i // cols) * ch + 6
        x = x0
        for s in sizes:
            sub = "16x16" if s < 32 else "scalable"
            pb = GdkPixbuf.Pixbuf.new_from_file_at_size(
                os.path.join(THEME, sub, ctx, name + ".svg"), s, s)
            Gdk.cairo_set_source_pixbuf(cr, pb, x, y0 + max(sizes) - s)
            cr.paint()
            x += s + 6
        cr.set_source_rgb(0.25, 0.18, 0.1)
        cr.select_font_face("DejaVu Sans")
        cr.set_font_size(9)
        cr.move_to(x0, y0 + max(sizes) + 14)
        cr.show_text(name)
    del cell
    surf.write_to_png(out)
    print(out)


if __name__ == "__main__":
    main()

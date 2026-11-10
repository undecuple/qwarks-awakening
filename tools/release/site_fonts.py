#!/usr/bin/env python3
"""Make the project page's web fonts from the launcher's Adwaita fonts.

    nix shell --impure --expr 'with import <nixpkgs> {}; python3.withPackages (p: [p.fonttools p.brotli])' \
        --command python3 -I tools/release/site_fonts.py

Reads launcher/fonts/ (the static instances made by tools/release/font_instances.py and Adwaita Mono Regular, SIL OFL
1.1, no Reserved Font Name), keeps the Latin range below plus the few signs the page uses, and writes WOFF2 files to
public/site/fonts/ with the licence text copied unchanged. Subsetting and the format change make each a Modified Version
under OFL 1.1 section 3 (allowed: the licence reserves no name).

  adwaita-sans-latin-400.woff2   AdwaitaSans-Regular.ttf    body text
  adwaita-sans-latin-600.woff2   AdwaitaSans-SemiBold.ttf   buttons, chips, row titles
  adwaita-sans-latin-700.woff2   AdwaitaSans-Bold.ttf       header bar title, headings
  adwaita-mono-latin-400.woff2   AdwaitaMono-Regular.ttf    code
  OFL-adwaita.txt                OFL-adwaita.txt            (copied)
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "launcher", "fonts")
OUT = os.path.join(ROOT, "public", "site", "fonts")
FONTS = (("AdwaitaSans-Regular.ttf", "adwaita-sans-latin-400.woff2"),
         ("AdwaitaSans-SemiBold.ttf", "adwaita-sans-latin-600.woff2"),
         ("AdwaitaSans-Bold.ttf", "adwaita-sans-latin-700.woff2"),
         ("AdwaitaMono-Regular.ttf", "adwaita-mono-latin-400.woff2"))
# Basic Latin + Latin-1, the common Latin extras (as Google Fonts' "latin" range), arrows and the status glyphs
# (check mark, circles, triangle) when the font has them.
UNICODES = "U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+2000-206F,U+2074,U+20AC,U+2122," \
           "U+2190-2199,U+2212,U+2215,U+25B6,U+25CB,U+25D0,U+2713"


def main():
    from fontTools import subset                           # fontTools + brotli (see the docstring's shell)
    os.makedirs(OUT, exist_ok=True)
    for src, dst in FONTS:
        opts = subset.Options()
        opts.flavor = "woff2"
        opts.layout_features = ["*"]
        opts.name_IDs = ["*"]                               # keep the copyright and licence entries
        opts.notdef_outline = True
        font = subset.load_font(os.path.join(SRC, src), opts)
        s = subset.Subsetter(opts)
        s.populate(unicodes=subset.parse_unicodes(UNICODES))
        s.subset(font)
        path = os.path.join(OUT, dst)
        subset.save_font(font, path, opts)
        print("wrote %s (%d bytes)" % (dst, os.path.getsize(path)))
    shutil.copyfile(os.path.join(SRC, "OFL-adwaita.txt"), os.path.join(OUT, "OFL-adwaita.txt"))
    print("wrote OFL-adwaita.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Make the launcher's Adwaita fonts from GNOME's adwaita-fonts release.

    nix develop .#re --command python3 -I tools/release/font_instances.py adwaita-fonts-51.0.tar.xz [OUTDIR]

The release (https://download.gnome.org/sources/adwaita-fonts/51/adwaita-fonts-51.0.tar.xz, SIL OFL 1.1, no Reserved
Font Name) ships Adwaita Sans as one variable font (axes opsz 14-32, wght 100-900). Dear ImGui's font loader reads only
a variable font's default instance, so this tool cuts static instances from it with fontTools' instancer (a Modified
Version under OFL 1.1 section 3; allowed since the licence reserves no name) and copies Adwaita Mono Regular and the
licence unchanged:

  AdwaitaSans-Regular.ttf   opsz 14, wght 400   body text, row titles
  AdwaitaSans-SemiBold.ttf  opsz 14, wght 600   buttons, switches' labels
  AdwaitaSans-Bold.ttf      opsz 14, wght 700   header bar title, headings
  AdwaitaMono-Regular.ttf   (copied)            paths and versions
  OFL-adwaita.txt           the release's LICENSE (copied)

The tarball's SHA-256 is checked first (pinned below); a different file is refused. OUTDIR defaults to launcher/fonts.
"""
import hashlib
import io
import os
import sys
import tarfile

RELEASE = "adwaita-fonts-51.0"
SHA256 = "fa104ae2c1b96580d322f563ffe8b2dfca5296b0eca4971efd2d8011405123d2"
INSTANCES = (("AdwaitaSans-Regular.ttf", 400, "Regular"), ("AdwaitaSans-SemiBold.ttf", 600, "SemiBold"),
             ("AdwaitaSans-Bold.ttf", 700, "Bold"))
COPIES = ((f"{RELEASE}/mono/AdwaitaMono-Regular.ttf", "AdwaitaMono-Regular.ttf"),
          (f"{RELEASE}/LICENSE", "OFL-adwaita.txt"))
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "..", "launcher", "fonts"))


def main(argv):
    if len(argv) not in (1, 2):
        print(__doc__, file=sys.stderr)
        return 2
    tar_path, out = argv[0], argv[1] if len(argv) == 2 else DEFAULT_OUT
    with open(tar_path, "rb") as f:
        data = f.read()
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        print(f"font_instances: {tar_path} has SHA-256 {got}, expected {SHA256} ({RELEASE}.tar.xz)", file=sys.stderr)
        return 1
    from fontTools.ttLib import TTFont                      # only needed past the checks (nix develop .#re has it)
    from fontTools.varLib import instancer
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as t:
        def member(name):
            return t.extractfile(t.getmember(name)).read()
        var = member(f"{RELEASE}/sans/AdwaitaSans-Regular.ttf")
        os.makedirs(out, exist_ok=True)
        for name, wght, style in INSTANCES:
            font = TTFont(io.BytesIO(var))
            inst = instancer.instantiateVariableFont(font, {"opsz": 14, "wght": wght}, updateFontNames=True)
            inst.save(os.path.join(out, name))
            print(f"wrote {name} (wght {wght}, {style})")
        for src, dst in COPIES:
            with open(os.path.join(out, dst), "wb") as f:
                f.write(member(src))
            print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

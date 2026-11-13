#!/usr/bin/env python3
"""Host test: the files tools/release/names.py cannot reach repeat its names exactly.

R&C version of the Killzone 3 port's tools/release/test_names.py (same author, MIT). Checks the public README, NOTICE,
CITATION.cff and .recomp.json against names.py, the release asset names (shape, and that release.yml and pack_zip.py
take them from names.py), and, where it exists, that installer_art.py takes its names from names.py. Needs no game
data; runs from the repo (public/ present) or from the public tree (public/ is the root there).

    python3 tools/release/test_names.py
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import names  # noqa: E402

ROOT = HERE.parents[1]
PUB = ROOT / "public" if (ROOT / "public").is_dir() else ROOT
FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def read(p):
    try:
        return p.read_text(encoding="utf-8")
    except OSError:
        return None


def main():
    check(names.DESCRIPTION.startswith(names.TAG + " ") and "//" in names.TAG, "DESCRIPTION carries the TAG")
    check(names.REPO_URL == f"https://github.com/{names.OWNER}/{names.REPO_SLUG}", "REPO_URL is OWNER/REPO_SLUG")
    check(" ".join(names.WORDMARK).replace("'", "").lower() == names.PRODUCT.replace("'", "").lower(),
          "WORDMARK spells PRODUCT")
    for rel, needles in (("README.md", (f"# {names.PRODUCT}", names.DESCRIPTION)),
                         ("NOTICE.md", (names.REPO_URL, f'"{names.PRODUCT} by {names.OWNER}"')),
                         ("CITATION.cff", (f'title: "{names.PRODUCT}"', names.REPO_URL, names.DESCRIPTION)),
                         (".recomp.json", (f'"project": "{names.PRODUCT}"', names.REPO_URL))):
        text = read(PUB / rel)
        for n in needles:
            check(text is not None and n in text, f"public {rel} has {n!r}")
    art = read(HERE / "installer_art.py")
    if art is None:
        print("skip installer_art.py (not in this tree)")
    else:
        for use in ("names.WORDMARK", "names.REPO_URL", "names.TAG"):
            check(use in art, f"installer_art.py takes {use} from names.py")
    # release assets
    z = names.launcher_zip_name("1.2.3")
    check(z == "Qwarks-Awakening-1.2.3-launcher-windows-x64.zip", "launcher_zip_name shape", )
    check(names.ASSET_STEM == "-".join(names.PRODUCT.replace("'", "").split()), "ASSET_STEM is PRODUCT without the apostrophe")
    check(all(c.isalnum() or c in "-._" for c in z + names.LAUNCHER_EXE + names.launcher_zip_top("1.2.3")),
          "asset, exe and zip folder names are plain ASCII (no spaces or quotes)")
    check(names.LAUNCHER_EXE.lower().endswith(".exe") and names.LAUNCHER_EXE.startswith(names.ASSET_STEM),
          "LAUNCHER_EXE starts with ASSET_STEM and ends in .exe")
    check(names.launcher_zip_top("1.2.3").startswith(names.ASSET_STEM + "-1.2.3"), "zip top folder names the version")
    s = names.setup_exe_name("1.2.3")
    check(s == "Qwarks-Awakening-1.2.3-setup-windows-x64.exe", "setup_exe_name shape (the installer)")
    check(all(c.isalnum() or c in "-._" for c in s), "setup exe name is plain ASCII (no spaces or quotes)")
    check(s not in (z, names.build_zip_name("1.2.3"), names.symbols_zip_name("1.2.3")), "setup exe name differs from the zips")
    check(re.fullmatch(r"\d+\.\d+\.\d+", names.VERSION) is not None, "VERSION is X.Y.Z")
    rel = read(PUB / ".github" / "workflows" / "release.yml")
    check(rel is not None and "names.launcher_zip_name" in rel and "names.ASSET_STEM" in rel,
          "release.yml takes the asset names from names.py")
    check(rel is not None and "names.setup_exe_name" in rel, "release.yml takes the setup exe name from names.py")
    pz = read(HERE / "pack_zip.py") or ""
    check("names.launcher_zip_name" in pz and "names.LAUNCHER_EXE" in pz, "pack_zip.py takes its names from names.py")
    print("test_names:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

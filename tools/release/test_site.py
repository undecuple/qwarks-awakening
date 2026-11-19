#!/usr/bin/env python3
"""Host test for tools/release/check_site.py: the committed site passes, and each rule fails on a planted fault
(unclosed tag, dead anchor, missing image, external script, a second inline script and an inline handler, external
stylesheet, a section or a link present on one page only, a missing link between the pages, a picture that is not a
link, a network call or unbalanced bracket in the script, an @import). Needs public/site (SKIP without it).
Exit 0 = PASS.

Structure after the Killzone 3 port's tools/release/test_site.py (same author, MIT); the faults and the staged layout
(assets/ beside site/, the script as tools/release/site.js) are this port's.

    python3 tools/release/test_site.py
"""
import pathlib
import re
import shutil
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import check_site  # noqa: E402

ROOT = HERE.parents[1]
SITE = ROOT / "public/site" if (ROOT / "public/site").is_dir() else ROOT / "site"
fails = []


def expect(cond, what):
    if not cond:
        fails.append(what)


def staged(tmp):
    """A copy of the site in a temp layout, an empty file for every picture it names, and the script beside it."""
    site = tmp / "site"
    shutil.copytree(SITE, site)
    for m in re.finditer(r'(?:src|href)="\.\./assets/([^"]+)"', (site / "index.html").read_text()):
        (tmp / "assets").mkdir(parents=True, exist_ok=True)
        (tmp / "assets" / m.group(1)).write_bytes(b"x")
    (tmp / "site.js").write_text((HERE / "site.js").read_text())
    return site


def mutate(name, f, text_edit, want):
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        site = staged(tmp)
        target = tmp / f if f == "site.js" else site / f
        t = target.read_text()
        new = text_edit(t)
        expect(new != t, f"{name}: the planted fault changed nothing")
        target.write_text(new)
        probs, _ = check_site.check(site, tmp / "site.js")
        expect(any(want in p for p in probs), f"{name}: not caught (wanted {want!r}); got {probs[:3]}")


def main():
    if not (SITE / "index.html").is_file():
        print("SKIP test_site (no public/site)")
        return 0
    with tempfile.TemporaryDirectory() as d:
        probs, notes = check_site.check(staged(pathlib.Path(d)), pathlib.Path(d) / "site.js")
        expect(not probs, f"the committed site has problems: {probs[:4]}")
    shot = re.compile(r'<a href="(\.\./assets/screen-[^"]+)" data-viewer="images">(<img[^>]*>)</a>')
    mutate("unclosed tag", "index.html", lambda t: t.replace("</figcaption>", "", 1), "closes")
    mutate("dead anchor", "index.html", lambda t: t.replace('href="#status"', 'href="#nowhere"', 1), "#nowhere has no target")
    mutate("missing image", "index.html", lambda t: t.replace("../assets/screen-metropolis.jpg", "../assets/gone.png"), "does not resolve")
    mutate("external script", "index.html", lambda t: t.replace("<script>", '<script src="https://cdn.example/a.js">', 1), "script loaded from")
    mutate("second script", "index.html", lambda t: t.replace("</body>", "<script>x()</script></body>", 1), "2 scripts")
    mutate("inline script on the plain page", "plain.html", lambda t: t.replace("</head>", "<script>x()</script></head>", 1), "1 scripts (want 0)")
    mutate("inline handler", "index.html", lambda t: t.replace("<main ", '<main onclick="x()" ', 1), "inline event handler")
    mutate("external stylesheet", "plain.html", lambda t: t.replace("</head>", '<link rel="stylesheet" href="https://cdn.example/x.css"></head>', 1), "loads https://cdn.example")
    mutate("web fonts on the plain page", "plain.html", lambda t: t.replace("</head>", '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=X"></head>', 1), "loads https://fonts")
    mutate("section only on index", "index.html", lambda t: t.replace('<section class="panel sec" id="about"', '<section class="panel sec" id="extra" aria-labelledby="h-extra"><h2 id="h-extra">Extra</h2></section>\n<section class="panel sec" id="about"', 1), "sections differ")
    mutate("link only on plain", "plain.html", lambda t: t.replace('<section class="sec" id="channels" aria-labelledby="h-channels">', '<section class="sec" id="channels" aria-labelledby="h-channels"><a href="https://example.org/">x</a>', 1), "links in section channels differ")
    mutate("no link to plain", "index.html", lambda t: t.replace('href="plain.html"', 'href="#top"'), "does not link plain.html")
    mutate("no link back", "plain.html", lambda t: t.replace('href="index.html"', 'href="#top"'), "does not link back")
    mutate("picture not a link", "index.html", lambda t: shot.sub(r"\2", t, count=1), "not a link to its own file")
    mutate("network call", "site.js", lambda t: t.replace("var root =", "fetch('x'); var root =", 1), "forbidden")
    mutate("outside url", "site.js", lambda t: t.replace("var root =", "var u = 'https://x.example'; var root =", 1), "forbidden")
    mutate("unbalanced bracket", "site.js", lambda t: t.replace("})();", "}();"), "brackets")
    mutate("network call in the inline script", "index.html", lambda t: t.replace("<script>", "<script>fetch('x');", 1), "forbidden")
    mutate("css import", "style.css", lambda t: '@import url("https://x.example/a.css");\n' + t, "@import")
    mutate("css external url", "style.css", lambda t: t + "\nbody{background:url(https://x.example/a.png)}\n", "url(")
    mutate("no reduced motion", "style.css", lambda t: t.replace("prefers-reduced-motion", "prefers-x"), "reduced-motion")
    if fails:
        print("FAIL test_site:\n  - " + "\n  - ".join(fails))
        return 1
    print("PASS test_site (committed site clean, 21 planted faults caught; " + "; ".join(notes) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())

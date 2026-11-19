#!/usr/bin/env python3
"""Project site: public/README.md, docs/ROADMAP.md, CHANGELOG.md, docs/requirements.md and NOTICE.md
-> public/site/index.html, public/site/plain.html and public/site/llms.txt; the site palette (data/theme.json "site") and the
per-game accents of data/theme.json -> the colour block of public/site/style.css and plain.css (the rest of both files
is hand-written).

Structure after the Killzone 3 port's tools/release/website.py (same owner, MIT): a console-style front-end menu (one
section shown at a time with JavaScript, all of them stacked without it), a plain page (plain.html) that carries the same
sections without any script, the section list with a one-line detail per item, and the generator/--check layout. The look
is this port's own: an original gadget-panel theme (dark navy space, purple, cyan, gold; bevelled panels, a holo grid and
a progress dial drawn in CSS and inline SVG; no game art, logo or font), self-hosted Adwaita fonts.
The page is generated from the public tree's own text, so its status cannot
drift from the README: the status paragraph, the compatibility and known-issue lists, the changelog entry, the
roadmap and the requirements tables are rendered from those files, not retyped. The few words the page adds itself
(section intros, status labels, gallery captions) are the constants at the top of this file.

The export ships public/site/ as site/ at the public repo's root, next to assets/ (the images the page uses are
pointed at as ../assets/). The page loads nothing from another site (fonts are in site/fonts/). Its one script is inline
(PAGE_JS below: the image viewer VIEWER_JS and the menu script tools/release/site.js, allowed by one hash in the
Content-Security-Policy, with the viewer's CSS beside it): every content image is a plain link to the image file, which
the viewer opens in the page; without JavaScript the link opens the file itself, and plain.html has no script at all.
Publishing happens at the public release only.

    python3 tools/release/website.py            # regenerate public/site/index.html, plain.html, llms.txt, the colour blocks
    python3 tools/release/website.py --check    # exit 1 when a committed page or a colour block is stale
    python3 tools/release/website.py --root DIR # another repo root (a test fixture)
"""
import argparse
import base64
import hashlib
import html
import json
import math
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import names  # noqa: E402  (product name, links: tools/release/names.py)
import theme_tokens

REPO = names.REPO_URL
BLOB = REPO + "/blob/main/"

# Words the page adds itself (everything else is taken from the public files).
NAV = [("status", "Status", "How far each game gets, from the README's own tables."),
       ("screens", "Screens", "Scripted runs of Tools of Destruction, whole frames."),
       ("about", "About", "What this is, how it works, and how AI was used."),
       ("changelog", "Changelog", "The newest release notes."),
       ("objectives", "Objectives", "What is planned, in rough order. No dates."),
       ("install", "Install", "The launcher builds each game from your own copy."),
       ("requirements", "Requirements", "What you need to build and run the port."),
       ("channels", "Official channels", "Where releases and help come from.")]
INTRO = {
    "status": "What the README says today, word for word.",
    "screens": ("Tools of Destruction running in the port: the Windows program under Proton on Linux, in scripted runs of "
                "November 2026, none played by hand. The box at the top left of each shot is the frame "
                "rate (frames presented per second, a 10-second average); the bar at the bottom names the shot."),
    "about": "",
    "changelog": "Newest first. The full file is on GitHub.",
    "objectives": "What is planned, in rough order. No dates.",
    "install": "No game program is released: the launcher builds it on your PC from your own copy of the game.",
    "requirements": "What you need to build and run the port.",
    "channels": "",
}
CREDIT = ("Game imagery © Sony Interactive Entertainment. <em>Ratchet &amp; Clank Future: Tools of Destruction</em> was "
          "developed by Insomniac Games.")
FONT_NOTE = ('Fonts: Adwaita Sans and Adwaita Mono, under the <a href="fonts/OFL-adwaita.txt">SIL Open Font License</a>, '
             'served from this site: the page makes no request to any other site.')
# Objectives > The other games: the per-game table lives in Status (from the README); the page points there once.
OTHER_GAMES_POINTER = 'The state of every game is in the <a href="#status">Status</a> table.'
GALLERY = [  # (file in public/assets, caption, alt)
    ("screen-metropolis.jpg", "Metropolis, the first level, after the glow fix (a new game, scripted, no input, 2026-11-10)",
     "Ratchet in a sunlit Metropolis plaza among trees and planters, a domed tower and airships behind"),
    ("screen-fight-festival.jpg", "The Imperial Fight Festival, started directly with the game's developer level option, scripted, no input, 2026-11-13",
     "Ratchet on the arena's platforms in the rain, a giant wheel and purple and yellow towers behind"),
    ("screen-apogee.jpg", "Apogee Space Station, started directly with the game's developer level option, scripted, no input, 2026-11-13",
     "Ratchet on a dark asteroid below the space station, blue crystals and a round pod nearby"),
    ("screen-space-combat.jpg", "The first space combat, started directly with the game's developer level option, scripted, no input, 2026-11-13",
     "Ratchet's ship among asteroids and purple clouds, enemy fighters ahead and a tutorial message on the auto-lock"),
    ("screen-sargasso.jpg", "Sargasso, started directly with the game's developer level option, scripted, no input, 2026-11-13",
     "Ratchet in a swamp meadow under a large moon, the Smuggler beside him"),
    ("screen-pirate-base.jpg", "The pirate base, started directly with the game's developer level option, scripted, no input, 2026-11-13",
     "Ratchet near his ship in a dark jungle cove, a domed building among the trees"),
]  # docs/checkpoints shots whole: every public image keeps the overlay bar (commit, what, link); the fps box is optional
GAUGE_TICKS = 24   # ticks around the progress dial
# Status label shown beside a game's State text in the Status table (label, glyph); picked from the State text.
# The last value picks the pill: accent for work done or under way, neutral for work not started.
GAME_LABELS = [("first target", "In progress", "▶", True), ("ppu code lifted", "Lifted", "◐", True),
               ("planned", "Planned", "○", False), ("last", "Last", "○", False)]
ROADMAP_LABELS = {"done": ("Done", "✓", True), "in progress": ("In progress", "▶", True), "planned": ("Planned", "○", False)}
# The site palette (an original gadget-panel scheme, not an official colour specification): dark navy space grounds,
# purple as the main accent, cyan for everything interactive (links, focus, the selected menu item), gold only for release
# highlights, a small Qwark green for identity. Light scheme under prefers-color-scheme: light. Every text/background pair
# is checked for WCAG AA in tools/release/site_check.py. The launcher keeps data/theme.json's own colours; only the
# per-game accents are read from that file.
SITE_PALETTE = json.loads((HERE.parents[1] / "data" / "theme.json").read_text(encoding="utf-8"))["site"]   # data/theme.json, "site"
SITE_METRICS = {"radius_card": 6, "radius_button": 6, "radius_pill": 9999, "focus_ring": 2}
# style.css's and plain.css's colour block, written between these two lines.
TOK_BEGIN = "/* colours: begin. Written by tools/release/website.py (SITE_PALETTE, data/theme.json game accents); edit those, not this block. */"
TOK_END = "/* colours: end */"


# The image viewer: every <a data-viewer="GROUP" href=IMAGE><img alt=...></a> opens IMAGE in a modal <dialog> in the
# page, with its figure caption (or the alt text), "n of N", previous/next within GROUP (buttons, Left/Right, swipe),
# and Close, Esc or a click beside the image to close. Focus moves to Close and returns to the clicked link; Tab stays
# in the dialog. Colours are the theme tokens of style.css. Without JavaScript the links open the image files.
VIEWER_LABELS = ("Previous image", "Next image", "Close")
VIEWER_GROUP = "images"   # one set for the whole page: Previous/Next walk the gallery, then the launcher shot
VIEWER_HTML = ('<dialog class="viewer" id="viewer" aria-labelledby="viewer-cap">'
               '<p class="viewer-count" aria-live="polite"></p>'
               '<button type="button" class="viewer-btn viewer-close" aria-label="%s"><span aria-hidden="true">✕</span></button>'
               '<button type="button" class="viewer-btn viewer-prev" aria-label="%s"><span aria-hidden="true">‹</span></button>'
               '<div class="viewer-stage"><img class="viewer-img" alt="" decoding="sync"></div>'
               '<button type="button" class="viewer-btn viewer-next" aria-label="%s"><span aria-hidden="true">›</span></button>'
               '<p class="viewer-cap" id="viewer-cap"></p></dialog>') % (VIEWER_LABELS[2], VIEWER_LABELS[0], VIEWER_LABELS[1])
VIEWER_CSS = """
body:has(.viewer[open]) { overflow: hidden; }
.page-shot { margin: 1.15rem 0; max-width: 45rem; }
.viewer { position: fixed; inset: 0; width: 100%; height: 100%; max-width: none; max-height: none; margin: 0; padding: 0.75rem; border: 0; color: var(--window-fg); background: var(--view-bg); box-sizing: border-box; }
.viewer[open] { display: grid; grid-template-columns: auto minmax(0, 1fr) auto; grid-template-rows: auto minmax(0, 1fr) auto; grid-template-areas: "count count close" "prev stage next" "cap cap cap"; gap: 0.5rem 0.75rem; align-items: center; }
.viewer::backdrop { background: var(--shade); }
.viewer-count { grid-area: count; margin: 0; color: var(--dim-fg); font-size: 0.875rem; }
.viewer-stage { grid-area: stage; display: flex; align-items: center; justify-content: center; min-height: 0; height: 100%; }
.viewer-img { display: block; max-width: 100%; max-height: 100%; object-fit: contain; border-radius: var(--radius-button); }
.viewer-cap { grid-area: cap; justify-self: center; max-width: 66rem; margin: 0; color: var(--dim-fg); font-size: 0.875rem; line-height: 1.55; text-align: center; }
.viewer-btn { display: inline-flex; align-items: center; justify-content: center; width: 2.75rem; height: 2.75rem; padding: 0; border: 1px solid var(--border); border-radius: var(--radius-button); color: var(--window-fg); background: var(--button-bg); font: inherit; font-size: 1.5rem; line-height: 1; cursor: pointer; }
.viewer-btn:hover { background: var(--button-hover); }
.viewer-btn:active { background: var(--button-active); }
.viewer-btn:focus-visible { outline: var(--focus-ring) solid var(--accent); outline-offset: 2px; }
.viewer-btn[hidden] { display: none; }
.viewer-close { grid-area: close; justify-self: end; font-size: 1.1rem; }
.viewer-prev { grid-area: prev; }
.viewer-next { grid-area: next; }
@media (max-width: 42rem) {
  .viewer[open] { grid-template-columns: minmax(0, 1fr) auto; grid-template-areas: "count close" "stage stage" "cap cap" "prev next"; }
  .viewer-next { justify-self: end; }
}
@media (prefers-reduced-motion: no-preference) { .viewer[open] { animation: viewer-in 0.15s ease-out; } }
@keyframes viewer-in { from { opacity: 0; } }
"""
VIEWER_JS = """
(function () {
  var d = document.getElementById("viewer");
  if (!d || !d.showModal) return;
  var q = function (s) { return d.querySelector(s); };
  var img = q(".viewer-img"), cap = q(".viewer-cap"), count = q(".viewer-count"),
      prev = q(".viewer-prev"), next = q(".viewer-next"), set = [], i = 0, from = null, x0 = null;
  var links = document.querySelectorAll("a[data-viewer]");
  for (var k = 0; k < links.length; k++) links[k].setAttribute("aria-haspopup", "dialog");
  function show(n) {
    i = (n + set.length) % set.length;
    var a = set[i], t = a.querySelector("img"), f = a.closest("figure"), c = f && f.querySelector("figcaption");
    img.src = a.getAttribute("href");
    img.alt = t ? t.alt : "";
    cap.textContent = c ? c.textContent : img.alt;
    count.textContent = (i + 1) + " of " + set.length;
    prev.hidden = next.hidden = set.length < 2;
  }
  document.addEventListener("click", function (e) {
    var a = e.target.closest && e.target.closest("a[data-viewer]");
    if (!a || e.button || e.ctrlKey || e.metaKey || e.shiftKey || e.altKey) return;
    e.preventDefault();
    set = [].slice.call(document.querySelectorAll('a[data-viewer="' + a.getAttribute("data-viewer") + '"]'));
    from = a;
    a.focus();   // the dialog's own focus restore then agrees with ours (Safari does not focus links on click)
    show(set.indexOf(a));
    d.showModal();
    q(".viewer-close").focus();
  });
  prev.addEventListener("click", function () { show(i - 1); });
  next.addEventListener("click", function () { show(i + 1); });
  q(".viewer-close").addEventListener("click", function () { d.close(); });
  d.addEventListener("click", function (e) { if (e.target === d || e.target.className === "viewer-stage") d.close(); });
  d.addEventListener("keydown", function (e) {
    if (e.key === "ArrowLeft") { show(i - 1); e.preventDefault(); }
    else if (e.key === "ArrowRight") { show(i + 1); e.preventDefault(); }
    else if (e.key === "Escape") { d.close(); e.preventDefault(); }
    else if (e.key === "Tab") {
      var b = [].filter.call(d.querySelectorAll("button"), function (x) { return !x.hidden; });
      var at = b.indexOf(document.activeElement), to = e.shiftKey ? at - 1 : at + 1;
      b[(to + b.length) % b.length].focus();
      e.preventDefault();
    }
  });
  d.addEventListener("touchstart", function (e) { x0 = e.changedTouches[0].clientX; }, { passive: true });
  d.addEventListener("touchend", function (e) {
    if (x0 === null) return;
    var dx = e.changedTouches[0].clientX - x0;
    x0 = null;
    if (Math.abs(dx) > 40 && set.length > 1) show(dx < 0 ? i + 1 : i - 1);
  }, { passive: true });
  d.addEventListener("close", function () { img.removeAttribute("src"); if (from) from.focus(); });
})();
"""


SITE_JS = (HERE / "site.js").read_text(encoding="utf-8")
PAGE_JS = VIEWER_JS + SITE_JS     # the page's one inline script


def csp_hash(text):
    """The CSP source that allows one inline <script> or <style> with exactly this text."""
    return "'sha256-%s'" % base64.b64encode(hashlib.sha256(text.encode("utf-8")).digest()).decode()


def viewer_link(href, img_html, group):
    """A content image as a link to its file that the viewer opens (group: the set previous/next walks); with no group
    (plain.html) a plain link to the file."""
    if group is None:
        return '<a href="%s">%s</a>' % (esc(href), img_html)
    return '<a href="%s" data-viewer="%s">%s</a>' % (esc(href), esc(group), img_html)


# ---------------------------------------------------------------- markdown subset
def esc(s):
    return html.escape(s, quote=True)


def fix_link(url):
    """Relative links of the public Markdown -> a page anchor or the file on GitHub."""
    if re.match(r"[a-z]+:", url):
        return url
    anchors = {"#getting-started": "#install", "#compatibility": "#status",
               "#settings-and-known-issues": "#status", "docs/ROADMAP.md": "#objectives"}
    if url in anchors:
        return anchors[url]
    if url.startswith("#"):
        return BLOB + "README.md" + url
    path = url
    if not path.startswith(("docs/", ".github/")) and path.endswith(".md") and path not in (
            "README.md", "NOTICE.md", "CHANGELOG.md", "AGENTS.md"):
        path = "docs/" + path           # links inside docs/*.md are relative to docs/; the root's own pages stay
    return BLOB + path


# README badges, [![alt](image-url)](link): the page's CSP is img-src 'self', so remote badge images are left out of the
# site (its text link beside them stays) instead of loosening the policy.
BADGE = re.compile(r"[ \t]*\[!\[[^\]]*\]\([^)\s]+\)\]\([^)\s]+\)")


def inline(text):
    text = BADGE.sub("", text)
    codes = []

    def stash(m):
        codes.append("<code>%s</code>" % esc(m.group(1)))
        return "\x00%d\x00" % (len(codes) - 1)
    t = re.sub(r"`([^`]+)`", stash, text)
    t = re.sub(r"<!--.*?-->", "", t)
    t = esc(t)
    t = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: '<a href="%s">%s</a>' % (esc(fix_link(html.unescape(m.group(2)))), m.group(1)), t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w*])\*([^*\s][^*]*?)\*(?![\w*])", r"<em>\1</em>", t)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], t)


def plain(text):
    """Markdown inline -> plain text (for llms.txt and the checks)."""
    t = re.sub(r"<!--.*?-->", "", BADGE.sub("", text))
    t = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", t)
    return re.sub(r"[`*]", "", t)


def split_row(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def render_table(rows, labels=None, game_rows=None):
    """A table on wide screens; on phones style.css shows each row as a boxed-list row, each cell under its column's
    name (data-label)."""
    head, body = rows[0], rows[2:]
    games = any(plain(r[0]).startswith(t) for r in body if r for t in (game_rows or {}))
    out = ['<div class="tablewrap%s"><table>' % (" games" if games else "")]
    if any(head):
        out.append("<thead><tr>%s</tr></thead>" % "".join('<th scope="col">%s</th>' % inline(c) for c in head))
    out.append("<tbody>")
    for r in body:
        cells = []
        for i, c in enumerate(r):
            extra = ""
            if labels and i == len(r) - 1:
                extra = labels(r) or ""
            lab = plain(head[i]) if i < len(head) else ""
            cells.append("<td%s>%s%s</td>" % (' data-label="%s"' % esc(lab) if lab else "", extra, inline(c)))
        game = next((gid for title, gid in (game_rows or {}).items() if plain(r[0]).startswith(title)), None) if r else None
        attr = f' id="game-{esc(game)}" data-game="{esc(game)}" tabindex="0"' if game else ""
        out.append("<tr%s>%s</tr>" % (attr, "".join(cells)))
    out.append("</tbody></table></div>")
    return "".join(out)


def chip(label, glyph, accent=True):
    return '<span class="chip%s"><span aria-hidden="true">%s</span> %s</span> ' % (
        " accent" if accent else "", glyph, esc(label))


def game_label(row):
    state = row[-1].lower()
    for key, label, glyph, accent in GAME_LABELS:
        if state.startswith(key):
            return chip(label, glyph, accent)
    return ""


def blocks(md, table_labels=None, hlevel=4, game_rows=None, viewer=True):
    """Render a Markdown fragment: paragraphs, bullet lists, tables, headings (as h<hlevel>), <details> skipped."""
    out, lines, i = [], md.split("\n"), 0
    while i < len(lines):
        ln = lines[i]
        if not ln.strip() or ln.startswith(("<details", "</details", "<summary")):
            i += 1
        elif ln.startswith("#"):
            m = re.match(r"(#+)\s+(.*)", ln)
            lvl = min(6, hlevel + len(m.group(1)) - 3)
            out.append("<h%d>%s</h%d>" % (lvl, inline(m.group(2)), lvl))
            i += 1
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(split_row(lines[i]))
                i += 1
            out.append(render_table(rows, table_labels, game_rows))
        elif re.match(r"\d+\. ", ln):
            items = []
            while i < len(lines) and (re.match(r"\d+\. ", lines[i]) or lines[i].startswith("   ") and lines[i].strip()):
                if re.match(r"\d+\. ", lines[i]):
                    items.append(re.sub(r"^\d+\. ", "", lines[i]).strip())
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<ol>%s</ol>" % "".join("<li>%s</li>" % inline(x) for x in items))
        elif ln.startswith("- "):
            items = []
            while i < len(lines) and (lines[i].startswith("- ") or lines[i].startswith("  ") and lines[i].strip()):
                if lines[i].startswith("- "):
                    items.append(lines[i][2:].strip())
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            out.append("<ul>%s</ul>" % "".join("<li>%s</li>" % inline(x) for x in items))
        else:
            para = []
            while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "|", "- ", "<details")) \
                    and not re.match(r"\d+\. ", lines[i]):
                para.append(lines[i].strip())
                i += 1
            text = " ".join(para)
            # the README's centred picture (<p align="center"><img src=... alt=...></p>): Pages publishes only site/
            # and assets/, so the page links the picture on GitHub instead of printing its HTML as text
            img = re.fullmatch(r'<p[^>]*>\s*<img\s+([^>]*?)/?>\s*</p>', text)
            src = img and re.search(r'src="([^"]+)"', img.group(1))
            if src and src.group(1).startswith("assets/"):
                # an image Pages serves (assets/ is published next to site/): shown on the page, opened in the viewer
                alt = re.search(r'alt="([^"]*)"', img.group(1))
                u = "../" + html.unescape(src.group(1))
                out.append('<figure class="page-shot">%s</figure>' % viewer_link(
                    u, '<img src="%s" loading="lazy" alt="%s">' % (esc(u), esc(html.unescape(alt.group(1))) if alt else ""),
                    VIEWER_GROUP if viewer else None))
            elif src:
                alt = re.search(r'alt="([^"]+)"', img.group(1))
                label = "Screenshot" + (": " + html.unescape(alt.group(1)) if alt else "")
                out.append('<p><a href="%s">%s</a></p>' % (esc(fix_link(src.group(1))), esc(label)))
            else:
                out.append("<p>%s</p>" % inline(text))
    return "\n".join(out)


def section(md, heading, level=2):
    """Text under '## heading' up to the next heading of the same or higher level."""
    pat = r"^%s\s+%s\s*$" % ("#" * level, re.escape(heading))
    m = re.search(pat, md, re.M)
    if not m:
        raise SystemExit("website.py: heading not found: " + heading)
    rest = md[m.end():]
    n = re.search(r"^#{1,%d}\s" % level, rest, re.M)
    # the README's section dividers (---) are not content
    return "\n".join(x for x in (rest[:n.start()] if n else rest).split("\n") if x.strip() != "---" and "assets/divider-" not in x).strip("\n")


def paragraphs(md):
    return [p.strip() for p in re.split(r"\n\s*\n", md) if p.strip()]


def para_starting(md, start):
    for p in paragraphs(md):
        if p.startswith(start):
            return p
    raise SystemExit("website.py: paragraph not found: " + start)


# ---------------------------------------------------------------- facts
def read_facts(root):
    pub = pathlib.Path(root) / "public"
    f = {}
    readme = (pub / "README.md").read_text(encoding="utf-8")
    f["readme"] = readme
    f["roadmap"] = (pub / "docs" / "ROADMAP.md").read_text(encoding="utf-8")
    f["changelog"] = (pub / "CHANGELOG.md").read_text(encoding="utf-8")
    f["requirements"] = (pub / "docs" / "requirements.md").read_text(encoding="utf-8")
    f["notice"] = (pub / "NOTICE.md").read_text(encoding="utf-8")
    f["status_para"] = para_starting(readme, "**Status:")
    m = re.match(r"\*\*Status: (.+?)\.\*\*", f["status_para"])
    f["status_text"] = m.group(1)                       # "early work, not playable yet"
    f["status_word"] = f["status_text"].split(",")[0]   # "early work"
    f["tagline"] = re.search(r"^# .*\n\n\*\*(.+?)\*\*$", readme, re.M).group(1)   # the bold headline under the title
    f["alt"] = html.unescape(re.search(r'alt="([^"]+)"', readme).group(1))
    f["intro"] = re.sub(r"^> ?", "", para_starting(readme, "> **Bring your own game.**"), flags=re.M)
    f["notaffil"] = para_starting(readme, "*Qwark's Awakening* is a fan project's name")
    f["name"] = names.PRODUCT
    return f


def first_changelog(md):
    m = re.search(r"^## (\[(?!Unreleased\]).+)$", md, re.M)   # the newest release: unreleased entries wait for their tag
    rest = md[m.end():]
    n = re.search(r"^## ", rest, re.M)
    return m.group(1), (rest[:n.start()] if n else rest).strip("\n")


# ---------------------------------------------------------------- page
ROADMAP_FIRST = "1. Ratchet & Clank: All 4 One (first target)"


def roadmap_progress(rm):
    """(done, total) of the steps in the roadmap's first table: the progress dial is counted from the roadmap's own
    Done / In progress / Planned column, so it cannot drift from it."""
    rows = [split_row(x) for x in section(rm, ROADMAP_FIRST).split("\n") if x.startswith("|")][2:]
    return sum(1 for r in rows if r[0].lower() == "done"), len(rows)


def gauge_svg(done, total):
    """The progress dial: a ring of ticks, a track and an arc (done of total), drawn in inline SVG (presentation
    attributes only; the colours are classes of style.css, so the page needs no inline style)."""
    c = 2 * math.pi * 48
    arc = c * done / total if total else 0.0
    ticks = []
    for i in range(GAUGE_TICKS):
        a = 2 * math.pi * i / GAUGE_TICKS
        r0, r1 = (55.5, 59.5) if i % 6 == 0 else (57, 59.5)
        ticks.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f"/>' % (
            60 + r0 * math.cos(a), 60 + r0 * math.sin(a), 60 + r1 * math.cos(a), 60 + r1 * math.sin(a)))
    label = "First target, roadmap: %d of %d steps done" % (done, total)
    return ('<svg class="gauge" viewBox="0 0 120 120" role="img" aria-label="%s"><g class="g-ticks">%s</g>'
            '<circle class="g-track" cx="60" cy="60" r="48"/>'
            '<circle class="g-arc" cx="60" cy="60" r="48" stroke-dasharray="%.1f %.1f" transform="rotate(-90 60 60)"/>'
            '<text class="g-num" x="60" y="63" text-anchor="middle">%d/%d</text>'
            '<text class="g-cap" x="60" y="79" text-anchor="middle">steps done</text></svg>') % (
                esc(label), "".join(ticks), arc, c - arc, done, total), label


def section_bodies(f, root, viewer):
    """[(id, title, body html)] for the eight sections, the same for index.html (viewer=True: content images open in the
    viewer) and plain.html (plain links to the image files)."""
    catalog = json.loads((pathlib.Path(root) / "data/titles/catalog.json").read_text(encoding="utf-8"))
    game_rows = {g["title"]: g["id"] for g in catalog["games"]}
    readme, rm = f["readme"], f["roadmap"]
    word = f["status_word"][:1].upper() + f["status_word"][1:]
    compat = section(readme, "Status")
    true_today = section(readme, "What makes it different")
    start = section(readme, "Settings and known issues")
    issues = start.split("**Where your data goes.**")[0].strip("\n")
    howworks = section(readme, "How it works")
    ai = section(readme, "How AI was used", 3)
    ver_title, ver_body = first_changelog(f["changelog"])
    req = f["requirements"]
    req_intro = paragraphs(req)[1]
    req_you = section(req, "What you need from your own copy of the game")
    req_hw = section(req, "Hardware")
    req_dist = section(req, "Distribution")
    channels = para_starting(start, "**Where releases come from.**")
    download = section(readme, "Install")
    where_data = para_starting(start, "**Where your data goes.**")
    online = para_starting(start, "**What goes online.**")
    grp = VIEWER_GROUP if viewer else None

    def bl(md, **kw):
        return blocks(md, viewer=viewer, **kw)

    def roadmap_table(md):
        rows = [split_row(x) for x in md.split("\n") if x.startswith("|")]
        out = ['<div class="tablewrap"><table><tbody>']
        for r in rows[2:]:
            lab = ROADMAP_LABELS.get(r[0].lower())
            out.append("<tr><th scope=\"row\">%s</th><td>%s</td></tr>" % (
                chip(*lab) if lab else inline(r[0]), inline(r[1])))
        out.append("</tbody></table></div>")
        return "".join(out)

    road = []
    for title in (ROADMAP_FIRST, "1b. Ratchet & Clank Future: Tools of Destruction (earlier work)", "2. PC options",
                  "3. The other games", "4. Native Linux build with Vulkan", "5. Online play through re-psn",
                  "6. Co-op for the single-player games (optional)", "Not planned"):
        body = section(rm, title)
        h = re.sub(r"^\d[a-z]?\.\s*", "", title)
        lines = body.split("\n")
        if title.startswith("1."):
            tbl = "\n".join(x for x in lines if x.startswith("|"))
            road.append('<article class="roadmap-item roadmap-primary"><h3>%s</h3>\n%s</article>' % (inline(h), roadmap_table(tbl)))
        elif title.startswith("3."):
            # The README's per-game table is already in Status; repeating the roadmap's copy here showed every game twice.
            prose = "\n".join(x for x in lines if not x.startswith("|"))
            road.append('<article class="roadmap-item"><h3>%s</h3>\n%s\n<p>%s</p></article>' % (inline(h), bl(prose), OTHER_GAMES_POINTER))
        else:
            road.append('<article class="roadmap-item"><h3>%s</h3>\n%s</article>' % (inline(h), bl(body)))
    road_intro = paragraphs(rm.split("\n", 1)[1])[0]

    gallery = []
    for fn, cap, alt in GALLERY:
        p = pathlib.Path(root) / "public" / "assets" / fn
        if not p.exists():
            raise SystemExit("website.py: missing image " + str(p))
        gallery.append("<figure>%s<figcaption>%s</figcaption></figure>" % (viewer_link(
            "../assets/" + fn, '<img src="../assets/%s" loading="lazy" alt="%s">' % (fn, esc(alt or f["alt"])), grp),
            esc(cap)))

    return [
        ("status", "Status", '<div class="statuscard"><p class="word">%s</p>%s</div>\n<h3>What makes it different</h3>\n%s\n'
         "<h3>Compatibility</h3>\n%s\n<h3>Known issues</h3>\n%s" % (
             chip(word, "◐", False), bl(f["status_para"]),
             bl(true_today), bl(compat, table_labels=game_label, game_rows=game_rows), bl(issues))),
        ("screens", "Screens", '<div class="gallery">%s</div>\n<p class="credit">%s</p>' % ("".join(gallery), CREDIT)),
        ("about", "About", "%s\n<h3>How it works, briefly</h3>\n%s\n<h3>How AI was used</h3>\n%s" % (
            bl(f["intro"]), bl(howworks), bl(ai))),
        ("changelog", "Changelog", '<article class="entry"><h3>%s</h3>\n%s\n<p><a href="%sCHANGELOG.md">Full changelog on GitHub</a></p></article>'
         % (inline(ver_title), bl(ver_body), BLOB)),
        ("objectives", "Objectives", '%s\n<div class="roadmap-grid">%s</div>\n<p><a href="%sdocs/ROADMAP.md">Roadmap on GitHub</a></p>' % (
            bl(road_intro), "\n".join(road), BLOB)),
        ("install", "Install", "%s\n%s\n%s\n<p class=\"cta\"><a class=\"button suggested\" href=\"%s/releases\">Download the launcher preview</a> "
         "<a class=\"button\" href=\"%sdocs/BUILDING.md\">Build from source</a> "
         "<a class=\"button\" href=\"%sdocs/ONLINE.md\">Online play</a></p>"
         % (bl(download), bl(where_data), bl(online), REPO, BLOB, BLOB)),
        ("requirements", "Requirements", "%s\n<h3>From your own copy of the game</h3>\n%s\n<h3>Hardware</h3>\n%s\n<h3>Distribution</h3>\n%s\n"
         '<p><a href="%sdocs/requirements.md">Requirements on GitHub</a></p>' % (
             bl(req_intro), bl(req_you), bl(req_hw), bl(req_dist), BLOB)),
        ("channels", "Official channels", "%s\n<p class=\"cta\"><a class=\"button\" href=\"%s\">GitHub repository</a> "
         '<a class="button" href="%s/releases">GitHub Releases</a> <a class="button" href="%s/issues">Issues</a> '
         '<a class="button" href="https://github.com/sp00nznet/ps3recomp">ps3recomp</a></p>' % (
             bl(channels), REPO, REPO, REPO)),
    ]


def intro_html(sid):
    intro = INTRO.get(sid)
    return ('<p class="lede">%s</p>' % esc(intro)) if intro else ""


def head_html(f, plain_page):
    desc = '<meta name="description" content="%s. Status: %s.">' % (esc(plain(f["tagline"])), esc(f["status_text"]))
    if plain_page:
        policy = "default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'"
        extra = '<link rel="stylesheet" href="plain.css">'
        title = "%s (plain page)" % f["name"]
    else:
        policy = ("default-src 'none'; script-src %s; style-src 'self' %s; font-src 'self'; img-src 'self'; "
                  "base-uri 'none'; form-action 'none'" % (csp_hash(PAGE_JS), csp_hash(VIEWER_CSS)))
        extra = '<link rel="stylesheet" href="style.css">\n<style>%s</style>' % VIEWER_CSS
        title = f["name"]
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(title)}</title>
{desc}
<meta name="color-scheme" content="dark light">
<link rel="icon" href="data:,">
<meta http-equiv="Content-Security-Policy" content="{policy}">
{extra}
</head>"""


def foot_html(f, plain_page):
    fonts = "" if plain_page else "\n<p>%s</p>" % FONT_NOTE
    return f"""<footer class="foot">
<div class="wrap">
<p>{inline(f["notaffil"])}</p>
<p>Licence: <a href="{BLOB}LICENSE">MIT</a>. Third-party components, credits and the no-game-content rule: <a href="{BLOB}NOTICE.md">NOTICE</a>.
Source: <a href="{REPO}">{esc(REPO.split("//", 1)[1])}</a>.</p>{fonts}
</div>
</footer>"""


def tagline_description(f):
    d = plain(f["tagline"]).partition(": ")[2] or plain(f["tagline"])
    return d[:1].upper() + d[1:]


def version_of(f):
    m = re.search(r"<!-- version -->(.*?)<!-- /version -->", f["readme"])
    return m.group(1) if m else names.VERSION


def build_html(f, root):
    """index.html: the front-end menu page."""
    parts = section_bodies(f, root, True)
    word = f["status_word"][:1].upper() + f["status_word"][1:]
    done, total = roadmap_progress(f["roadmap"])
    gauge, gauge_label = gauge_svg(done, total)
    items = "".join(
        '<li><a href="#%s" data-detail="%s"><span class="idx" aria-hidden="true">%02d</span><span class="lbl">%s</span></a></li>'
        % (i, esc(d), n, esc(t)) for n, (i, t, d) in enumerate(NAV, 1))
    secs = "\n".join(
        '<section class="panel sec" id="%s" aria-labelledby="h-%s">\n<div class="panel-in">\n'
        '<header class="section-heading"><h2 id="h-%s">%s</h2>\n%s</header>\n%s\n</div>\n</section>'
        % (sid, sid, sid, esc(title), intro_html(sid), body) for sid, title, body in parts)
    top = f"""<body>
<a class="skip" href="#main">Skip to content</a>
<div id="app">
<header class="headerbar">
<div class="header-inner">
<a class="brand" href="#top"><span class="brand-mark" aria-hidden="true">QA</span><span><strong>{esc(f["name"])}</strong><span class="brand-caption">Unofficial PC port</span></span></a>
<nav class="header-links" aria-label="Project links"><a href="plain.html">Plain page</a><a href="{REPO}">Repository</a><a class="button" href="#install">Get the launcher <span aria-hidden="true">↗</span></a></nav>
</div>
</header>
<main id="main">
<div class="wrap">
<div class="hero" id="top">
<div class="hero-copy">
<p class="eyebrow">Ratchet &amp; Clank · PlayStation 3</p>
<h1>{esc(f["name"])}</h1>
<p class="intro-description">{esc(tagline_description(f))}</p>
<p class="intro-status"><span class="chip purple">{esc(word)}</span><span class="chip gold">Version {esc(version_of(f))}</span><span>{esc(f["status_text"][:1].upper() + f["status_text"][1:])}.</span></p>
<p class="cta"><a class="button suggested" href="{REPO}/releases">Launcher downloads <span aria-hidden="true">↗</span></a><a class="button" href="#status">Check game status</a></p>
</div>
<div class="hero-side">
<div class="dial">{gauge}<p class="dial-cap">{esc(gauge_label)}</p></div>
<aside class="intro-note" aria-label="Your own copy">{blocks(f["intro"])}</aside>
</div>
</div>
<p class="rule"><img class="divider" src="../assets/divider-plate.png" alt="" width="900" height="32"></p>
<div class="console">
<nav id="menu" class="menu" aria-label="Page sections">
<p class="eyebrow">Main menu</p>
<ol>{items}</ol>
<p id="detail" class="detail"></p>
<p class="hint">Up and Down, then Enter, or a game pad; tap on a phone.</p>
<a class="source-link" href="{BLOB}README.md">Read the README <span aria-hidden="true">↗</span></a>
</nav>
<div id="panel" class="panels" tabindex="-1">
"""
    foot = f"""
</div>
</div>
<p class="rule"><img class="divider" src="../assets/divider-orbit.png" alt="" width="900" height="32"></p>
</div>
</main>
{foot_html(f, False)}
</div>
{VIEWER_HTML}
<script>{PAGE_JS}</script>
</body>
</html>
"""
    return head_html(f, False) + "\n" + top + secs + foot


def build_plain(f, root):
    """plain.html: the same sections as one plain page, no script, own small stylesheet."""
    parts = section_bodies(f, root, False)
    toc = "".join('<li><a href="#%s">%s</a></li>' % (i, esc(t)) for i, t, _d in NAV)
    secs = "\n".join(
        '<section class="sec" id="%s" aria-labelledby="h-%s">\n<h2 id="h-%s">%s</h2>\n%s\n%s\n</section>'
        % (sid, sid, sid, esc(title), intro_html(sid), body) for sid, title, body in parts)
    top = f"""<body>
<a class="skip" href="#main">Skip to content</a>
<header class="top">
<div class="wrap">
<p class="crumb"><a href="index.html">Full page with the menu</a> · <a href="{REPO}">Repository</a></p>
<h1 id="top">{esc(f["name"])}</h1>
<p>{esc(tagline_description(f))}</p>
<p><strong>{esc(f["status_text"][:1].upper() + f["status_text"][1:])}.</strong> Version {esc(version_of(f))}.</p>
<nav aria-label="Sections"><ol class="toc">{toc}</ol></nav>
</div>
</header>
<main id="main">
<div class="wrap">
"""
    foot = f"""
</div>
</main>
{foot_html(f, True)}
</body>
</html>
"""
    return head_html(f, True) + "\n" + top + secs + foot


def build_llms(f):
    ver_title, ver_body = first_changelog(f["changelog"])
    games = [split_row(x) for x in section(f["readme"], "Status").split("\n") if x.startswith("|")]
    lines = ["# " + f["name"], "", "> " + plain(f["tagline"]), "",
             plain(f["status_para"]), "",
             plain(f["notaffil"]), "",
             "## Pages", "",
             "- [Project page](index.html): status, screens, about, changelog, objectives, install, requirements",
             "- [Plain page](plain.html): the same sections on one page, without script",
             "- [Repository](%s)" % REPO,
             "- [Building from source](%sdocs/BUILDING.md)" % BLOB,
             "- [Roadmap](%sdocs/ROADMAP.md)" % BLOB,
             "- [Online play](%sdocs/ONLINE.md)" % BLOB,
             "- [Requirements](%sdocs/requirements.md)" % BLOB,
             "- [Changelog](%sCHANGELOG.md)" % BLOB,
             "- [Licence and credits](%sNOTICE.md)" % BLOB, "",
             "## Games", ""]
    for r in games:
        # the per-game rows only: their second cell is the PS3 release year (the runs table's is a date)
        if len(r) == 3 and re.fullmatch(r"\d{4}", r[1].strip()):
            lines.append("- %s (%s): %s" % (plain(r[0]), r[1], plain(r[2])))
    lines += ["", "## Reuse", "",
              "MIT licence. Copies or substantial portions keep the copyright and permission notice; derived work "
              "credits \"Qwark's Awakening by %s\" with a link to the repository. See NOTICE.md." % names.OWNER, ""]
    return "\n".join(lines)


def build(root):
    """(index.html, plain.html, llms.txt) as text."""
    f = read_facts(root)
    return build_html(f, root), build_plain(f, root), build_llms(f)


def css_tokens(root):
    """The colour block of style.css and plain.css: SITE_PALETTE (dark by default, light under prefers-color-scheme:
    light; every token as --name, underscores become dashes), the metrics in px, and the per-game accents of
    data/theme.json (validated with theme_tokens, as the launcher's)."""
    t = json.loads((pathlib.Path(root) / "data" / "theme.json").read_text(encoding="utf-8"))
    bad = theme_tokens.problems(t)
    if bad:
        raise ValueError("Invalid shared theme: " + "; ".join(bad))

    def decls(mode, ind):
        return "".join("%s--%s: %s;\n" % (ind, k.replace("_", "-"), v) for k, v in SITE_PALETTE[mode].items())
    metrics = "".join("  --%s: %dpx;\n" % (k.replace("_", "-"), v) for k, v in SITE_METRICS.items())
    game_css = ""
    for mode in ("dark", "light"):
        rules = "".join(f'[data-game="{game}"] {{ --game-accent: {a[mode]}; }}\n'
                        for game, a in t.get("game_accents", {}).items())
        game_css += rules if mode == "dark" else "@media (prefers-color-scheme: light) {\n" + rules + "}\n"
    return (TOK_BEGIN + "\n:root {\n  color-scheme: dark;\n" + decls("dark", "  ") + metrics + "}\n"
            "@media (prefers-color-scheme: light) {\n  :root {\n    color-scheme: light;\n" + decls("light", "    ")
            + "  }\n}\n" + game_css + TOK_END)


def build_css(root, css):
    """A stylesheet with its colour block replaced by css_tokens(root)."""
    if css.count(TOK_BEGIN) != 1 or css.count(TOK_END) != 1:
        raise SystemExit("website.py: a stylesheet needs exactly one colour block (%s ... %s)" % (TOK_BEGIN, TOK_END))
    i, j = css.index(TOK_BEGIN), css.index(TOK_END) + len(TOK_END)
    return css[:i] + css_tokens(root) + css[j:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", default=str(HERE.parents[1]))
    a = ap.parse_args()
    page, plain_page, llms = build(a.root)
    site = pathlib.Path(a.root) / "public" / "site"
    css_now = {n: (site / n).read_text(encoding="utf-8") for n in ("style.css", "plain.css")}
    css = {n: build_css(a.root, t) for n, t in css_now.items()}
    texts = {"index.html": page, "plain.html": plain_page, "llms.txt": llms}
    if a.check:
        stale = [n for n, t in texts.items() if not (site / n).exists() or (site / n).read_text(encoding="utf-8") != t]
        stale += [n for n in css if css[n] != css_now[n]]
        if stale:
            print("public/site is stale (%s): run python3 tools/release/website.py" % ", ".join(stale))
            return 1
        print("public/site is current")
        return 0
    site.mkdir(parents=True, exist_ok=True)
    for n, t in {**texts, **css}.items():
        (site / n).write_text(t, encoding="utf-8")
    print("wrote %s (%d bytes), plain.html, llms.txt and the stylesheets' colours" % (site / "index.html", len(page.encode())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

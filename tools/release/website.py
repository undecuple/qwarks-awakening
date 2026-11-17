#!/usr/bin/env python3
"""Project site: public/README.md, docs/ROADMAP.md, CHANGELOG.md, docs/requirements.md and NOTICE.md
-> public/site/index.html and public/site/llms.txt; data/theme.json -> the colour block of public/site/style.css (the
rest of style.css is hand-written).

Structure after the Killzone 3 port's tools/release/website.py (same owner, MIT); the look is this port's own:
shared navy/orange/steel tokens, self-hosted Adwaita fonts, a compact navigation rail and grouped content panels.
The page is generated from the public tree's own text, so its status cannot
drift from the README: the status paragraph, the compatibility and known-issue lists, the changelog entry, the
roadmap and the requirements tables are rendered from those files, not retyped. The few words the page adds itself
(section intros, status labels, gallery captions) are the constants at the top of this file.

The export ships public/site/ as site/ at the public repo's root, next to assets/ (the images the page uses are
pointed at as ../assets/). The page loads no scripts and nothing from another site (fonts are in site/fonts/).
Publishing happens at the public release only.

    python3 tools/release/website.py            # regenerate public/site/index.html, llms.txt and style.css's colours
    python3 tools/release/website.py --check    # exit 1 when the committed page or style.css's colours are stale
    python3 tools/release/website.py --root DIR # another repo root (a test fixture)
"""
import argparse
import html
import json
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
NAV = [("status", "Status"), ("screens", "Screens"), ("about", "About"), ("changelog", "Changelog"),
       ("objectives", "Objectives"), ("install", "Install"), ("requirements", "Requirements"),
       ("channels", "Official channels")]
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
# Status label shown beside a game's State text in the Status table (label, glyph); picked from the State text.
# The last value picks the pill: accent for work done or under way, neutral for work not started.
GAME_LABELS = [("first target", "In progress", "▶", True), ("ppu code lifted", "Lifted", "◐", True),
               ("planned", "Planned", "○", False), ("last", "Last", "○", False)]
ROADMAP_LABELS = {"done": ("Done", "✓", True), "in progress": ("In progress", "▶", True), "planned": ("Planned", "○", False)}
# style.css's colour block, written from data/theme.json between these two lines.
TOK_BEGIN = "/* colours: begin. Written by tools/release/website.py from data/theme.json; edit the JSON, not this block. */"
TOK_END = "/* colours: end */"


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


def inline(text):
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
    t = re.sub(r"<!--.*?-->", "", text)
    t = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", t)
    return re.sub(r"[`*]", "", t)


def split_row(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def render_table(rows, labels=None, game_rows=None):
    """A table on wide screens; on phones style.css shows each row as a boxed-list row, each cell under its column's
    name (data-label)."""
    head, body = rows[0], rows[2:]
    out = ['<div class="tablewrap"><table>']
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


def blocks(md, table_labels=None, hlevel=4, game_rows=None):
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
            if src:
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
    return "\n".join(x for x in (rest[:n.start()] if n else rest).split("\n") if x.strip() != "---").strip("\n")


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
    m = re.search(r"^## (\[.+)$", md, re.M)
    rest = md[m.end():]
    n = re.search(r"^## ", rest, re.M)
    return m.group(1), (rest[:n.start()] if n else rest).strip("\n")


# ---------------------------------------------------------------- page
def build_html(f, root):
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
    steps = ""
    download = section(readme, "Install")
    where_data = para_starting(start, "**Where your data goes.**")
    online = para_starting(start, "**What goes online.**")

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
    for title in ("1. Ratchet & Clank Future: Tools of Destruction (first target)", "2. PC options",
                  "3. The other games", "4. Native Linux build with Vulkan", "5. Online play through re-psn",
                  "6. Co-op for the single-player games (optional)", "Not planned"):
        body = section(rm, title)
        h = re.sub(r"^\d\.\s*", "", title)
        lines = body.split("\n")
        if title.startswith("1."):
            tbl = "\n".join(x for x in lines if x.startswith("|"))
            road.append('<article class="roadmap-item roadmap-primary"><h3>%s</h3>\n%s</article>' % (inline(h), roadmap_table(tbl)))
        elif title.startswith("3."):
            # The README's per-game table is already in Status; repeating the roadmap's copy here showed every game twice.
            prose = "\n".join(x for x in lines if not x.startswith("|"))
            road.append('<article class="roadmap-item"><h3>%s</h3>\n%s\n<p>%s</p></article>' % (inline(h), blocks(prose), OTHER_GAMES_POINTER))
        else:
            road.append('<article class="roadmap-item"><h3>%s</h3>\n%s</article>' % (inline(h), blocks(body)))
    road_intro = paragraphs(rm.split("\n", 1)[1])[0]

    gallery = []
    for fn, cap, alt in GALLERY:
        p = pathlib.Path(root) / "public" / "assets" / fn
        if not p.exists():
            raise SystemExit("website.py: missing image " + str(p))
        gallery.append('<figure><a href="../assets/%s"><img src="../assets/%s" loading="lazy" alt="%s"></a>'
                       "<figcaption>%s</figcaption></figure>" % (fn, fn, esc(alt or f["alt"]), esc(cap)))

    def sec(sid, title, body, intro=None):
        intro = INTRO.get(sid) if intro is None else intro
        return ('<section class="panel" id="%s" aria-labelledby="h-%s">\n<header class="section-heading"><h2 id="h-%s">%s</h2>\n%s</header>\n%s\n</section>'
                % (sid, sid, sid, esc(title), ('<p class="lede">%s</p>' % esc(intro)) if intro else "", body))

    nav = "".join('<li><a href="#%s">%s</a></li>' % (i, esc(t)) for i, t in NAV)
    parts = [
        sec("status", "Status", '<div class="statuscard"><p class="word">%s</p>%s</div>\n<h3>What makes it different</h3>\n%s\n'
            "<h3>Compatibility</h3>\n%s\n<h3>Known issues</h3>\n%s" % (
                chip(word, "◐", False), blocks(f["status_para"]),
                blocks(true_today), blocks(compat, game_label, game_rows=game_rows), blocks(issues))),
        sec("screens", "Screens", '<div class="gallery">%s</div>\n<p class="credit">%s</p>' % ("".join(gallery), CREDIT)),
        sec("about", "About", "%s\n<h3>How it works, briefly</h3>\n%s\n<h3>How AI was used</h3>\n%s" % (
            blocks(f["intro"]), blocks(howworks), blocks(ai))),
        sec("changelog", "Changelog", '<article class="entry"><h3>%s</h3>\n%s\n<p><a href="%sCHANGELOG.md">Full changelog on GitHub</a></p></article>'
            % (inline(ver_title), blocks(ver_body), BLOB)),
        sec("objectives", "Objectives", '%s\n<div class="roadmap-grid">%s</div>\n<p><a href="%sdocs/ROADMAP.md">Roadmap on GitHub</a></p>' % (
            blocks(road_intro), "\n".join(road), BLOB)),
        sec("install", "Install", "%s\n%s\n%s\n%s\n<p class=\"cta\"><a class=\"button suggested\" href=\"%s/releases\">Download the launcher preview</a> "
            "<a class=\"button\" href=\"%sdocs/BUILDING.md\">Build from source</a> "
            "<a class=\"button\" href=\"%sdocs/ONLINE.md\">Online play</a></p>"
            % (blocks(download), blocks(steps), blocks(where_data), blocks(online), REPO, BLOB, BLOB)),
        sec("requirements", "Requirements", "%s\n<h3>From your own copy of the game</h3>\n%s\n<h3>Hardware</h3>\n%s\n<h3>Distribution</h3>\n%s\n"
            '<p><a href="%sdocs/requirements.md">Requirements on GitHub</a></p>' % (
                blocks(req_intro), blocks(req_you), blocks(req_hw), blocks(req_dist), BLOB)),
        sec("channels", "Official channels", "%s\n<p class=\"cta\"><a class=\"button\" href=\"%s\">GitHub repository</a> "
            '<a class="button" href="%s/releases">GitHub Releases</a> <a class="button" href="%s/issues">Issues</a> '
            '<a class="button" href="https://github.com/sp00nznet/ps3recomp">ps3recomp</a></p>' % (
                blocks(channels), REPO, REPO, REPO)),
    ]
    head = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(f["name"])}</title>
<meta name="description" content="{esc(plain(f["tagline"]))}. Status: {esc(f["status_text"])}.">
<meta name="color-scheme" content="dark light">
<link rel="icon" href="data:,">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'self'; font-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'">
<link rel="stylesheet" href="style.css">
</head>"""
    description = plain(f["tagline"]).partition(": ")[2] or plain(f["tagline"])
    description = description[:1].upper() + description[1:]
    top = f"""<body>
<a class="skip" href="#main">Skip to content</a>
<header class="headerbar">
<div class="header-inner">
<a class="brand" href="#top"><span class="brand-mark" aria-hidden="true">QA</span><span><strong>{esc(f["name"])}</strong><span class="brand-caption">Unofficial PC port</span></span></a>
<nav class="header-links" aria-label="Project links"><a href="{REPO}">Repository</a><a class="button" href="#install">Get the launcher <span aria-hidden="true">↗</span></a></nav>
</div>
</header>
<main id="main">
<div class="wrap">
<div class="project-intro" id="top">
<div class="intro-copy">
<p class="eyebrow">Ratchet &amp; Clank · PlayStation 3</p>
<h1>{esc(f["name"])}</h1>
<p class="intro-description">{esc(description)}</p>
<p class="intro-status"><span class="chip">{esc(word)}</span><span>{esc(f["status_text"][:1].upper() + f["status_text"][1:])}.</span></p>
<p class="cta"><a class="button suggested" href="{REPO}/releases">Launcher downloads <span aria-hidden="true">↗</span></a><a class="button" href="#status">Check game status</a></p>
</div>
<aside class="intro-note" aria-label="Your own copy">{blocks(f["intro"])}</aside>
</div>
<div class="page-layout">
<aside class="section-nav"><p class="eyebrow">Explore the project</p><nav aria-label="Page sections"><ul>{nav}</ul></nav><a class="source-link" href="{BLOB}README.md">Read the README <span aria-hidden="true">↗</span></a></aside>
<div class="page-content">
"""
    foot = f"""
</div>
</div>
</div>
</main>
<footer class="foot">
<div class="wrap">
<p>{inline(f["notaffil"])}</p>
<p>Licence: <a href="{BLOB}LICENSE">MIT</a>. Third-party components, credits and the no-game-content rule: <a href="{BLOB}NOTICE.md">NOTICE</a>.
Source: <a href="{REPO}">{esc(REPO.split("//", 1)[1])}</a>.</p>
<p>{FONT_NOTE}</p>
</div>
</footer>
</body>
</html>
"""
    return head + "\n" + top + "\n".join(parts) + foot


def build_llms(f):
    ver_title, ver_body = first_changelog(f["changelog"])
    games = [split_row(x) for x in section(f["readme"], "Status").split("\n") if x.startswith("|")]
    lines = ["# " + f["name"], "", "> " + plain(f["tagline"]), "",
             plain(f["status_para"]), "",
             plain(f["notaffil"]), "",
             "## Pages", "",
             "- [Project page](index.html): status, screens, about, changelog, objectives, install, requirements",
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
    f = read_facts(root)
    return build_html(f, root), build_llms(f)


def css_tokens(root):
    """style.css's colour block from data/theme.json: dark by default, light under prefers-color-scheme: light;
    every token as --name (underscores become dashes), plus the metrics in px."""
    t = json.loads((pathlib.Path(root) / "data" / "theme.json").read_text(encoding="utf-8"))
    bad = theme_tokens.problems(t)
    if bad:
        raise ValueError("Invalid shared theme: " + "; ".join(bad))

    def decls(mode, ind):
        return "".join("%s--%s: %s;\n" % (ind, k.replace("_", "-"), v) for k, v in t["modes"][mode].items())
    metrics = "".join("  --%s: %dpx;\n" % (k.replace("_", "-"), v) for k, v in t["metrics"].items())
    game_css = ""
    for mode in ("dark", "light"):
        rules = "".join(f'[data-game="{game}"] {{ --game-accent: {a[mode]}; }}\n'
                        for game, a in t.get("game_accents", {}).items())
        game_css += rules if mode == "dark" else "@media (prefers-color-scheme: light) {\n" + rules + "}\n"
    return (TOK_BEGIN + "\n:root {\n  color-scheme: dark;\n" + decls("dark", "  ") + metrics + "}\n"
            "@media (prefers-color-scheme: light) {\n  :root {\n    color-scheme: light;\n" + decls("light", "    ")
            + "  }\n}\n" + game_css + TOK_END)


def build_css(root, css):
    """style.css with its colour block replaced by css_tokens(root)."""
    if css.count(TOK_BEGIN) != 1 or css.count(TOK_END) != 1:
        raise SystemExit("website.py: style.css needs exactly one colour block (%s ... %s)" % (TOK_BEGIN, TOK_END))
    i, j = css.index(TOK_BEGIN), css.index(TOK_END) + len(TOK_END)
    return css[:i] + css_tokens(root) + css[j:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", default=str(HERE.parents[1]))
    a = ap.parse_args()
    page, llms = build(a.root)
    site = pathlib.Path(a.root) / "public" / "site"
    css_now = (site / "style.css").read_text(encoding="utf-8")
    css = build_css(a.root, css_now)
    if a.check:
        cur = [(site / n).read_text(encoding="utf-8") if (site / n).exists() else None
               for n in ("index.html", "llms.txt")]
        if cur != [page, llms] or css != css_now:
            print("public/site is stale: run python3 tools/release/website.py")
            return 1
        print("public/site is current")
        return 0
    site.mkdir(parents=True, exist_ok=True)
    (site / "index.html").write_text(page, encoding="utf-8")
    (site / "llms.txt").write_text(llms, encoding="utf-8")
    (site / "style.css").write_text(css, encoding="utf-8")
    print("wrote %s (%d bytes), llms.txt and style.css's colours" % (site / "index.html", len(page.encode())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

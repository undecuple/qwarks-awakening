#!/usr/bin/env python3
"""Host test: source rules the launcher download depends on, checked without a Windows build.

    python3 tools/release/test_launcher_src.py

- tools/release/catalog.py (the engine the zip runs with the bundled Python embeddable package) imports standard
  library modules only, and no module of its own folder: the package's python313._pth keeps the script's folder off
  sys.path and the package is shipped unmodified, so a sibling import would work in a checkout and fail on the
  player's PC. Every import statement is read with ast (top level and inside functions).
- launcher/src/main.cpp keeps its UTF-8 entry: SDL_main.h is included and SDL_MAIN_HANDLED is not defined (on Windows
  SDL's entry point hands main() a UTF-8 argv built from the wide command line), the environment is read with
  SDL_getenv (getenv is ANSI on Windows), and the bundled Python is found from SDL_GetBasePath (the exe's folder), not
  the working folder.
- theme: main.cpp takes its colours from the generated theme_tokens.h (current with data/theme.json), follows
  SDL_GetSystemTheme and its change event, stores the choice with the engine's set-pref; every font it loads is in
  launcher/fonts with the OFL texts, and Barlow/Rajdhani are gone.
Exit 0 = PASS.
"""
import ast
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
FAILS = []


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:600]))
    if not cond:
        FAILS.append(what)


def imported_roots(path):
    with open(path, encoding="utf-8") as f:
        tree = ast.parse(f.read(), path)
    roots, relative = set(), []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            roots |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                relative.append(node.module or ".")
            elif node.module:
                roots.add(node.module.split(".")[0])
        elif isinstance(node, ast.Call) and getattr(node.func, "id", getattr(node.func, "attr", "")) in (
                "__import__", "import_module"):
            relative.append("dynamic import")
    return roots, relative


def test_catalog_imports():
    cat = os.path.join(HERE, "catalog.py")
    roots, relative = imported_roots(cat)
    check(roots != set(), "catalog.py: imports found", roots)
    check(not relative, "catalog.py: no relative or dynamic import", relative)
    local = {os.path.splitext(n)[0] for n in os.listdir(HERE) if n.endswith(".py")} | \
            {n for n in os.listdir(HERE) if os.path.isdir(os.path.join(HERE, n))}
    check(not (roots & local), "catalog.py: no module of tools/release/ (the embeddable Python cannot see them)",
          sorted(roots & local))
    std = getattr(sys, "stdlib_module_names", None)
    if std is None:
        print("skip stdlib-name check (Python < 3.10 has no sys.stdlib_module_names)")
    else:
        check(roots <= set(std), "catalog.py: standard library modules only (no pip package)", sorted(roots - set(std)))


def test_main_cpp():
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        src = f.read()
    code = re.sub(r"//[^\n]*", "", src)
    check("#include <SDL3/SDL_main.h>" in code, "main.cpp: SDL_main.h included (SDL's UTF-8 entry point on Windows)")
    check("SDL_MAIN_HANDLED" not in code, "main.cpp: SDL_MAIN_HANDLED not defined")
    check(re.search(r"\bint main\(int argc, char\*\* argv\)", code) is not None and "wmain" not in code,
          "main.cpp: one plain main(argc, argv), renamed to SDL_main by the header")
    check(re.search(r"(?<![\w_])getenv\(", code) is None and "SDL_getenv(" in code, "main.cpp: SDL_getenv, never getenv")
    m = re.search(r"static std::vector<std::string> DefaultEngine\(const std::string& dir\) \{(.*?)\n\}", code, re.S)
    body = m.group(1) if m else ""
    check(m is not None and "pythonw.exe" in body and "python.exe" in body and "catalog.py" in body,
          "main.cpp: DefaultEngine finds the bundled pythonw.exe / python.exe and catalog.py")
    check("DefaultEngine(base" in code and "SDL_GetBasePath()" in code, "main.cpp: the bundled Python is looked up in the exe's folder")


def test_theme():
    """colours come from the generated token table, the style follows the system or the player's choice, the
    fonts the launcher loads ship with their licences."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        code = re.sub(r"//[^\n]*", "", f.read())
    check('#include "theme_tokens.h"' in code and "theme::kLight" in code and "theme::kDark" in code,
          "main.cpp: colours from theme_tokens.h (light and dark sets)")
    check(re.search(r"\bc(Bg|Panel|Panel2|Line|Text|Dim|Acc|AccText|Gold|Chip|Ok|Bad)\b", code) is None,
          "main.cpp: the old fixed palette constants are gone")
    check("SDL_GetSystemTheme()" in code and "SDL_EVENT_SYSTEM_THEME_CHANGED" in code,
          "main.cpp: System follows SDL_GetSystemTheme and its change event")
    check('"set-pref", "--library", A.libPath, "theme"' in code and '"--theme"' in code,
          "main.cpp: the style choice is stored by the engine (set-pref theme); --theme for screenshots")
    with open(os.path.join(ROOT, "launcher", "src", "model.h"), encoding="utf-8") as f:
        check('root["prefs"]["theme"]' in f.read(), "model.h: reads library.json prefs.theme")
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "theme_tokens.py"), "--check"], capture_output=True, text=True)
    check(r.returncode == 0, "theme_tokens.h is current with data/theme.json", r.stderr)
    fonts = os.path.join(ROOT, "launcher", "fonts")
    loaded = set(re.findall(r'LoadFont\("([^"]+)"\)', code))
    have = set(os.listdir(fonts))
    check(loaded and loaded <= have, "every font main.cpp loads is in launcher/fonts", sorted(loaded - have))
    check({"OFL-adwaita.txt", "OFL-russo-one.txt"} <= have, "launcher/fonts: the OFL texts beside the fonts")
    check(not any(n.startswith(("Barlow", "Rajdhani")) for n in have), "launcher/fonts: Barlow and Rajdhani removed")
    extra = sorted(n for n in have if n.endswith(".ttf") and n not in loaded)
    check(not extra, "launcher/fonts: no font the launcher does not load", extra)


def test_library_navigation():
    """Library-first destinations, compact header, Back inputs and preserved setup/settings groups.
    These are source contracts; compiled keyboard/visual checks are separate."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        code = re.sub(r"//[^\n]*", "", f.read())
    pages = re.search(r"kPages\[4\] = \{(.*?)\};", code, re.S)
    names = re.findall(r'\{"(\w+)", Ico::\w+, App::Sc(\w+)\}', pages.group(1)) if pages else []
    check([n for n, _ in names] == ["Library", "Files", "Settings", "About"], "main.cpp: navigation includes Library, Files, Settings, About", names)
    check(re.search(r"kCollapseW = 600\.f", code) is not None and "W < A.S(kCollapseW)" in code,
          "main.cpp: compact header below 600 px (scaled)")
    check("ImGuiKey_Escape" in code and "ImGuiKey_GamepadFaceRight" in code and "GoBack()" in code,
          "main.cpp: Esc and controller B go back")
    menu = re.search(r"static void HeaderBar\(float x0, float W\) \{(.*?)\n\}", code, re.S)
    check(menu is not None and "kPages" in menu.group(1) and "ImGuiKey_GamepadStart" in menu.group(1),
          "main.cpp: all destinations reachable from header menu and controller Start")
    check('ToggleGroup("filters"' in code, "main.cpp: Library filters are a linked toggle group")
    check('"No games added yet"' in code and '"loc|library_folders|--add"' in code,
          "main.cpp: an empty library shows a status page with Add folder")
    for fn in ("FilesScreen", "DetailsScreen", "AboutScreen"):
        m = re.search(r"static void " + fn + r"\(\) \{(.*?)\n\}", code, re.S)
        check(m is not None and "BeginBoxedList" in m.group(1), f"main.cpp: {fn} is built on boxed lists")
    m = re.search(r"static void AboutScreen\(\) \{(.*?)\n\}", code, re.S)
    check(m is not None and "A.fMark" in m.group(1) and "LAUNCHER_VERSION" in m.group(1), "main.cpp: About has the wordmark and version")
    with open(os.path.join(ROOT, "launcher", "src", "model.h"), encoding="utf-8") as f:
        check("lib.empty = lib.games.empty()" in f.read(), "model.h: a library with no game is empty, not an error")


def test_build_play():
    """Phase 2 wiring (2026-11-10): Build and Play run the engine's build / play as a child process, one at a time,
    streamed; Cancel is firstrun.py's stop file; no fake build is left."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        code = re.sub(r"//[^\n]*", "", f.read())
    check('{"build", "--library"}' in code and '"--stop-file"' in code and '{"play", "--library"}' in code,
          "main.cpp: Build runs `build --library F --game ID --stop-file S`, Play runs `play --library F --game ID`")
    check("SDL_GetProcessOutput" in code and "SDL_ReadIO" in code, "main.cpp: the engine's output is read as it streams")
    check("A.task.running.exchange(true)" in code, "main.cpp: one build or game at a time")
    for tag in ('"PROGRESS "', '"REASON "', '"RESULT "', '"LOG path="', '"EXITED "', '"GAME "'):
        check(tag in code, f"main.cpp: parses the engine's {tag.strip(chr(34)).strip()} lines")
    check("kBuildSeconds" not in code and "A.job" not in code and "buildTemp" not in code, "main.cpp: the fake build is gone")
    m = re.search(r"static void DetailsScreen\(\) \{(.*?)\n\}", code, re.S)
    check(m is not None and "BuildAndPlay(" in m.group(1), "main.cpp: the Details page has Build and play")


def test_phase4():
    """Launcher phase 4 (2026-11-10): the build kit, D30-the online settings and the opt-in release check. Nothing
    may connect without the player's click or opt-in: the only process that fetches is kit.py, started from the kit
    dialog's Download, the symbols Download, Check now, or at start only when prefs.update_check is on."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        code = re.sub(r"//[^\n]*", "", f.read())
    with open(os.path.join(ROOT, "launcher", "CMakeLists.txt"), encoding="utf-8") as f:
        cm = f.read()
    check("file(SHA256 \"${LAUNCHER_KIT_MANIFEST}\" LAUNCHER_KIT_SHA256)" in cm and "LAUNCHER_KIT_URL=" in cm
          and "releases/download/" in cm, "CMakeLists.txt: the kit hash and GitHub release URL come from kit.json")
    linux = re.search(r"static void StartLinuxTask\(.*?\n\}", code, re.S)
    check(linux is not None and len(re.findall(r"StartLinuxTask\(", code)) == 2
          and 'StartLinuxTask(rt ? "runtime" : "tools")' in code,
          "main.cpp: the Linux downloads (proton.py / linux_kit.py fetch) start only from their dialog's Download")
    kit_code = code.replace(linux.group(0), "") if linux else code
    for verb in ("fetch", "symbols", "check-release"):
        n = len(re.findall(r'"%s"' % verb, kit_code))
        check(n == 1, f"main.cpp: kit.py {verb} is started from one place only", n)
    m = re.search(r"static void StartTask\(.*?\n\}", code, re.S)
    check(m is not None and "KitNeeded()" in m.group(0) and "A.kit.open = true" in m.group(0)
          and m.group(0).index("KitNeeded()") < m.group(0).index("ClaimTask("),
          "main.cpp: Build without the kit opens the kit dialog before anything starts")
    m = re.search(r"static void KitDialog\(.*?\n\}", code, re.S)
    body = m.group(0) if m else ""
    check('StartTask("kit"' in body and "Download##kd" in body and "Not now##kn" in body and "LAUNCHER_KIT_BYTES" in body
          and "A.kit.url" in body, "main.cpp: the kit dialog shows size and address and downloads only on Download")
    check(code.count('StartTask("kit"') == 2 and 'screenName == "kit"' in code,
          "main.cpp: the kit download starts from the dialog (and a screenshot, which starts no process)")
    m = re.search(r"CheckRelease\(true\)", code)
    check(len(re.findall(r"CheckRelease\(true\)", code)) == 2 and len(re.findall(r'updateCheck == "on"', code)) >= 2,
          "main.cpp: the release check at start runs only with prefs.update_check on")
    check('PlayDisclaimer(e)' in code and 'PlayDisclaimer(g)' in code and '"In this release: "' in code,
          "main.cpp: disclaimer beside Build (Details rows and the selected panel)")
    check('"Update available: rebuild"' in code and '"Port updated: rebuild"' not in code, "main.cpp: wording")
    check('"set-pref", "--library", A.libPath, key' in code and '"debug_info"' in code and '"update_check"' in code,
          "main.cpp: Debug information and Check for new releases are prefs written by the engine")
    check('"get-online"' in code and '"set-online"' in code and '"--method"' in code,
          "main.cpp: Online settings only through the engine's get-online / set-online")
    with open(os.path.join(ROOT, "launcher", "src", "model.h"), encoding="utf-8") as f:
        mh = f.read()
    check('j["playability"]' in mh and 'j["playability_note"]' in mh and 'root["prefs"]["debug_info"]' in mh
          and 'root["prefs"]["update_check"]' in mh, "model.h: reads playability and the phase 4 prefs")


def test_unofficial():
    """ENHANCEMENT: the opt-in Unofficial group is launch only: off by default, shown after the port's
    own games, picked through the engine, never fetched, built or patched."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        raw = f.read()
    code = re.sub(r"//[^\n]*", "", raw)
    check('SetPref("unofficial", on)' in code and 'Switch("##unofficial"' in code and "unofficialOn" in code,
          "main.cpp: Settings has the Unofficial switch (set-pref unofficial)")
    lib = re.search(r"static void LibraryScreen\(float W\) \{(.*?)\n\}", code, re.S)
    check(lib is not None and lib.group(1).index("DrawCard(g, i,") < lib.group(1).index("UnofficialGroup(")
          and "A.lib.unofficialOn" in lib.group(1), "main.cpp: the unofficial group comes after the port's own cards and only when on")
    card = re.search(r"static void DrawUnofficialCard\(.*?\n\}", code, re.S)
    check(card is not None and '"Unofficial"' in card.group(0) and "not supported here" in code,
          "main.cpp: each card has the Unofficial badge and the not-supported note")
    check('"unofficial-set"' in code and '"unofficial-clear"' in code and "BrowseExe" in code,
          "main.cpp: picking an exe goes through the engine (unofficial-set / -clear)")
    check("SDL_OpenURL(u.url" in raw and 'u.url.rfind("https://github.com/", 0) == 0' in raw,
          "main.cpp: the card only opens the project page on request (no fetching)")
    with open(os.path.join(ROOT, "launcher", "src", "model.h"), encoding="utf-8") as f:
        m = f.read()
    check('root["prefs"]["unofficial"]' in m and 'root["unofficial"]' in m and "if (lib.unofficialOn)" in m,
          "model.h: entries are read only when prefs.unofficial is on")


def test_firstrun_ux(code=None):
    """First-run blockers (report items 3, 5, 7, 8, 13, 14): source contracts."""
    if code is None:
        with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
            code = f.read()
    check('"override", "--library"' in code and "AddQueueNext()" in code and '"Use these files##use"' in code,
          "main.cpp: the Add drawer hands the chosen paths to the engine (override), Build waits for it")
    check("you do not supply it" not in code, "main.cpp: the drawer no longer says the player does not supply the EBOOT")
    check("SDL_ShowSimpleMessageBox" in code and code.count("return StartupFail(") >= 4 and "SDL_GetBasePath()" in code[code.index("static int StartupFail"):][:600],
          "main.cpp: start-up failures show a message box with the reason and the install folder")
    thr = re.search(r"static void TaskThread\(.*?\n\}", code, re.S)
    check(thr is not None and "ended without a result (exit code" in thr.group(0),
          "main.cpp: a build that ends without RESULT becomes a failed row with exit code and log path")
    check("fixFiles" in code and "e.state == State::PortUpdated || e.state == State::NeedsAttention;" not in code,
          "main.cpp: Build is not offered for a game that needs attention (Choose files opens Files)")
    check("A.addDisc0 = g.disc; A.addUpd0 = g.update; } };" in code,
          "main.cpp: a refused path resets the drawer baseline to the engine's paths (Build stays off)")
    check("If that was not expected, the log is here" in code, "main.cpp: an unexpected game exit shows the log path")


def main():
    test_catalog_imports()
    test_firstrun_ux()
    test_main_cpp()
    test_theme()
    test_library_navigation()
    test_build_play()
    test_phase4()
    test_unofficial()
    print("test_launcher_src:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

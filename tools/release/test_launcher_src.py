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


def test_split_view():
    """pass 2: a navigation split view (Library, Files, Settings, About) that collapses to one column, keyboard
    and controller back on Esc / B, the screens on the boxed-list widgets, the empty library as a status page."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        code = re.sub(r"//[^\n]*", "", f.read())
    pages = re.search(r"kPages\[4\] = \{(.*?)\};", code, re.S)
    names = re.findall(r'\{"(\w+)", Ico::\w+, App::Sc(\w+)\}', pages.group(1)) if pages else []
    check([n for n, _ in names] == ["Library", "Files", "Settings", "About"], "main.cpp: sidebar pages Library, Files, Settings, About", names)
    check(re.search(r"kCollapseW = 600\.f", code) is not None and "W < A.S(kCollapseW)" in code,
          "main.cpp: the split view collapses below 600 px (scaled)")
    check("ImGuiKey_Escape" in code and "ImGuiKey_GamepadFaceRight" in code and "GoBack()" in code,
          "main.cpp: Esc and controller B go back")
    check("ImGuiKey_GamepadDpadLeft" in code and "FocusSidebar" in code and "FocusContent" in code,
          "main.cpp: arrows / D-pad move focus between sidebar and content")
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


def main():
    test_catalog_imports()
    test_main_cpp()
    test_theme()
    test_split_view()
    print("test_launcher_src:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

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
Exit 0 = PASS.
"""
import ast
import os
import re
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


def main():
    test_catalog_imports()
    test_main_cpp()
    print("test_launcher_src:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

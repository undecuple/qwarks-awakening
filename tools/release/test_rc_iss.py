#!/usr/bin/env python3
"""Host test of installer/rc.iss (0.3.1): static checks of the Inno Setup script against the release tools. No
ISCC, no game data, no network; runs in the repository and in the public tree. Adapted from the Killzone 3 port's
test_install_dir_rule.py and test_uninstall_covers.py (Project Helghast, MIT, same author).

  - names: AppName, AssetStem, LauncherExe, RepoURL and AppDescription equal names.py; OutputBaseFilename is
    names.setup_exe_name without .exe; AppVersion and Payload come from the command line only (no default to drift);
  - payload: PayloadTops equals the top-level names of the full build zip as pack_zip.py builds it (bare launcher +
    build kit + kit.json + README.txt); PayloadDirs are its folders; [Files] installs the whole payload with no
    Excludes and no other source but the uninstall icon; the compile-time check walks the payload's top level;
  - over-install and uninstall: [InstallDelete] and [UninstallDelete] remove exactly the PayloadDirs folders
    (whole: Python's __pycache__), plus the program's own leftovers (the debug file, catalog.py BUILD_LOCK, kit.py
    STAGING and DOWNLOADS); the player's data (library.json and launcher/src/main.cpp's DefaultLocation folders, the
    build folder firstrun.py defaults to) is in none of [Files], [InstallDelete], [UninstallDelete], and only the
    uninstaller's question removes it (default No, never in a silent uninstall);
  - install folder rules (from kz3.iss): per-user, default {localappdata}\\Programs\\<product>, AppendDefaultDirName,
    the folder's last name, a foreign non-empty folder refused, [ ] ; refused, OneDrive refused, the same rules for
    silent /DIR= in PrepareToInstall; TimeStampsInUTC;
  - wizard: desktop shortcut task ticked, Start-menu entry, finish-page start of the launcher, one log (SetupLogging),
    no game pages, no download, no game file type named, public wording;
  - Pascal hygiene: no '{' inside a { } comment, no [Code] line starting with #NN, the product name with its
    apostrophe only through AppDirNameP inside Pascal strings.

    python3 tools/release/test_rc_iss.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_zip  # noqa: E402
import kit  # noqa: E402
import catalog  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
ISS = os.path.join(ROOT, "installer", "rc.iss")
FAILS = []


def check(ok, msg, detail=""):
    print(("ok   " if ok else "FAIL ") + msg + ("" if ok else f"\n     {str(detail)[:400]}"))
    if not ok:
        FAILS.append(msg)


def section(text, name):
    m = re.search(r"^\[" + re.escape(name) + r"\]\n(.*?)(?=^\[[A-Za-z]+\]\s*$|\Z)", text, re.M | re.S)
    return m.group(1) if m else ""


def entries(sec):
    """[(Type, path relative to {app}, lower case)] of an [InstallDelete] / [UninstallDelete] section."""
    out = []
    for m in re.finditer(r'^Type: (\w+); Name: "\{app\}\\([^"]+)"', sec, re.M):
        out.append((m.group(1), m.group(2).replace("\\", "/").lower()))
    return out


def expected_tops():
    """Top-level names of the full build zip, from pack_zip.py's own lists (no file needs to exist)."""
    _, bare = pack_zip.gather("", "", ROOT, "")
    paths = {r for r, _ in bare} | {"README.txt", names.KIT_MANIFEST, "python/python.exe"}
    paths |= {f"{t}/x" for t in pack_zip.KIT_TREE} | set(pack_zip.KIT_FILES)
    paths |= {"ps3recomp/x", "licenses/ps3recomp-LICENSE.txt"}
    import json
    with open(os.path.join(HERE, "toolchain.json"), encoding="utf-8") as f:
        for c in json.load(f)["components"]:
            if c.get("kit"):
                paths.add(c["kit"]["dest"] + "/x")
            if c.get("kit_license"):
                paths.add("licenses/" + c["kit_license"])
    tops = {p.split("/")[0].lower() for p in paths}
    dirs = {p.split("/")[0].lower() for p in paths if "/" in p}
    return tops, dirs


def player_data():
    """What the launcher and its engine write next to the launcher as the player's data."""
    with open(os.path.join(ROOT, "launcher", "src", "main.cpp"), encoding="utf-8") as f:
        src = f.read()
    m = re.search(r"static std::string DefaultLocation\(.*?\n(.*?)\n\}", src, re.S)
    locs = re.findall(r'[?:] "([a-z-]+)"', m.group(1)) if m else []
    check(len(locs) == 4, "main.cpp DefaultLocation names four folders", locs)
    check('+ "library.json"' in src, "main.cpp writes library.json next to the launcher")
    with open(os.path.join(HERE, "firstrun.py"), encoding="utf-8") as f:
        fr = f.read()
    check('os.path.join(root, "build", "firstrun", pid)' in fr, "firstrun.py builds under <root>/build by default")
    return ["library.json"] + locs + ["build"]


def main():
    with open(ISS, encoding="utf-8") as f:
        t = f.read()
    defs = dict(re.findall(r'^#define (\w+) "([^"]*)"', t, re.M))
    code = section(t, "Code")

    # names
    for k, v in (("AppName", names.PRODUCT), ("AppDirName", names.PRODUCT), ("AssetStem", names.ASSET_STEM),
                 ("LauncherExe", names.LAUNCHER_EXE), ("RepoURL", names.REPO_URL), ("AppDescription", names.DESCRIPTION)):
        check(defs.get(k) == v, f"#define {k} equals names.py", (defs.get(k), v))
    ob = re.search(r"^OutputBaseFilename=(.+)$", t, re.M)
    out = ob.group(1).replace("{#AssetStem}", defs.get("AssetStem", "")).replace("{#AppVersion}", "9.8.7") if ob else ""
    check(out + ".exe" == names.setup_exe_name("9.8.7"), "OutputBaseFilename is names.setup_exe_name", out)
    check("AppVersion" not in defs and "Payload" not in defs and "#error AppVersion is needed" in t
          and "#error Payload is needed" in t, "AppVersion and Payload come from the command line only")
    check(re.search(r"^AppId=\{\{[0-9A-F-]{36}\}\s*$", t, re.M) is not None, "AppId is a fixed GUID")
    guid = re.search(r"^AppId=\{\{([0-9A-F-]{36})\}", t, re.M)
    check(guid and f"Uninstall\\{{{guid.group(1)}}}_is1" in code, "PreviousInstallDir reads this AppId's uninstall key")

    # payload
    tops, dirs = expected_tops()
    have_tops = set(defs.get("PayloadTops", "").split("|"))
    have_dirs = set(defs.get("PayloadDirs", "").split("|"))
    check(have_tops == tops, "PayloadTops = the full build zip's top-level names (pack_zip.py)",
          (sorted(have_tops ^ tops)))
    check(have_dirs == dirs and have_dirs <= have_tops, "PayloadDirs = its folders", sorted(have_dirs ^ dirs))
    check("FindFirst(AddBackslash(Payload)" in t and 'Pos("|" + LowerCase(TopName) + "|", "|" + PayloadTops + "|")' in t
          and "#error the payload holds a top-level name" in t, "the compile stops on a payload name PayloadTops lacks")
    files = section(t, "Files")
    src = re.findall(r'^Source: "([^"]+)"([^\n]*)$', files, re.M)
    check([s for s, _ in src] == ["{#Payload}\\*", "art\\rc-uninstall.ico"], "[Files]: the whole payload and the icon", src)
    check(src and "recursesubdirs" in src[0][1] and "createallsubdirs" in src[0][1] and "Excludes" not in files
          and "dontcopy" not in files and "external" not in files, "[Files]: payload recursive, no Excludes or extra sources")
    for k in ("SetupIconFile", "WizardImageFile", "WizardSmallImageFile"):
        m = re.search(rf"^{k}=(.+)$", t, re.M)
        for p in (m.group(1).split(",") if m else ["(missing)"]):
            check(os.path.isfile(os.path.join(ROOT, "installer", *p.strip().split("\\"))), f"{k}: {p.strip()} exists")
    check(os.path.isfile(os.path.join(ROOT, "installer", "art", "rc-uninstall.ico")), "art\\rc-uninstall.ico exists")

    # over-install and uninstall
    ins, uni = entries(section(t, "InstallDelete")), entries(section(t, "UninstallDelete"))
    check({p for ty, p in ins if ty == "filesandordirs"} == dirs, "[InstallDelete] replaces exactly the payload folders",
          sorted({p for _, p in ins} ^ dirs))
    check({p for ty, p in uni if ty == "filesandordirs" and not p.startswith(".")} == dirs,
          "[UninstallDelete] removes exactly the payload folders (whole, with __pycache__)")
    leftovers = {"launcher.exe.debug", catalog.BUILD_LOCK.lower(), kit.STAGING.lower(), kit.DOWNLOADS.lower()}
    check(leftovers <= {p for _, p in uni}, "[UninstallDelete] has the debug file, the build lock and the kit's temp folders",
          sorted(leftovers - {p for _, p in uni}))
    check("launcher.exe.debug" in open(os.path.join(HERE, "build_launcher.sh"), encoding="utf-8").read(),
          "the debug file name is build_launcher.sh's")
    check(all(ty in ("files", "filesandordirs") for ty, _ in ins + uni), "only files / filesandordirs entries")
    data = player_data()
    for d in data:
        dl = d.lower()
        hit = [p for _, p in ins + uni if p == dl or p.startswith(dl + "/")]
        check(not hit and dl not in have_tops, f"player data {d}: not in the payload, [InstallDelete] or [UninstallDelete]", hit)
        check(f"App + '\\{d}'" in code, f"player data {d}: removed only by the uninstaller's question")
    m = re.search(r"procedure CurUninstallStepChanged.*?\nend;", code, re.S)
    body = m.group(0) if m else ""
    check("if UninstallSilent then Exit;" in body and body.count("MB_DEFBUTTON2") == 2 and "usPostUninstall" in body
          and body.index("HasPlayerData(App)") < body.index("DeletePlayerData(App)"),
          "uninstall asks (default No) before deleting data; a silent uninstall keeps it")
    check("PS3Recomp" not in code.replace("(%APPDATA%\\PS3Recomp) is never touched", ""),
          "the folder shared with the other ports is never deleted")

    # install folder rules
    for line in ("PrivilegesRequired=lowest", "PrivilegesRequiredOverridesAllowed=",
                 "DefaultDirName={localappdata}\\Programs\\{#AppDirName}", "AppendDefaultDirName=yes",
                 "TimeStampsInUTC=yes", "DisableDirPage=no", "SetupLogging=yes", "ArchitecturesInstallIn64BitMode=x64compatible"):
        check(re.search(rf"^{re.escape(line)}\s*$", t, re.M) is not None, f"[Setup] {line}")
    check("(CurPageID = wpSelectDir) and (not WizardSilent)" in code
          and "WizardForm.DirEdit.Text := AddBackslash(D) + '{#AppDirNameP}'" in code, "the folder page appends the app's name")
    check(re.search(r"DirExists\(D\) and DirHasFiles\(D\) and \(not FileExists\(AddBackslash\(D\) \+ 'unins000\.exe'\)\)", code),
          "a non-empty folder that is not ours is refused")
    m = re.search(r"function InstallDirProblem.*?\nend;", code, re.S)
    check(m and "HasCMakeBadChar" in m.group(0) and "InOneDrive(D)" in m.group(0) and "IsProtectedFolder(D)" in m.group(0),
          "InstallDirProblem: [ ] ;, OneDrive, system folders")
    m = re.search(r"function PrepareToInstall.*?\nend;", code, re.S)
    check(m and "InstallDirProblem(ExpandConstant('{app}'))" in m.group(0), "silent /DIR= gets the same rules")

    # wizard
    tasks = section(t, "Tasks")
    check(re.search(r'^Name: "desktopicon";', tasks, re.M) and "unchecked" not in tasks, "desktop shortcut task, ticked")
    icons = section(t, "Icons")
    check('Name: "{group}\\{#AppDirName}"; Filename: "{app}\\{#LauncherExe}"' in icons
          and 'Name: "{autodesktop}\\{#AppDirName}"' in icons and "Tasks: desktopicon" in icons, "Start-menu and desktop entries")
    run = section(t, "Run")
    check('Filename: "{app}\\{#LauncherExe}"' in run and "postinstall" in run and "skipifsilent" in run,
          "finish page starts the launcher (not in a silent install)")
    check(not re.search(r"CreateInput(Dir|File|Option)Page|DownloadTemporaryFile|CreateDownloadPage|firstrun\.py|Exec\(", code),
          "no game pages, no build step, no download, nothing run")
    lower = t.lower()
    bad = [e for e in pack_zip.GAME_EXTS if e in lower and e not in (".elf",)] + \
        (["eboot"] if re.search(r"\beboot\b", lower) else [])
    check(not bad, "no game file type is named", bad)
    words = re.findall(r"(?i)\b(maintainer|lanes?|PM|project manager)\b|/home/", t)
    check(not words, "public wording: no internal words, no private path", words)

    # Pascal hygiene
    braced = [c for c in re.findall(r"\{[^}]*\}", code) if "{" in c[1:] and not c.startswith("{#")]
    check(not braced, "no '{' inside a { } comment in [Code]", braced[:3])
    check(not [ln for ln in code.splitlines() if re.match(r"\s*#\d", ln)], "no [Code] line starts with #NN")
    check("{#AppDirName}" not in code and "{#AppName}" not in code,
          "[Code] uses AppDirNameP (the apostrophe doubled) inside Pascal strings")
    check('#define AppDirNameP StringChange(AppDirName, "\'", "\'\'")' in t, "AppDirNameP doubles the apostrophe")
    check(t.splitlines()[1].lower().count("killzone 3") == 1 and "MIT" in "\n".join(t.splitlines()[:3]),
          "header names the source (kz3.iss, MIT)")
    print("test_rc_iss:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

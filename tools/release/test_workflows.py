#!/usr/bin/env python3
"""Host test for the GitHub workflows and dependabot.yml (public/.github in this repository, .github in the public
tree). Text checks only (standard library, no YAML parser, no network). Adapted from the Killzone 2 and Killzone 3
ports' tools/release/test_workflows.py (same author, MIT).

  - every `uses:` of an action is pinned to a full 40-hex commit SHA with its version in a comment; every checkout
    keeps no credentials (persist-credentials: false); top-level permissions are set; no secret beyond the run's own
    token; no privileged trigger (pull_request_target, workflow_run);
  - release.yml: a launcher-vX.Y.Z or <catalogue game id>-vX.Y.Z tag push or workflow_dispatch only; read-only by default and every job sets its own
    permissions; only the publish job can write, it runs on tag pushes only and checks out no code; concurrency per
    ref without cancel-in-progress; the tag must be v + names.VERSION in the build and in publish; the notes come from
    the CHANGELOG (release_notes.py) before the toolchain is fetched and travel in the artifact as release-body.md;
    every file publish uses is in the artifact the launcher job uploads; `gh release create TAG --draft --latest
    --verify-tag --title TAG --notes-file release-body.md`, never --prerelease; an existing release is refused;
    no --clobber, upload, edit or delete, no tag or push; SHA256SUMS (setup exe + three zips, no per-file .sha256
    since 0.3.1) checked before the release is created; the gh CLI runs outside a checkout with GH_REPO set; the zip bundles the pinned Python
    embeddable package (PSF credited in the header); the toolchain manifest is checked
    --strict; only the imgui submodule is fetched and ps3recomp-patched.sh is not run; no game file is named;
  - release.yml (2026-11-03 review): the version mirrors are grepped with the dots escaped (the step is run in a temp
    tree: 0x2x0 must not pass for 0.2.0); the built exe's imports are checked (pe_imports.py --allow SDL3.dll);
    the Windows-host toolchain only; the 14-day artifact retention is explained;
  - release.yml installer job (0.3.1): after the launcher job, read-only plus actions: read, no rebuild (the
    full build zip from the launcher artifact), Inno Setup pinned by version and SHA-256 from toolchain.json build_tools,
    ISCC on installer/rc.iss with the payload and version, the setup exe (names.setup_exe_name) uploaded and published;
  - ci.yml: runs the release tools' host tests (every tools/release/test_*.py) and fetch_toolchain.py --check --strict,
    and cross-compiles the launcher (launcher-build) with the pinned Linux llvm-mingw and checks its imports;
  - dependabot.yml: github-actions updates, weekly, grouped.

    python3 tools/release/test_workflows.py
"""
import glob
import json
import os
import re
import shutil
import subprocess
import tempfile
import textwrap
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GH = os.path.join(ROOT, "public", ".github")
if not os.path.isdir(GH):
    GH = os.path.join(ROOT, ".github")          # the public tree ships public/ at its root
WF = os.path.join(GH, "workflows")
FAILS = []
USES = re.compile(r"^\s*(?:-\s+)?uses:\s*(\S+)(.*)$")


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"\n     {str(detail)[:600]}"))
    if not cond:
        FAILS.append(what)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def jobs(text):
    """-> {job name: its text} for a workflow's top-level `jobs:` block (two-space indented job keys)."""
    m = re.search(r"^jobs:\n", text, re.M)
    if not m:
        return {}
    body, out = text[m.end():], {}
    parts = re.split(r"^  ([A-Za-z0-9_-]+):\s*\n", body, flags=re.M)
    for i in range(1, len(parts) - 1, 2):
        out[parts[i]] = parts[i + 1]
    return out


def code(text):
    """The text without comment lines (so a comment naming a forbidden command does not count)."""
    return "\n".join(ln for ln in text.splitlines() if not ln.lstrip().startswith("#"))


def test_common(files):
    for p in files:
        name = os.path.basename(p)
        t = read(p)
        check(re.search(r"^permissions:", t, re.M) is not None, f"{name}: top-level permissions set")
        for line in t.splitlines():
            m = USES.match(line)
            if not m or m.group(1).startswith("./"):
                continue
            ref, rest = m.group(1), m.group(2)
            ok = re.fullmatch(r"[\w.-]+/[\w./-]+@[0-9a-f]{40}", ref) is not None and re.search(r"#\s*v\d", rest)
            check(bool(ok), f"{name}: {ref.split('@')[0]} pinned to a commit SHA with its version", line.strip())
        secrets = set(re.findall(r"secrets\.([A-Za-z_][A-Za-z0-9_]*)", t)) - {"GITHUB_TOKEN"}
        check(not secrets, f"{name}: no secret beyond the run's own token", secrets)
        for m in re.finditer(r"uses: actions/checkout@\S+[^\n]*\n((?:\s{8,}[^\n]*\n)*)", t):
            check("persist-credentials: false" in m.group(1), f"{name}: checkout keeps no credentials", m.group(0))
        check("pull_request_target" not in t and "workflow_run" not in t, f"{name}: no privileged trigger")


def upload_paths(job):
    m = re.search(r"uses: actions/upload-artifact@\S+[^\n]*\n(?:.*\n)*?\s+path: \|\n((?:\s{12,}\S[^\n]*\n)+)", job)
    return [ln.strip() for ln in m.group(1).splitlines()] if m else []


def test_installer_job(ij, pub):
    """The installer job (0.3.1)."""
    check("needs: launcher" in ij and "if: github.event_name == 'workflow_dispatch' || startsWith(github.ref_name, 'launcher-v')" in ij,
          "release.yml: installer runs after the launcher job, on launcher tags and the dry run")
    perms = re.search(r"^    permissions:\n((?:      [^\n]*\n)+)", ij, re.M)
    pl = re.findall(r"^      (\w[\w-]*):\s*(\w+)", perms.group(1), re.M) if perms else []
    check(sorted(pl) == [("actions", "read"), ("contents", "read")],
          "release.yml: installer is read-only (contents: read, actions: read for gh run download)", pl)
    check("runs-on: windows-2022" in ij and "core.autocrlf false" in ij and ij.index("core.autocrlf") < ij.index("actions/checkout"),
          "release.yml: installer on windows-2022, LF kept before checkout")
    check('gh run download "$GITHUB_RUN_ID" --name "$ARTIFACT"' in ij and "ARTIFACT: ${{ needs.launcher.outputs.artifact }}" in ij
          and "BUILD_ZIP: ${{ needs.launcher.outputs.build_zip }}" in ij and "-m zipfile -e" in ij
          and "build_launcher.sh" not in ij and "pack_zip.py" not in ij and "fetch_toolchain.py" not in ij,
          "release.yml: installer unpacks the launcher job's full build zip, no rebuild")
    with open(os.path.join(ROOT, "tools", "release", "toolchain.json"), encoding="utf-8") as f:
        pin = [b for b in json.load(f).get("build_tools", []) if b["name"] == "innosetup"]
    ver = re.search(r'INNO_VERSION: "([\d.]+)"', ij)
    check(len(pin) == 1 and re.fullmatch(r"[0-9a-f]{64}", pin[0]["sha256"]) and ver and ver.group(1) == pin[0]["version"]
          and pin[0]["url"].startswith("https://github.com/jrsoftware/issrc/releases/download/"),
          "release.yml: INNO_VERSION equals the toolchain.json build_tools pin (official jrsoftware/issrc release, SHA-256)",
          (pin, ver and ver.group(1)))
    check("ConvertFrom-Json).build_tools" in ij and "Get-FileHash $exe -Algorithm SHA256" in ij
          and "if ($got -ne $pin.sha256)" in ij and ij.index("if ($got -ne $pin.sha256)") < ij.index("Start-Process $exe"),
          "release.yml: the Inno Setup download is hash-checked before it runs")
    check('"/DAppVersion=$env:VERSION" "/DPayload=$env:PAYLOAD"' in ij and "installer\\rc.iss" in ij
          and "SETUP_EXE: ${{ needs.launcher.outputs.setup_exe }}" in ij and "names.setup_exe_name(sys.argv[1])" in js_launcher_text(),
          "release.yml: ISCC compiles installer/rc.iss with the version and payload; the setup name comes from names.py")
    check("python3 tools/release/test_rc_iss.py" in ij, "release.yml: installer runs the rc.iss checks first")
    check("path: build/release-out/${{ needs.launcher.outputs.setup_exe }}" in ij and "name: ${{ steps.names.outputs.artifact }}" in ij
          and "artifact: ${{ steps.names.outputs.artifact }}" in ij and "SETUP_ARTIFACT: ${{ needs.installer.outputs.artifact }}" in pub
          and 'gh run download "$GITHUB_RUN_ID" --name "$SETUP_ARTIFACT"' in pub,
          "release.yml: the setup exe travels in its own artifact to publish")


def js_launcher_text():
    return jobs(read(os.path.join(WF, "release.yml"))).get("launcher", "")


def test_release():
    t = read(os.path.join(WF, "release.yml"))
    c = code(t)
    head = "\n".join(t.splitlines()[:3])
    check("adapted from Project Helghast" in head and "Killzone 3" in head and "MIT" in head,
          "release.yml: header credits the Killzone 3 port's workflow (MIT)")
    on = t[t.index("\non:"):t.index("\npermissions:")]
    tags = re.findall(r'^      - "([^"]+)"', on, re.M)
    with open(os.path.join(ROOT, "data", "titles", "catalog.json"), encoding="utf-8") as f:
        cat = json.load(f)
    ids = [g["id"] for e in cat["games"] for g in [e] + list(e.get("children", [])) if not g.get("children")]
    check(tags == [f"{i}-v[0-9]+.[0-9]+.[0-9]+" for i in ["launcher"] + ids] and "workflow_dispatch:" in on
          and "branches" not in on and "pull_request" not in on,
          "release.yml: runs on launcher-vX.Y.Z, <catalogue game id>-vX.Y.Z (every id, in catalogue order) or a dispatch only",
          (tags, ids))
    check(re.search(r"^permissions:\n  contents: read", t, re.M) is not None, "release.yml: read-only by default")
    check(re.search(r"^concurrency:\n  group: release-\$\{\{ github\.ref \}\}\n  cancel-in-progress: false", t, re.M)
          is not None, "release.yml: one run per ref, never cancelled half-way")
    js = jobs(t)
    check(set(js) == {"launcher", "installer", "publish", "game-notes", "game-publish"},
          "release.yml: launcher, installer, publish, game-notes and game-publish jobs", list(js))
    for job, body in js.items():
        check(re.search(r"^    permissions:", body, re.M) is not None, f"release.yml: job {job} sets its own permissions")
    writers = [j for j, b in js.items() if re.search(r"contents:\s*write", b)]
    check(sorted(writers) == ["game-publish", "publish"], "release.yml: only the two publish jobs can write", writers)
    lj, pub = js.get("launcher", ""), js.get("publish", "")
    check("contents: write" not in lj and "actions: write" not in lj, "release.yml: the build job is read-only")
    check("if: github.event_name == 'push' && github.ref_type == 'tag' && startsWith(github.ref_name, 'launcher-v')" in pub
          and "actions/checkout" not in pub and "needs: [launcher, installer]" in pub,
          "release.yml: publish runs on launcher tag pushes only, after the build, with no checkout")
    check("if: github.event_name == 'workflow_dispatch' || startsWith(github.ref_name, 'launcher-v')" in lj,
          "release.yml: the launcher builds on a launcher tag or the dry run only (a game tag builds nothing)")
    test_game_jobs(js.get("game-notes", ""), js.get("game-publish", ""))
    test_installer_job(js.get("installer", ""), pub)
    check("runs-on: windows-latest" in lj and "git config --global core.autocrlf false" in lj
          and lj.index("core.autocrlf") < lj.index("actions/checkout"), "release.yml: Windows runner, LF kept before checkout")
    # version: tag == names.VERSION in both jobs
    check('if [ "$v" != "$want" ]' in lj and "names.VERSION" in lj, "release.yml: build job checks tag == names.VERSION")
    check('[ "$GITHUB_REF_NAME" = "launcher-v$VERSION" ]' in pub, "release.yml: publish checks tag == launcher-v + VERSION")
    check('release_notes.py --tag "launcher-v$VERSION"' in lj, "release.yml: launcher notes from release_notes.py --tag launcher-vX.Y.Z")
    # notes early and in the artifact
    check("release_notes.py" in lj and lj.index("release_notes.py") < lj.index("fetch_toolchain.py"),
          "release.yml: the notes are written before the toolchain and the build")
    check("--out build/release-out/release-body.md" in lj, "release.yml: notes file is release-body.md")
    paths = upload_paths(lj)
    check(paths != [], "release.yml: the build uploads an artifact with a path list", paths)
    need = {"release-body.md": "release-body.md", '"$ZIP"': "${{ steps.ver.outputs.zip }}",
            '"$BUILD_ZIP"': "${{ steps.ver.outputs.build_zip }}",
            '"$SYMBOLS_ZIP"': "${{ steps.ver.outputs.symbols_zip }}"}
    for used, path in need.items():
        check(used in pub and any(p.endswith(path) for p in paths),
              f"release.yml: publish uses {used}, and the artifact carries it", paths)
    check("--name \"$ARTIFACT\"" in pub and "artifact: ${{ steps.ver.outputs.artifact }}" in lj
          and "name: ${{ steps.ver.outputs.artifact }}" in lj, "release.yml: publish downloads the artifact the build named")
    check("ZIP: ${{ needs.launcher.outputs.zip }}" in pub and "zip: ${{ steps.ver.outputs.zip }}" in lj
          and "BUILD_ZIP: ${{ needs.launcher.outputs.build_zip }}" in pub and "build_zip: ${{ steps.ver.outputs.build_zip }}" in lj
          and "SYMBOLS_ZIP: ${{ needs.launcher.outputs.symbols_zip }}" in pub
          and "symbols_zip: ${{ steps.ver.outputs.symbols_zip }}" in lj
          and "names.symbols_zip_name(sys.argv[1])" in lj,
          "release.yml: all three zip names (names.py) travel as job outputs")
    # the release command
    cmd = re.search(r"gh release create ((?:[^\n]*\\\n)*[^\n]*)", c)
    rc = cmd.group(1) if cmd else ""
    check(cmd is not None and re.match(r'"\$GITHUB_REF_NAME" --draft --latest --verify-tag --title "Launcher v\$VERSION" '
                                       r"--notes-file release-body\.md", rc) is not None,
          "release.yml: gh release create TAG --draft --latest --verify-tag --title 'Launcher vX.Y.Z' --notes-file release-body.md", rc)
    check("--prerelease" not in pub and "$pre" not in pub,
          "release.yml: the launcher release is a full release at every version, never a pre-release")
    check(re.search(r'"\$SETUP_EXE" "\$ZIP" "\$BUILD_ZIP" "\$SYMBOLS_ZIP" SHA256SUMS\s*$', rc) is not None,
          "release.yml: assets are the setup exe, the launcher, build and symbols zips and SHA256SUMS", rc)
    check(not re.search(r"(?<!pin)\.sha256\b", code(t)), "release.yml: no per-file .sha256 is made, uploaded or published (0.3.1)")
    check(not re.search(r"--clobber|gh release (upload|edit|delete)|git (tag|push)|git push", c),
          "release.yml: no clobber, upload, edit or delete; no tag or push")
    check("gh release view" in pub and pub.index("gh release view") < pub.rindex("gh release create"),
          "release.yml: an existing release is refused before anything is created")
    check('sha256sum "$SETUP_EXE" "$ZIP" "$BUILD_ZIP" "$SYMBOLS_ZIP" > SHA256SUMS' in pub and "sha256sum -c SHA256SUMS" in pub
          and pub.index("sha256sum -c SHA256SUMS") < pub.rindex("gh release create"),
          "release.yml: SHA256SUMS lists the setup exe and the three zips and is verified before the release")
    check("> SHA256SUMS" in pub, "release.yml: SHA256SUMS written beside the assets")
    check('"$RUNNER_TEMP/release-assets"' in pub and "GH_REPO: ${{ github.repository }}" in pub,
          "release.yml: assets downloaded outside any checkout, gh told the repository")
    check("sha256sum ./*.zip" in lj, "release.yml: the build prints each zip's SHA-256")
    # toolchain and inputs
    check("fetch_toolchain.py --check --strict" in lj, "release.yml: the toolchain manifest is checked --strict")
    check("bump_version.py --agree" in lj, "release.yml: shared version checker covers the catalog mirror")
    check("git submodule update --init --depth 1 third_party/imgui third_party/ps3recomp" in lj
          and "submodules:" not in code(lj) and "bash tools/ps3recomp-patched.sh" in lj,
          "release.yml: the imgui and ps3recomp submodules only; the patched ps3recomp is the build kit's toolkit")
    kit = "pack_zip.py --make-kit-manifest build/kit/kit.json"
    check(kit in lj and "LAUNCHER_KIT_MANIFEST: build/kit/kit.json" in lj
          and lj.index("ps3recomp-patched.sh") < lj.index(kit) < lj.index("build_launcher.sh")
          and "--kit-manifest build/kit/kit.json --toolkit work/ps3recomp-patched" in lj,
          "release.yml: kit.json is made before the launcher, which is built with it, and the full zip is packed with it")
    check(not re.search(r"EBOOT|\.elf\b|\.self\b|\.psarc|library/", c), "release.yml: no game file named")
    # version mirrors: the version goes into the regular expressions with its dots escaped
    greps = re.findall(r'grep -qE? "((?:[^"\\]|\\.)*)"', lj)
    check(len(greps) == 3 and all("$ve" in g and not re.search(r"\$v(?![e\w])", g) for g in greps)
          and "ve=$(printf '%s' \"$v\" | sed 's/[.]/\\\\./g')" in lj,
          "release.yml: version greps use $ve (dots escaped), never a bare $v", greps)
    test_version_step(lj)
    # imports of the built exe: after the build, before the zip
    pe = "python3 tools/release/pe_imports.py --allow SDL3.dll build/launcher-release/launcher.exe"
    check(pe in lj and lj.index("build_launcher.sh") < lj.index(pe) < lj.index("pack_zip.py --exe"),
          "release.yml: the built exe's imports are checked (system DLLs + SDL3.dll) before it is zipped")
    check(re.search(r"fetch_toolchain\.py --only llvm-mingw,sdl3,cmake,ninja,python,[a-z0-9,.-]+\n", lj) is not None,
          "release.yml: fetches the Windows-host toolchain only")
    check(re.search(r"#[^\n]*14 days[^\n]*\n(?:\s*#[^\n]*\n)*\s*retention-days: 14", lj) is not None,
          "release.yml: the 14-day artifact retention is explained")
    check('LAUNCHER_SYMBOLS: "1"' in lj and "--symbols build/launcher-release/launcher.exe.debug" in lj
          and lj.index("pe_imports.py") < lj.index("pack_zip.py --exe"),
          "release.yml: the launcher is built with its debug file split off and the symbols zip is packed from it")
    check("--python work/release/toolchain-cache/trees/python" in lj and "Python Software Foundation" in t[:t.index("\nname:")],
          "release.yml: the zip bundles the pinned Python embeddable package, credited to the PSF in the header")
    check("for t in test_names" in lj and "test_workflows" in lj, "release.yml: host tests of the release tools run")


def test_game_jobs(gn, gp):
    """Game tags: notes only, a read-only notes job and a write job with no checkout; never latest; no asset."""
    cond = "if: github.event_name == 'push' && github.ref_type == 'tag' && !startsWith(github.ref_name, 'launcher-v')"
    check(cond in gn and cond in gp and "needs: game-notes" in gp, "release.yml: game jobs run on game tag pushes only")
    check("contents: read" in gn and "contents: write" not in gn and "actions/checkout" not in gp,
          "release.yml: game-notes is read-only; game-publish checks out no code")
    check('if [ "$v" != "$want" ]' in gn and "names.VERSION" in gn,
          "release.yml: a game tag's version must equal names.VERSION (the release whose kit builds it)")
    check('release_notes.py --tag "$GITHUB_REF_NAME"' in gn and "--title-out build/release-out/release-title.txt" in gn,
          "release.yml: game notes and title from release_notes.py --tag")
    paths = upload_paths(gn)
    check(paths == ["build/release-out/release-body.md", "build/release-out/release-title.txt"]
          and "--name game-release-notes" in gp and "name: game-release-notes" in gn,
          "release.yml: the game notes travel in their own artifact", paths)
    c = code(gp)
    cmd = re.search(r"gh release create ((?:[^\n]*\\\n)*[^\n]*)", c)
    rc = re.sub(r"\\\n\s*", "", cmd.group(1)) if cmd else ""
    check(rc.strip() == '"$GITHUB_REF_NAME" --draft --prerelease --latest=false --verify-tag --title "$title" '
                        '--notes-file release-body.md',
          "release.yml: a game release is a draft pre-release, never latest, notes only, no asset", rc)
    check("gh release view" in gp and gp.index("gh release view") < gp.rindex("gh release create")
          and not re.search(r"--clobber|gh release (upload|edit|delete)|git (tag|push)", c),
          "release.yml: game-publish refuses an existing release; no clobber, upload, edit, delete, tag or push")


def test_version_step(lj):
    """Run the build job's version step (bash) in a temp tree: the right mirrors pass; a mirror whose version only
    matches with unescaped dots (0x2y0 for 0.2.0) and a wrong tag fail."""
    bash = shutil.which("bash")
    m = re.search(r"id: ver\n\s+run: \|\n((?:          .*\n|\n)+)", lj)
    if not bash or not m:
        check(m is not None, "release.yml: version step found")
        print("skip version step run (no bash)")
        return
    script = textwrap.dedent(m.group(1))
    names_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), "names.py")

    def run(cmake_ver, tag, catalog_ver=lambda v: v):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "tools", "release"))
            os.makedirs(os.path.join(d, "launcher"))
            os.makedirs(os.path.join(d, "port", "rc"))
            os.makedirs(os.path.join(d, "data", "titles"))
            shutil.copy(names_py, os.path.join(d, "tools", "release", "names.py"))
            shutil.copy(os.path.join(os.path.dirname(names_py), "bump_version.py"),
                        os.path.join(d, "tools", "release", "bump_version.py"))
            sys.path.insert(0, os.path.dirname(names_py))
            import names
            v = names.VERSION
            with open(os.path.join(d, "data", "titles", "catalog.json"), "w") as f:
                json.dump({"port_version": catalog_ver(v)}, f)
            with open(os.path.join(d, "launcher", "CMakeLists.txt"), "w") as f:
                f.write(f"project(qwark_launcher VERSION {cmake_ver(v)} LANGUAGES CXX)\n")
            with open(os.path.join(d, "port", "rc", "CMakeLists.txt"), "w") as f:
                f.write(f"project(x\n    VERSION {v}\n)\n")
            with open(os.path.join(d, "CITATION.cff"), "w") as f:
                f.write(f"version: {v}\n")
            env = dict(os.environ, GITHUB_OUTPUT=os.path.join(d, "out.txt"), GITHUB_EVENT_NAME="push",
                       GITHUB_REF_TYPE="tag", GITHUB_REF_NAME=tag(v))
            # the script goes in a file, as the runner does: run as a -c argument, the Windows runner's check failed
            # (0x2x0 matched); ASSUMPTION: the MSYS runtime re-parses the argument and turns the sed replacement's \\ into \
            sp = os.path.join(d, "step.sh")
            with open(sp, "w", newline="\n") as f:
                f.write(script)
            r = subprocess.run([bash, "-e", "step.sh"], cwd=d, env=env, capture_output=True, text=True, timeout=60)
            return r.returncode, r.stdout + r.stderr

    rc, out = run(lambda v: v, lambda v: "launcher-v" + v)
    check(rc == 0, "release.yml version step: matching tag and mirrors pass", out)
    rc, out = run(lambda v: v.replace(".", "x"), lambda v: "launcher-v" + v)
    check(rc != 0 and "launcher/CMakeLists.txt" in out, "release.yml version step: 0x2x0 is not 0.2.0 (dots escaped)", out)
    rc, out = run(lambda v: v, lambda v: "launcher-v9" + v)
    check(rc != 0, "release.yml version step: a tag that is not names.VERSION fails", out)
    rc, out = run(lambda v: v, lambda v: "v" + v)
    check(rc != 0 and "launcher-v" in out, "release.yml version step: a bare vX.Y.Z tag fails", out)
    rc, out = run(lambda v: v, lambda v: "tods-v" + v)
    check(rc != 0, "release.yml version step: a game tag does not build the launcher", out)
    rc, out = run(lambda v: v, lambda v: "launcher-v" + v, lambda v: "0.0.0")
    check(rc != 0 and "catalog.json" in out, "release.yml version step: stale catalog mirror refused", out)


def test_ci():
    t = read(os.path.join(WF, "ci.yml"))
    check("tools/release/test_*.py" in t, "ci.yml: runs every release-tool host test")
    check("fetch_toolchain.py --check --strict" in t, "ci.yml: checks the toolchain manifest --strict")
    lb = jobs(t).get("launcher-build", "")
    check("fetch_toolchain.py --only llvm-mingw-linux,sdl3" in lb and "tools/release/build_launcher.sh" in lb
          and "pe_imports.py --allow SDL3.dll build/launcher-release/launcher.exe" in lb
          and "git submodule update --init --depth 1 third_party/imgui" in lb and "submodules:" not in lb,
          "ci.yml: launcher-build cross-compiles the launcher with the pinned toolchain and checks its imports")


def test_dependabot():
    t = read(os.path.join(GH, "dependabot.yml"))
    check("version: 2" in t and "package-ecosystem: github-actions" in t, "dependabot.yml: github-actions updates")
    check("interval: weekly" in t and "groups:" in t, "dependabot.yml: weekly, grouped")


def main():
    files = sorted(glob.glob(os.path.join(WF, "*.yml")))
    have = {os.path.basename(p) for p in files}
    check({"ci.yml", "release.yml"} <= have, "workflows present: ci.yml, release.yml", sorted(have))
    test_common(files)
    test_release()
    test_ci()
    test_dependabot()
    print("test_workflows:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

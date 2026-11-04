#!/usr/bin/env python3
"""Host test for the GitHub workflows and dependabot.yml (public/.github in this repository, .github in the public
tree). Text checks only (standard library, no YAML parser, no network). Adapted from the Killzone 2 and Killzone 3
ports' tools/release/test_workflows.py (same author, MIT).

  - every `uses:` of an action is pinned to a full 40-hex commit SHA with its version in a comment; every checkout
    keeps no credentials (persist-credentials: false); top-level permissions are set; no secret beyond the run's own
    token; no privileged trigger (pull_request_target, workflow_run);
  - release.yml: a vX.Y.Z tag push or workflow_dispatch only; read-only by default and every job sets its own
    permissions; only the publish job can write, it runs on tag pushes only and checks out no code; concurrency per
    ref without cancel-in-progress; the tag must be v + names.VERSION in the build and in publish; the notes come from
    the CHANGELOG (release_notes.py) before the toolchain is fetched and travel in the artifact as release-body.md;
    every file publish uses is in the artifact the launcher job uploads; `gh release create TAG --draft $pre
    --verify-tag --title TAG --notes-file release-body.md`, 0.* gets --prerelease; an existing release is refused;
    no --clobber, upload, edit or delete, no tag or push; sha256sum -c on the .sha256 and SHA256SUMS before the
    release is created; the gh CLI runs outside a checkout with GH_REPO set; the zip bundles the pinned Python
    embeddable package (PSF credited in the header); the toolchain manifest is checked
    --strict; only the imgui submodule is fetched and ps3recomp-patched.sh is not run; no game file is named;
  - release.yml (2026-11-03 review): the version mirrors are grepped with the dots escaped (the step is run in a temp
    tree: 0x2x0 must not pass for 0.2.0); the built exe's imports are checked (pe_imports.py --allow SDL3.dll);
    the Windows-host toolchain only; the 14-day artifact retention is explained;
  - ci.yml: runs the release tools' host tests (every tools/release/test_*.py) and fetch_toolchain.py --check --strict,
    and cross-compiles the launcher (launcher-build) with the pinned Linux llvm-mingw and checks its imports;
  - dependabot.yml: github-actions updates, weekly, grouped.

    python3 tools/release/test_workflows.py
"""
import glob
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


def test_release():
    t = read(os.path.join(WF, "release.yml"))
    c = code(t)
    head = "\n".join(t.splitlines()[:3])
    check("adapted from Project Helghast" in head and "Killzone 3" in head and "MIT" in head,
          "release.yml: header credits the Killzone 3 port's workflow (MIT)")
    on = t[t.index("\non:"):t.index("\npermissions:")]
    check('tags: ["v[0-9]+.[0-9]+.[0-9]+"]' in on and "workflow_dispatch:" in on and "branches" not in on
          and "pull_request" not in on, "release.yml: runs on a vX.Y.Z tag or a dispatch only", on)
    check(re.search(r"^permissions:\n  contents: read", t, re.M) is not None, "release.yml: read-only by default")
    check(re.search(r"^concurrency:\n  group: release-\$\{\{ github\.ref \}\}\n  cancel-in-progress: false", t, re.M)
          is not None, "release.yml: one run per ref, never cancelled half-way")
    js = jobs(t)
    check(set(js) == {"launcher", "publish"}, "release.yml: launcher and publish jobs", list(js))
    for job, body in js.items():
        check(re.search(r"^    permissions:", body, re.M) is not None, f"release.yml: job {job} sets its own permissions")
    writers = [j for j, b in js.items() if re.search(r"contents:\s*write", b)]
    check(writers == ["publish"], "release.yml: only the publish job can write", writers)
    lj, pub = js.get("launcher", ""), js.get("publish", "")
    check("contents: write" not in lj and "actions: write" not in lj, "release.yml: the build job is read-only")
    check("if: github.event_name == 'push' && github.ref_type == 'tag'" in pub and "actions/checkout" not in pub
          and "needs: launcher" in pub, "release.yml: publish runs on tag pushes only, after the build, with no checkout")
    check("runs-on: windows-latest" in lj and "git config --global core.autocrlf false" in lj
          and lj.index("core.autocrlf") < lj.index("actions/checkout"), "release.yml: Windows runner, LF kept before checkout")
    # version: tag == names.VERSION in both jobs
    check('if [ "$v" != "$want" ]' in lj and "names.VERSION" in lj, "release.yml: build job checks tag == names.VERSION")
    check('[ "$GITHUB_REF_NAME" = "v$VERSION" ]' in pub, "release.yml: publish checks tag == v + VERSION")
    # notes early and in the artifact
    check("release_notes.py" in lj and lj.index("release_notes.py") < lj.index("fetch_toolchain.py"),
          "release.yml: the notes are written before the toolchain and the build")
    check("--out build/release-out/release-body.md" in lj, "release.yml: notes file is release-body.md")
    paths = upload_paths(lj)
    check(paths != [], "release.yml: the build uploads an artifact with a path list", paths)
    need = {"release-body.md": "release-body.md", '"$ZIP"': "${{ steps.ver.outputs.zip }}",
            '"$ZIP.sha256"': "${{ steps.ver.outputs.zip }}.sha256"}
    for used, path in need.items():
        check(used in pub and any(p.endswith(path) for p in paths),
              f"release.yml: publish uses {used}, and the artifact carries it", paths)
    check("--name \"$ARTIFACT\"" in pub and "artifact: ${{ steps.ver.outputs.artifact }}" in lj
          and "name: ${{ steps.ver.outputs.artifact }}" in lj, "release.yml: publish downloads the artifact the build named")
    check("ZIP: ${{ needs.launcher.outputs.zip }}" in pub and "zip: ${{ steps.ver.outputs.zip }}" in lj,
          "release.yml: the zip name travels as a job output")
    # the release command
    cmd = re.search(r"gh release create ((?:[^\n]*\\\n)*[^\n]*)", c)
    rc = cmd.group(1) if cmd else ""
    check(cmd is not None and re.match(r'"\$GITHUB_REF_NAME" --draft \$pre --verify-tag --title "\$GITHUB_REF_NAME" '
                                       r"--notes-file release-body\.md", rc) is not None,
          "release.yml: gh release create TAG --draft $pre --verify-tag --title TAG --notes-file release-body.md", rc)
    check(re.search(r'"\$ZIP" "\$ZIP\.sha256" SHA256SUMS\s*$', rc) is not None, "release.yml: assets are the zip, its .sha256, SHA256SUMS", rc)
    check('0.*) pre="--prerelease"' in pub, "release.yml: a 0.x version is a pre-release")
    check(not re.search(r"--clobber|gh release (upload|edit|delete)|git (tag|push)|git push", c),
          "release.yml: no clobber, upload, edit or delete; no tag or push")
    check("gh release view" in pub and pub.index("gh release view") < pub.rindex("gh release create"),
          "release.yml: an existing release is refused before anything is created")
    check('sha256sum -c "$ZIP.sha256"' in pub and "sha256sum -c SHA256SUMS" in pub
          and pub.index("sha256sum -c SHA256SUMS") < pub.rindex("gh release create"),
          "release.yml: the .sha256 and SHA256SUMS are verified before the release")
    check("> SHA256SUMS" in pub, "release.yml: SHA256SUMS written beside the assets")
    check('"$RUNNER_TEMP/release-assets"' in pub and "GH_REPO: ${{ github.repository }}" in pub,
          "release.yml: assets downloaded outside any checkout, gh told the repository")
    check("sha256sum -c" in lj, "release.yml: the build verifies the zip's .sha256")
    # toolchain and inputs
    check("fetch_toolchain.py --check --strict" in lj, "release.yml: the toolchain manifest is checked --strict")
    check("git submodule update --init --depth 1 third_party/imgui" in lj and "submodules:" not in code(lj)
          and "ps3recomp-patched.sh" not in c, "release.yml: only the imgui submodule; no ps3recomp toolkit")
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
    check(re.search(r"fetch_toolchain\.py --only llvm-mingw,sdl3,cmake,ninja,python\n", lj) is not None,
          "release.yml: fetches the Windows-host toolchain only")
    check(re.search(r"#[^\n]*14 days[^\n]*\n(?:\s*#[^\n]*\n)*\s*retention-days: 14", lj) is not None,
          "release.yml: the 14-day artifact retention is explained")
    check("--python work/release/toolchain-cache/trees/python" in lj and "Python Software Foundation" in t[:t.index("\nname:")],
          "release.yml: the zip bundles the pinned Python embeddable package, credited to the PSF in the header")
    check("for t in test_names" in lj and "test_workflows" in lj, "release.yml: host tests of the release tools run")


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

    def run(cmake_ver, tag):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "tools", "release"))
            os.makedirs(os.path.join(d, "launcher"))
            os.makedirs(os.path.join(d, "port", "rc"))
            shutil.copy(names_py, os.path.join(d, "tools", "release", "names.py"))
            sys.path.insert(0, os.path.dirname(names_py))
            import names
            v = names.VERSION
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

    rc, out = run(lambda v: v, lambda v: "v" + v)
    check(rc == 0, "release.yml version step: matching tag and mirrors pass", out)
    rc, out = run(lambda v: v.replace(".", "x"), lambda v: "v" + v)
    check(rc != 0 and "launcher/CMakeLists.txt" in out, "release.yml version step: 0x2x0 is not 0.2.0 (dots escaped)", out)
    rc, out = run(lambda v: v, lambda v: "v9" + v)
    check(rc != 0, "release.yml version step: a tag that is not names.VERSION fails", out)


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

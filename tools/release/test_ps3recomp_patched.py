#!/usr/bin/env python3
"""Host tests for the cross-platform ps3recomp patched-tree builder."""
import hashlib
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ps3recomp_patched", ROOT / "tools" / "ps3recomp_patched.py")
PATCHED = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHED)
sys.path.insert(0, str(ROOT / "tools" / "release"))
import firstrun  # noqa: E402
import firstrun_check  # noqa: E402
FAILS = 0


def check(condition, what, detail=""):
    global FAILS
    if not condition:
        FAILS += 1
    print(("ok " if condition else "FAIL ") + what + (f": {detail}" if detail and not condition else ""))


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8", newline="\n")


def fixture(top):
    root = top / "repo"
    source = root / "third_party" / "ps3recomp"
    patches = root / "port" / "patches"
    source.mkdir(parents=True)
    patches.mkdir(parents=True)
    git(root, "init", "-q")
    git(source, "init", "-q")
    git(source, "config", "user.name", "Test")
    git(source, "config", "user.email", "test@example.invalid")
    write(source / "CMakeLists.txt", "project(ps3recomp)\n")
    write(source / "value.txt", "before\n")
    write(source / "unchanged.txt", "same\n")
    # Upstream has files with mixed line endings (runtime/syscalls/sys_event.c); a patch's context then carries a CR.
    (source / "mixed.txt").write_bytes(b"one\ntwo\r\nthree\n")
    git(source, "add", ".")
    git(source, "commit", "-qm", "fixture")
    # A Git for Windows default and a working-tree edit: neither may change what is exported or how patches match.
    git(root, "config", "core.autocrlf", "true")
    git(source, "config", "core.autocrlf", "true")
    (source / "value.txt").write_bytes(b"before\r\n")
    write(
        patches / "0001-value.patch",
        """diff --git a/value.txt b/value.txt
--- a/value.txt
+++ b/value.txt
@@ -1 +1 @@
-before
+after
""",
    )
    (patches / "0002-mixed.patch").write_bytes(
        b"diff --git a/mixed.txt b/mixed.txt\n--- a/mixed.txt\n+++ b/mixed.txt\n"
        b"@@ -1,3 +1,3 @@\n one\n two\r\n-three\n+THREE\n"
    )
    return root, source


def test_prepare():
    with tempfile.TemporaryDirectory(prefix="rc-patched-") as tmp:
        root, source = fixture(pathlib.Path(tmp))
        write(root / "tools" / "ps3recomp_patched.py", "# fixture\n")
        check(firstrun_check.can_make_toolkit(root), "source toolkit can be prepared without Bash")

        class Context:
            toolkit_make = True
            py = ["python", "-X", "utf8"]

            def r(self, *parts):
                return os.path.join(root, *parts)

        check(firstrun.cmd_toolkit(Context()) == [
            ["python", "-X", "utf8", os.path.join(root, "tools", "ps3recomp_patched.py")]
        ], "first-run toolkit step invokes native Python")

        messages = []
        stamp = PATCHED.prepare(root, "work/ps3recomp-patched", messages.append)
        out = root / "work" / "ps3recomp-patched"
        check((out / "value.txt").read_bytes() == b"after\n", "patch is applied")
        check((out / "mixed.txt").read_bytes() == b"one\ntwo\r\nTHREE\n",
              "a patch with a CR in its context applies byte for byte (no CRLF normalisation)")
        raw = hashlib.sha256(b"".join(p.read_bytes() for p in sorted((root / "port" / "patches").glob("*.patch"))))
        check(stamp.split("-")[0] == raw.hexdigest()[:16],
              "stamp hashes the raw patch bytes, as tools/build-rc.sh and firstrun_check do")
        check((source / "value.txt").read_bytes() == b"before\r\n", "CRLF submodule checkout stays untouched")
        check((out / ".rc-patchset").read_text(encoding="utf-8").strip() == stamp, "patch-set stamp is written")
        check(stamp.endswith("-" + git(source, "rev-parse", "--short", "HEAD")), "stamp names the pinned commit")
        check(any(line.startswith("applied port/patches/") for line in messages), "clean apply is reported")

        kept = out / "unchanged.txt"
        fixed_time = 946684800
        os.utime(kept, (fixed_time, fixed_time))
        write(out / "stale.txt", "remove me\n")
        PATCHED.prepare(root, "work/ps3recomp-patched", lambda *_: None)
        check(int(kept.stat().st_mtime) == fixed_time, "unchanged output keeps its timestamp")
        check(not (out / "stale.txt").exists(), "stale output file is removed")
        check(not pathlib.Path(str(out) + ".new").exists(), "scratch tree is removed")


def test_patches_not_converted():
    attrs = (ROOT / ".gitattributes").read_text(encoding="utf-8").split("\n")
    check("port/patches/*.patch -text" in attrs, ".gitattributes keeps patches out of line-ending conversion")


def test_duplicate_refused():
    with tempfile.TemporaryDirectory(prefix="rc-patched-dup-") as tmp:
        root, _ = fixture(pathlib.Path(tmp))
        patch = root / "port" / "patches" / "0002-duplicate.patch"
        write(
            patch,
            """diff --git a/a b/a
--- a/a
+++ b/repeated.txt
diff --git a/b b/b
--- a/b
+++ b/repeated.txt
""",
        )
        try:
            PATCHED.prepare(root, "work/ps3recomp-patched", lambda *_: None)
        except PATCHED.PatchError as exc:
            check("names a file twice" in str(exc), "duplicate patch path is refused", str(exc))
        else:
            check(False, "duplicate patch path is refused")


test_prepare()
test_patches_not_converted()
test_duplicate_refused()
print("test_ps3recomp_patched:", "FAIL" if FAILS else "PASS")
raise SystemExit(1 if FAILS else 0)

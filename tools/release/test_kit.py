#!/usr/bin/env python3
"""Host test for tools/release/kit.py (everything the launcher downloads). No internet: downloads come from file://
URLs and from a test HTTP server on 127.0.0.1, both allowed only with --allow-local; zips and PE files are synthetic.
Exit 0 = PASS.

    python3 tools/release/test_kit.py

Cases: kit.py imports only the standard library (the bundled Python runs it by path); URL rules (GitHub release
downloads and api.github.com only; file:// and localhost only with allow_local; the final host after redirects must be
GitHub's); status (none / missing / other / damaged / ready); install: a good kit, then refusals that change nothing
(a kit.json of another hash, a tampered file, a missing file, a wrong size, a launcher file or an unsafe path in the
list, a zip without one top folder), a reinstall that removes the files the new kit dropped and keeps the launcher's
own; fetch over HTTP with PROGRESS / RESULT lines and over file://, refused without allow_local, cancelled by the stop
file (exit 130, nothing installed, download removed), refused when larger than the cap; symbols: the debug file is
taken only when its CRC-32 is the one the exe's .gnu_debuglink names; check-release: newest non-draft release,
pre-releases only when asked, "newer" against the current version, served over HTTP.
"""
import ast
import hashlib
import http.server
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import threading
import urllib.request
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kit  # noqa: E402

FAILS = []


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:800]))
    if not cond:
        FAILS.append(what)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


FILES = {"port/rc/CMakeLists.txt": b"project(rc)\n", "ps3recomp/CMakeLists.txt": b"project(ps3recomp)\n",
         "tools/release/firstrun.py": b"pass\n", "cmake/llvm-mingw.cmake": b"# tc\n",
         "toolchain/ninja/ninja.exe": b"MZ ninja", "licenses/ninja/COPYING.txt": b"Apache\n"}


def manifest(files):
    return {"format": 1, "release": "1.2.3", "files": [{"path": p, "sha256": hashlib.sha256(d).hexdigest(), "size": len(d)}
                                                       for p, d in sorted(files.items())]}


def make_zip(path, files, m=None, top="Top-1.2.3", extra=None, raw_manifest=None):
    """A full build zip: the launcher's own files, the kit files, kit.json."""
    m = m or manifest(files)
    with zipfile.ZipFile(path, "w") as z:
        for p, d in (("Launcher.exe", b"MZ launcher from the zip"), ("python/python.exe", b"MZ python")):
            if p not in files:
                z.writestr(f"{top}/{p}", d)
        for p, d in files.items():
            z.writestr(f"{top}/{p}", d)
        for p, d in (extra or {}).items():
            z.writestr(p if p.startswith(top) or "/" not in p else f"{top}/{p}", d)
        z.writestr(f"{top}/kit.json", raw_manifest if raw_manifest is not None else kit.manifest_bytes(m))
    data = raw_manifest if raw_manifest is not None else kit.manifest_bytes(m)
    return hashlib.sha256(data).hexdigest()


def bare_root(tmp, name):
    root = os.path.join(tmp, name)
    write(os.path.join(root, "Launcher.exe"), b"MZ the running launcher")
    write(os.path.join(root, "python", "python.exe"), b"MZ bundled python")
    return root


def snapshot(root):
    out = {}
    for d, _, fs in os.walk(root):
        for n in fs:
            p = os.path.join(d, n)
            out[os.path.relpath(p, root)] = open(p, "rb").read()
    return out


def test_imports():
    with open(os.path.join(HERE, "kit.py"), encoding="utf-8") as f:
        tree = ast.parse(f.read())
    roots = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            roots |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom):
            roots.add((node.module or ".").split(".")[0] if not node.level else ".")
    std = getattr(sys, "stdlib_module_names", None)
    local = {os.path.splitext(n)[0] for n in os.listdir(HERE) if n.endswith(".py")}
    check(not (roots & local) and "." not in roots, "kit.py imports no module of tools/release", roots & local)
    if std is not None:
        check(roots <= set(std), "kit.py imports the standard library only", sorted(roots - set(std)))


def test_urls():
    ok = kit.url_ok
    check(ok("https://github.com/o/r/releases/download/v1.2.3/x.zip", False), "a GitHub release download is allowed")
    for bad in ("http://github.com/o/r/releases/download/v1/x.zip", "https://github.com.evil.example/o/r/releases/download/x",
                "https://example.com/o/r/releases/download/v1/x.zip", "https://github.com/o/r/archive/x.zip",
                "https://user:pw@github.com/o/r/releases/download/v1/x.zip", "file:///tmp/x.zip",
                "http://127.0.0.1:8000/x.zip"):
        check(not ok(bad, False), f"refused without allow_local: {bad}")
    check(ok("file:///tmp/x.zip", True) and ok("http://127.0.0.1:9/x.zip", True) and ok("http://localhost:9/x", True),
          "file:// and localhost with allow_local")
    check(not ok("http://192.0.2.1/x.zip", True), "another http host stays refused with allow_local")
    check(ok("https://api.github.com/repos/o/r", False, "api") and not ok("https://api.github.com/users/o", False, "api")
          and not ok("https://github.com/o/r/releases/download/v1/x", False, "api"), "the release list: api.github.com/repos only")
    check(kit.served_ok("https://objects.githubusercontent.com/x", False)
          and kit.served_ok("https://release-assets.githubusercontent.com/x", False)
          and not kit.served_ok("https://evil.example/x", False)
          and not kit.served_ok("http://objects.githubusercontent.com/x", False), "redirect targets: GitHub's hosts, https")


def test_status_install(tmp):
    root = bare_root(tmp, "inst")
    z = os.path.join(tmp, "full.zip")
    sha = make_zip(z, FILES)
    check(kit.status(root, "")["state"] == "none", "status: none without a kit hash (developer build)")
    check(kit.status(root, sha)["state"] == "missing", "status: missing")
    before = snapshot(root)
    # refusals first: nothing may change
    cases = []
    zz = os.path.join(tmp, "bad1.zip")
    make_zip(zz, FILES)
    cases.append(("a kit.json of another hash", zz, "0" * 64))
    t = dict(FILES)
    m = manifest(FILES)
    t["port/rc/CMakeLists.txt"] = b"project(xx)\n"        # same size, other bytes
    zz = os.path.join(tmp, "bad2.zip")
    cases.append(("a tampered file", zz, make_zip(zz, t, m)))
    t = dict(FILES)
    del t["cmake/llvm-mingw.cmake"]
    zz = os.path.join(tmp, "bad3.zip")
    cases.append(("a missing file", zz, make_zip(zz, t, m)))
    t = dict(FILES)
    t["tools/release/firstrun.py"] = b"pass\n# longer\n"
    zz = os.path.join(tmp, "bad4.zip")
    cases.append(("a file of another size", zz, make_zip(zz, t, m)))
    for what, extra in (("a launcher file (python/) in the list", {"python/python.exe": b"MZ evil"}),
                        ("a launcher exe in the list", {"Launcher.exe": b"MZ evil"})):
        t = dict(FILES)
        t.update(extra)
        zz = os.path.join(tmp, f"bad-{len(cases)}.zip")
        cases.append((what, zz, make_zip(zz, t)))
    mm = manifest(FILES)
    mm["files"].append({"path": "../escape.txt", "sha256": "0" * 64, "size": 0})
    zz = os.path.join(tmp, "bad-path.zip")
    cases.append(("an unsafe path in the list", zz, make_zip(zz, FILES, mm)))
    zz = os.path.join(tmp, "bad-top.zip")
    with zipfile.ZipFile(zz, "w") as zf:
        zf.writestr("a/kit.json", kit.manifest_bytes(manifest(FILES)))
        zf.writestr("b/x", b"x")
    cases.append(("a zip without one top folder", zz, sha))
    for what, path, expect in cases:
        try:
            kit.install(path, root, expect, io.StringIO())
            refused = False
        except kit.Refused:
            refused = True
        check(refused and snapshot(root) == before and not os.path.exists(os.path.join(root, kit.STAGING)),
              f"install refused, nothing changed: {what}")
    n = kit.install(z, root, sha, io.StringIO())
    check(n == len(FILES) and kit.status(root, sha)["state"] == "ready" and not kit.verify(root, sha),
          "install: every kit file, status ready, verify clean")
    check(open(os.path.join(root, "Launcher.exe"), "rb").read() == b"MZ the running launcher"
          and open(os.path.join(root, "python", "python.exe"), "rb").read() == b"MZ bundled python",
          "install leaves the launcher's own files alone (the zip's copies are not taken)")
    check(kit.status(root, "f" * 64)["state"] == "other", "status: other (a kit of another release)")
    os.remove(os.path.join(root, "ps3recomp", "CMakeLists.txt"))
    check(kit.status(root, sha)["state"] == "damaged" and kit.verify(root, sha), "status: damaged; verify names it")
    # a newer kit drops a file: the reinstall removes it
    t = dict(FILES)
    del t["licenses/ninja/COPYING.txt"]
    t["toolchain/ninja/new.txt"] = b"new\n"
    z2 = os.path.join(tmp, "full2.zip")
    sha2 = make_zip(z2, t)
    kit.install(z2, root, sha2, io.StringIO())
    check(not os.path.exists(os.path.join(root, "licenses", "ninja", "COPYING.txt"))
          and os.path.isfile(os.path.join(root, "toolchain", "ninja", "new.txt"))
          and kit.status(root, sha2)["state"] == "ready", "a reinstall removes the files the new kit dropped")
    return z, sha


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def serve(folder):
    h = lambda *a, **k: Quiet(*a, directory=folder, **k)  # noqa: E731
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), h)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def test_fetch(tmp, z, sha):
    web = os.path.join(tmp, "web")
    os.makedirs(web)
    shutil.copy(z, os.path.join(web, "Top-1.2.3-build.zip"))
    srv = serve(web)
    try:
        url = f"http://127.0.0.1:{srv.server_address[1]}/Top-1.2.3-build.zip"
        root = bare_root(tmp, "f1")
        out = io.StringIO()
        rc = kit.fetch(root, url, sha, out, allow_local=True)
        text = out.getvalue()
        check(rc == 0 and kit.status(root, sha)["state"] == "ready", "fetch over HTTP installs the kit", text)
        check("PROGRESS step=1/2 name=download" in text and "name=verify" in text and "RESULT ok=1" in text,
              "fetch prints PROGRESS and RESULT lines", text)
        check(not os.path.exists(os.path.join(root, kit.DOWNLOADS)), "the download is removed after the install")
        root = bare_root(tmp, "f2")
        before = snapshot(root)
        out = io.StringIO()
        rc = kit.fetch(root, url, sha, out, allow_local=False)
        check(rc == 1 and "REASON" in out.getvalue() and snapshot(root) == before, "a local URL is refused without allow_local",
              out.getvalue())
        out = io.StringIO()
        rc = kit.fetch(root, url, "0" * 64, out, allow_local=True)
        check(rc == 1 and "not the build kit this launcher was made for" in out.getvalue() and snapshot(root) == before
              and not os.path.exists(os.path.join(root, kit.DOWNLOADS)), "a download of another kit is refused and deleted",
              out.getvalue())
        stop = os.path.join(tmp, "stop")
        write(stop, b"")
        out = io.StringIO()
        rc = kit.fetch(root, url, sha, out, stop_file=stop, allow_local=True)
        check(rc == 130 and "step=cancelled" in out.getvalue() and snapshot(root) == before
              and not os.path.exists(stop), "the stop file cancels: exit 130, nothing installed, stop file removed",
              out.getvalue())
        out = io.StringIO()
        old = kit.MAX_KIT_ZIP
        kit.MAX_KIT_ZIP = 100
        try:
            rc = kit.fetch(root, url, sha, out, allow_local=True)
        finally:
            kit.MAX_KIT_ZIP = old
        check(rc == 1 and "larger" in out.getvalue() and snapshot(root) == before, "a download over the cap is refused",
              out.getvalue())
        out = io.StringIO()
        rc = kit.main(["fetch", "--root", root, "--expect", sha, "--url", "file://" + urllib.request.pathname2url(z),
                       "--allow-local"], out=out)
        check(rc == 0 and kit.status(root, sha)["state"] == "ready", "kit.py fetch (CLI) over file://", out.getvalue())
        out = io.StringIO()
        rc = kit.main(["status", "--root", root, "--expect", sha], out=out)
        st = json.loads(out.getvalue().split("STATUS ", 1)[1])
        check(rc == 0 and st["state"] == "ready" and st["release"] == "1.2.3", "kit.py status (CLI) prints STATUS json", st)
    finally:
        srv.shutdown()


def pe_with_debuglink(name, crc, long_names=True):
    """A synthetic PE image with one .gnu_debuglink section (long name through the string table, or cut at 8)."""
    link = name.encode() + b"\0"
    link += b"\0" * ((4 - len(link) % 4) % 4) + struct.pack("<I", crc)
    b = bytearray(0x400)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 60, 64)
    b[64:68] = b"PE\0\0"
    strtab = 0x300 if long_names else 0
    struct.pack_into("<HHIIIHH", b, 68, 0x8664, 1, 0, strtab, 0, 0, 0x22)
    sec = 64 + 24
    b[sec:sec + 8] = b"/4\0\0\0\0\0\0" if long_names else b".gnu_deb"
    struct.pack_into("<IIII", b, sec + 8, len(link), 0x1000, len(link), 0x200)
    b[0x200:0x200 + len(link)] = link
    if long_names:
        s = b".gnu_debuglink\0"
        struct.pack_into("<I", b, strtab, 4 + len(s))
        b[strtab + 4:strtab + 4 + len(s)] = s
    return bytes(b)


def test_symbols(tmp):
    data = b"\x7fdebug information of the launcher" * 50
    crc = zlib.crc32(data) & 0xFFFFFFFF
    for long_names in (True, False):
        exe = os.path.join(tmp, f"sym{int(long_names)}", "Launcher.exe")
        write(exe, pe_with_debuglink("Launcher.exe.debug", crc, long_names))
        check(kit.debuglink(exe) == ("Launcher.exe.debug", crc),
              f"debuglink read ({'string-table name' if long_names else 'name cut at 8'})", kit.debuglink(exe))
    z = os.path.join(tmp, "symbols.zip")
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr("Top-1.2.3-launcher-symbols/Launcher.exe.debug", data)
    url = "file://" + urllib.request.pathname2url(z)
    out = io.StringIO()
    rc = kit.symbols(exe, url, out, allow_local=True)
    dbg = os.path.join(os.path.dirname(exe), "Launcher.exe.debug")
    check(rc == 0 and open(dbg, "rb").read() == data, "symbols: the debug file lands beside the exe", out.getvalue())
    os.remove(dbg)
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr("Top/Launcher.exe.debug", data + b"x")
    out = io.StringIO()
    rc = kit.symbols(exe, url, out, allow_local=True)
    check(rc == 1 and "another build" in out.getvalue() and not os.path.exists(dbg),
          "symbols: a debug file with another CRC-32 is refused", out.getvalue())
    plain = os.path.join(tmp, "plain.exe")
    write(plain, pe_with_debuglink("x", 0)[:0x200])
    out = io.StringIO()
    rc = kit.symbols(plain, url, out, allow_local=True)
    check(rc == 1 and "without a separate debug file" in out.getvalue(), "symbols: an exe without a debuglink",
          out.getvalue())


def test_release_check(tmp):
    rel = [{"tag_name": "v0.4.0", "draft": True, "prerelease": False, "html_url": "d"},
           {"tag_name": "v0.3.1", "draft": False, "prerelease": True, "html_url": "p"},
           {"tag_name": "v0.3.0", "draft": False, "prerelease": False, "html_url": "s"},
           {"tag_name": "nightly", "draft": False, "prerelease": False}]
    check(kit.newest(rel, False) == ("0.3.0", "s") and kit.newest(rel, True) == ("0.3.1", "p")
          and kit.newest([], True) == (None, None), "newest: drafts never, pre-releases only when asked")
    d40 = [{"tag_name": "tods-v0.9.0", "draft": False, "prerelease": True, "html_url": "g"},
           {"tag_name": "launcher-v0.4.0", "draft": False, "prerelease": True, "html_url": "l"},
           {"tag_name": "v0.3.0", "draft": False, "prerelease": True, "html_url": "o"}]
    check(kit.newest(d40, True) == ("0.4.0", "l") and kit.newest(d40[:1], True) == (None, None)
          and kit.vkey("launcher-v1.2.3") == (1, 2, 3) and kit.vkey("rc1-v1.2.3") is None,
          "newest: launcher-vX.Y.Z and the old vX.Y.Z are launcher releases; a game tag never is")
    web = os.path.join(tmp, "api")
    write(os.path.join(web, "releases.json"), json.dumps(rel))
    srv = serve(web)
    try:
        api = f"http://127.0.0.1:{srv.server_address[1]}/releases.json"
        out = io.StringIO()
        rc = kit.main(["check-release", "--api", api, "--current", "0.2.1", "--prerelease", "--allow-local"], out=out)
        st = json.loads(out.getvalue().split("STATUS ", 1)[1]) if "STATUS " in out.getvalue() else {}
        check(rc == 0 and st.get("latest") == "0.3.1" and st.get("newer") is True, "check-release: newer pre-release",
              out.getvalue())
        out = io.StringIO()
        rc = kit.main(["check-release", "--api", api, "--current", "0.3.0", "--allow-local"], out=out)
        st = json.loads(out.getvalue().split("STATUS ", 1)[1]) if "STATUS " in out.getvalue() else {}
        check(rc == 0 and st.get("latest") == "0.3.0" and st.get("newer") is False, "check-release: up to date",
              out.getvalue())
        out = io.StringIO()
        rc = kit.main(["check-release", "--api", api, "--current", "0.3.0"], out=out)
        check(rc == 1 and "REASON" in out.getvalue(), "check-release: a local list is refused without allow_local")
    finally:
        srv.shutdown()


def main():
    tmp = tempfile.mkdtemp()
    try:
        test_imports()
        test_urls()
        z, sha = test_status_install(tmp)
        test_fetch(tmp, z, sha)
        test_symbols(tmp)
        test_release_check(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_kit:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

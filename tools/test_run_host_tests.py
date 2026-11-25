#!/usr/bin/env python3
"""Host test of tools/run-host-tests.sh's verdict rules on fixture tests (audit 2026-11-25 M8, M9, L50), plus the
SKIP lines of the display and settings tests' Windows-compile halves (M9).
  - a test that prints only SKIP and exits 0 is SKIP, and FAIL under RC_HOST_TESTS_STRICT=1 even when allow-listed;
  - a partial SKIP (SKIP line next to a PASS line) is shown on the ok line; strict mode fails it unless the list has a
    `#:allow-skip PATH reason` line for the test, and then the ok line names the allowed SKIP;
  - strict mode fails a test that exits 0 without a pass line (L50); a non-zero exit is a FAIL in every mode;
  - every port/rc/tests/run_*.sh is listed or named in the list's ledger (M7);
  - port/rc/tests/run_rc_display_test.sh and run_rc_settings_test.sh print `SKIP Windows compile` when MinGW is not on
    PATH (they printed nothing and still PASSed), and tools/host-tests.txt allow-lists exactly those partial skips.
  python3 -I tools/test_run_host_tests.py [OTHER/run-host-tests.sh]   (the argument tests another copy of the runner)
Needs bash and gcc (the real display/settings tests compile against work/ps3recomp-patched). Exit 0 = PASS."""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
RUNNER = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE / "run-host-tests.sh"
N, FAILS = 0, []


def check(name, cond, detail=""):
    global N
    N += 1
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)
        print("     " + str(detail).strip().replace("\n", "\n     ")[:900])


FIXTURES = {
    "t_pass.sh": 'echo "[t_pass] PASS"',
    "t_skiponly.sh": 'echo "SKIP: nothing to test here"',
    "t_partial.sh": 'echo "[PASS] host half"\necho "SKIP Windows compile: no mingw"\necho "[t_partial] PASS"',
    "t_partial_allowed.sh": 'echo "[PASS] host half"\necho "SKIP Windows compile: no mingw"\necho "[t_partial_allowed] PASS"',
    "t_skiponly_allowed.sh": 'echo "SKIP: everything"',
    "t_silent.sh": 'true',
    "t_fail.sh": 'echo "[t_fail] PASS"; exit 1',
}


def run_list(d, strict):
    lst = d / "list.txt"
    lines = [f"#:allow-skip {d / 't_partial_allowed.sh'} the Windows half needs MinGW (test fixture)",
             f"#:allow-skip {d / 't_skiponly_allowed.sh'} allow-listed but skips everything (test fixture)"]
    lines += [str(d / n) for n in FIXTURES]
    lst.write_text("\n".join(lines) + "\n")
    env = dict(os.environ, RC_HOST_TESTS_STRICT="1" if strict else "0", RC_HOST_TESTS_TIMEOUT="60")
    r = subprocess.run(["bash", str(RUNNER), str(lst)], capture_output=True, text=True, env=env)
    by = {}
    for line in r.stdout.splitlines():
        m = re.match(r"^(ok|FAIL|SKIP)\s+(\S+)(.*)$", line)
        if m:
            by[pathlib.Path(m.group(2)).name] = (m.group(1), m.group(3))
    return r.returncode, by, r.stdout + r.stderr


base = pathlib.Path(tempfile.gettempdir())
if re.search(r"\s", str(base)):
    base = pathlib.Path("/tmp")   # list paths are whitespace-separated words
d = pathlib.Path(tempfile.mkdtemp(prefix="rc-rht-test-", dir=base))
try:
    for n, body in FIXTURES.items():
        (d / n).write_text("#!/usr/bin/env bash\n" + body + "\n")
    rc, by, out = run_list(d, strict=False)
    st = {k: v[0] for k, v in by.items()}
    check("default: pass test ok", st.get("t_pass.sh") == "ok", out)
    check("default: SKIP-only test is SKIP", st.get("t_skiponly.sh") == "SKIP", out)
    check("default: partial SKIP is ok and the ok line names the SKIP",
          st.get("t_partial.sh") == "ok" and "partial SKIP: SKIP Windows compile" in by.get("t_partial.sh", ("", ""))[1], out)
    check("default: silent exit 0 is ok", st.get("t_silent.sh") == "ok", out)
    check("default: non-zero exit is FAIL, run exits 1", st.get("t_fail.sh") == "FAIL" and rc == 1, out)
    rc, by, out = run_list(d, strict=True)
    st = {k: v[0] for k, v in by.items()}
    check("strict: pass test ok", st.get("t_pass.sh") == "ok", out)
    check("strict: SKIP-only test FAILs", st.get("t_skiponly.sh") == "FAIL", out)
    check("strict: SKIP-only test FAILs even when allow-listed", st.get("t_skiponly_allowed.sh") == "FAIL", out)
    check("strict: partial SKIP not allow-listed FAILs", st.get("t_partial.sh") == "FAIL", out)
    check("strict: allow-listed partial SKIP is ok and named on the ok line",
          st.get("t_partial_allowed.sh") == "ok"
          and "allowed partial SKIP: SKIP Windows compile" in by.get("t_partial_allowed.sh", ("", ""))[1], out)
    check("strict: exit 0 with no pass line FAILs (L50)", st.get("t_silent.sh") == "FAIL", out)
finally:
    shutil.rmtree(d, ignore_errors=True)

# M9: the real tests' Windows halves say SKIP when MinGW is missing (PATH without any x86_64-w64-mingw32-* tool)
path = os.pathsep.join(p for p in os.environ.get("PATH", "").split(os.pathsep)
                       if not any(pathlib.Path(p, f"x86_64-w64-mingw32-{c}").exists() for c in ("gcc", "g++")))
for t in ("run_rc_display_test.sh", "run_rc_settings_test.sh"):
    r = subprocess.run(["bash", str(ROOT / "port" / "rc" / "tests" / t)], capture_output=True, text=True,
                       env=dict(os.environ, PATH=path))
    o = r.stdout + r.stderr
    check(f"{t}: exit 0 with a SKIP Windows compile line when MinGW is missing",
          r.returncode == 0 and re.search(r"^SKIP Windows compile", o, re.M) is not None, o[-900:])
allow = re.findall(r"^#:allow-skip\s+(\S+)\s+(.+)$", (ROOT / "tools" / "host-tests.txt").read_text(), re.M)
names = {p for p, _ in allow}
for t in ("port/rc/tests/run_rc_display_test.sh", "port/rc/tests/run_rc_settings_test.sh"):
    check(f"host-tests.txt allow-lists {t} with a reason", t in names and all(len(r) > 20 for p, r in allow if p == t),
          allow)
# M7: every port/rc/tests/run_*.sh is listed or named in the list's "left out" ledger (comment lines), with its reason
# there (three synthetic tests were in neither, so nothing ran them and nothing said why)
text = (ROOT / "tools" / "host-tests.txt").read_text()
listed = {l.strip() for l in text.splitlines() if l.strip() and not l.lstrip().startswith("#")}
ledger = " ".join(l for l in text.splitlines() if l.lstrip().startswith("#") and not l.startswith("#:allow-skip"))
loose = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "port" / "rc" / "tests").glob("run_*.sh")
               if str(p.relative_to(ROOT)) not in listed and not re.search(r"\b" + re.escape(p.name) + r"\b", ledger))
check("every port/rc/tests/run_*.sh is listed or in the ledger", not loose, loose)
print(f"test_run_host_tests: {N - len(FAILS)}/{N} checks", "FAIL" if FAILS else "PASS")
sys.exit(1 if FAILS else 0)

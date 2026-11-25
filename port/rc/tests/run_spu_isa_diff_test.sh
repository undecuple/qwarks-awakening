#!/usr/bin/env bash
# Do the SPU helpers compute the same bits with and without the SPU ISA flags of port/rc/CMakeLists.txt?
# Builds test_spu_isa_diff.c (call table from gen_spu_isa_diff.py, every public helper of runtime/spu/spu_helpers.h)
# three ways and compares the raw results lane by lane:
#   base  = -O2                               (the lifted SPU sources before RC_SPU_SSE41, plain x86-64)
#   sse41 = -O2 -msse4.1 -ffp-contract=off    (RC_SPU_SSE41=ON, the default)
#   fma   = sse41 + -mfma                     (RC_SPU_FMA=ON, off by default)
# under each runtime fma mode g_kz3_spu_slowcg (0 = default with the patch-1040 flush to zero, 1 = old per-lane path,
# 2 = fast path without the flush). Checks (any failure = FAIL):
#   1. sse41 == base, every helper except spu_fma/fms/fnms, every lane, in all three modes (R&C 2026-11-23: the fma
#      family differs in every mode on today's helpers, beyond patch 1040's flush in mode 0; its lane counts are
#      printed and port/rc/CMakeLists.txt RC_SPU_SSE41_IMAGES lists only images that do not use it);
#   2. fma == sse41, every helper, every lane, in all three modes.
# Double-precision helpers (spu_df*): a NaN result counts as one canonical NaN (its payload follows the host operand
# order; ASSUMPTION, as the KZ2 port's test: the SPU code cannot observe it beyond "is NaN").
# Same idea as the Killzone 2 port's tools/runtime/test_spu_isa_diff.sh, written here for R&C.
# RC_P3R_TREE = the patched tree (default work/ps3recomp-patched of this checkout, else of the main checkout); CC (gcc).
#   nix develop .#ci --command port/rc/tests/run_spu_isa_diff_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-}"
if [[ -z "$tree" ]]; then
  for t in "$repo/work/ps3recomp-patched" "$main/work/ps3recomp-patched"; do
    [[ -f "$t/runtime/spu/spu_helpers.h" ]] && { tree="$t"; break; }
  done
fi
[[ -n "$tree" && -f "$tree/runtime/spu/spu_helpers.h" ]] || { echo "SKIP run_spu_isa_diff_test: no patched tree"; exit 0; }
cc="${CC:-gcc}"
tmp="${TMPDIR:-/tmp}/spuisa_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
python3 -I "$here/gen_spu_isa_diff.py" "$tree/runtime/spu/spu_helpers.h" > "$tmp/spu_isa_diff.inc"
CF=(-std=gnu17 -O2 -w -fno-strict-aliasing -I"$tmp" -I"$tree/runtime/spu" -I"$tree/include" -I"$tree/runtime")
declare -A FL=([base]="" [sse41]="-msse4.1 -ffp-contract=off" [fma]="-msse4.1 -ffp-contract=off -mfma")
# A helper defined under one ISA only (an #if __SSE4_1__ branch without a twin) cannot be compared: drop it, say so.
for v in base fma; do
  # shellcheck disable=SC2086
  miss=$("$cc" "${CF[@]}" ${FL[$v]} -fsyntax-only -Werror=implicit-function-declaration "$here/test_spu_isa_diff.c" 2>&1 \
         | grep -o "implicit declaration of function .spu_[a-z0-9_]*" | grep -o "spu_[a-z0-9_]*" | sort -u || true)
  for m in $miss; do
    grep -v -E "(call_$m\(|\"$m\")" "$tmp/spu_isa_diff.inc" > "$tmp/x.inc"; mv "$tmp/x.inc" "$tmp/spu_isa_diff.inc"
    echo "(only under one ISA, not compared: $m)"
  done
done
for v in base sse41 fma; do
  # shellcheck disable=SC2086
  "$cc" "${CF[@]}" ${FL[$v]} -o "$tmp/t_$v" "$here/test_spu_isa_diff.c" -lm
  for cgm in 0 1 2; do "$tmp/t_$v" "$cgm" "$tmp/$v.$cgm.names" "$tmp/$v.$cgm.bin"; done
done
python3 -I - "$tmp" <<'EOF'
import struct, sys
d = sys.argv[1]
def load(v, cgm):
    names = open(f"{d}/{v}.{cgm}.names").read().split('\n')
    n = int(names[0]); rows = [l.split() for l in names[1:] if l]
    data = open(f"{d}/{v}.{cgm}.bin", 'rb').read()
    out = {}
    for i, (name, dbl) in enumerate(rows):
        w = struct.unpack_from('<%dI' % (n * 4), data, i * n * 16)
        if dbl == '1':   # dword k = words 2k (high), 2k+1 (low): canonical NaN
            w = list(w)
            for j in range(0, len(w), 2):
                if (w[j] >> 20) & 0x7FF == 0x7FF and ((w[j] & 0xFFFFF) or w[j + 1]):
                    w[j], w[j + 1] = 0x7FF80000, 0
        out[name] = w
    return out
def diff(a, b):   # {name: (ftz lanes, other lanes)}; ftz = a subnormal, b its signed zero
    res = {}
    for name in a:
        f = o = 0
        for x, y in zip(a[name], b[name]):
            if x == y: continue
            if (x & 0x7F800000) == 0 and (x & 0x7FFFFF) and y == (x & 0x80000000): f += 1
            else: o += 1
        if f or o: res[name] = (f, o)
    return res
R = {(v, c): load(v, c) for v in ('base', 'sse41', 'fma') for c in (0, 1, 2)}
nh = len(R[('base', 0)]); nl = len(next(iter(R[('base', 0)].values())))
fail = []
# R&C 2026-11-23: spu_fma/fms/fnms give other bits with -msse4.1 in every mode on today's helpers (not only patch 1040's
# flush), so RC_SPU_SSE41_IMAGES lists only images without them; those three are reported, every other helper must match.
FMA = ('spu_fma', 'spu_fms', 'spu_fnms')
known = {}
for c in (1, 2):
    dd = diff(R[('base', c)], R[('sse41', c)])
    known.update({f"{k} mode {c}": v[1] for k, v in dd.items() if k in FMA})
    bad = {k: v for k, v in dd.items() if k not in FMA}
    if bad: fail.append(f"mode {c} sse41 != base: {bad}")
dd = diff(R[('base', 0)], R[('sse41', 0)])
bad = {k: v for k, v in dd.items() if k not in FMA}
if bad: fail.append(f"mode 0 sse41 != base: {bad}")
known.update({f"{k} mode 0 (beyond the flush)": v[1] for k, v in dd.items() if k in FMA and v[1]})
ftz = {k: v[0] for k, v in dd.items()}
for c in (0, 1, 2):
    dd = diff(R[('sse41', c)], R[('fma', c)])
    if dd: fail.append(f"mode {c} fma != sse41: {dd}")
print(f"{nh} helpers x {nl} lanes x 3 modes x 3 flag sets")
print(f"INFO mode 0 (default): base -> sse41 flushes subnormal results to signed zero in {ftz or 'no lanes'} (patch 1040)")
if fail:
    for f in fail: print("FAIL", f)
    sys.exit(1)
print(f"INFO sse41 != base in the fma family (lanes; images using these stay off RC_SPU_SSE41_IMAGES): {known or 'none'}")
print("PASS run_spu_isa_diff_test: every helper but spu_fma/fms/fnms: sse41 == base bit for bit in all modes; fma == sse41 in all modes")
EOF

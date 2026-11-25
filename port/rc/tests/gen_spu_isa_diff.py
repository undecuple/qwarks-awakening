#!/usr/bin/env python3
"""Generate the call table of test_spu_isa_diff.c (run_spu_isa_diff_test.sh): one entry per public SPU helper of
runtime/spu/spu_helpers.h (u128 spu_NAME(...) whose parameters are only u128 and integers). The test runs every entry
on the same pseudo-random inputs and writes the raw results; the script builds it with and without the SPU ISA flags
of port/rc/CMakeLists.txt (RC_SPU_SSE41 / RC_SPU_FMA) and compares the results lane by lane.
Same idea as the Killzone 2 port's tools/runtime/gen_spu_isa_diff.py (runtime pass 25), written here for R&C.
usage: gen_spu_isa_diff.py spu_helpers.h > spu_isa_diff.inc"""
import re
import sys

INT_TYPES = {'int', 'int32_t', 'uint32_t', 'unsigned', 'uint8_t', 'int8_t', 'uint16_t', 'int16_t'}
src = open(sys.argv[1]).read()
seen = {}
for m in re.finditer(r'^(?:SPU_HOT |static inline |SPU_XR_COLD |static )*u128 (spu_[a-z0-9_]+)\(([^)]*)\)\s*\{',
                     src, re.M):
    name, args = m.group(1), m.group(2).strip()
    if name.startswith('spu__') or name in seen:
        continue
    kinds = []
    for p in (a.strip() for a in args.split(',')) if args else []:
        t = p.rsplit(' ', 1)[0].strip()
        if t == 'u128':
            kinds.append('v')
        elif t in INT_TYPES:
            kinds.append('i')
        else:
            kinds = None
            break
    if kinds:
        seen[name] = kinds
for name, kinds in seen.items():
    call, vi, ii = [], 0, 0
    for k in kinds:
        if k == 'v':
            call.append('V[(n + %d) %% NV]' % (vi * 7)); vi += 1
        else:
            call.append('IM[(n + %d) %% NI]' % (ii * 3)); ii += 1
    # spu_df*: double-precision results (a NaN's payload follows the host operand order; see the .c file)
    print('static u128 call_%s(int n) { return %s(%s); }' % (name, name, ', '.join(call)))
print('static const struct { const char* name; u128 (*fn)(int); int dbl; } CALLS[] = {')
for name in seen:
    print('  { "%s", call_%s, %d },' % (name, name, 1 if name.startswith('spu_df') else 0))
print('};')

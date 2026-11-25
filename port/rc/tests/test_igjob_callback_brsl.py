#!/usr/bin/env python3
"""Host test of R&C patch 7106 (spu_lifter.py --callback-brsl, passed by tools/spu/lift_igjobs.py). No game data.

The ig jobs' packer idiom (Nexus 1.01 job 0x00C21400 0x68A4 `brsl $r77, 0x5E40`; A4O/FFA/ACiT 1.20 the same job): a
function passes its own continuation in r77 and branches to a packer; the packer calls that continuation back per batch
(`bisl $r78, $r77`, the producer comes back with `bi $r78`) and leaves with `bi $r0`, to the function's CALLER. Lifted
as a host call bracket, the brsl waits for brsl+4, which nothing returns to: two host frames per call stay open and the
rest of the job runs inside them (Nexus prison_transport: host_depth 2000 at pc 0x4D10, the SPU halts).

A synthetic ELF holds a job kernel (LS 0x3F580, entry 0x3F5A0: `bisl $r0,$r15` to the job, then `stop 0x1234`) and a
format-A job blob (LS 0x100, entry 0x130) whose loop calls F N times; F = `brsl $r77, P`, producer G (r3 += r5 items,
`bi $r78`), packer P (two batches, 2 + 3 items, `bi $r0`). Expected end state: stop 0x1234, r3 = 5 * N. lift_igjobs.py
lifts both; the test links them with the patched SPU runtime and runs an image -1 policy context (as test_igjob_rejoin).
Negative control: the same job with "callback_brsl": false in the table (the lift before 7106). It must FAIL: with N
above the depth limit it halts ([spu-depth]); with a small N the job only finishes because 7099 unwinds the dead frames
at the job's exit ([spu-jobframe] unwind, the A4O luminopolis shape). The runner exits 0 only when the control fails.
    python3 -I port/rc/tests/test_igjob_callback_brsl.py PS3RECOMP_TREE      (run_igjob_callback_brsl_test.sh)"""
import json, os, re, struct, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TREE = Path(sys.argv[1]).resolve()
TOOL = REPO / "tools/spu/lift_igjobs.py"


def ri16(op9, rt, imm): return (op9 << 23) | ((imm & 0xFFFF) << 7) | rt
def ila(rt, imm): return (0x21 << 25) | ((imm & 0x3FFFF) << 7) | rt
def il(rt, imm): return ri16(0x081, rt, imm)
def ai(rt, ra, imm): return (0x1C << 24) | ((imm & 0x3FF) << 14) | (ra << 7) | rt
def ori(rt, ra, imm): return (0x04 << 24) | ((imm & 0x3FF) << 14) | (ra << 7) | rt
def brsl(rt, pc, tgt): return ri16(0x066, rt, (tgt - pc) >> 2)
def brnz(rt, pc, tgt): return ri16(0x042, rt, (tgt - pc) >> 2)
def brasl(rt, tgt): return ri16(0x062, rt, tgt >> 2)
def bi(ra): return (0x1A8 << 21) | (ra << 7)
def bisl(rt, ra): return (0x1A9 << 21) | (ra << 7) | rt
def stop(code): return code & 0x3FFF
LNOP = 0x00200000


def words(ws): return b"".join(struct.pack(">I", w) for w in ws)


KLSA, KENTRY = 0x3F580, 0x3F5A0
KERNEL = words([0x12345678, 0x9ABCDEF0, 0xCAFEF00D, 0, 0xFFFFFFFF, 1, 2, 3,   # 0x3F580 data
                ila(15, 0x130), bisl(0, 15), stop(0x1234), LNOP])               # 0x3F5A0 call the job, stop
KEND = KLSA + len(KERNEL)
JOB_CODE = [  # (LS address, word, disassembly the test expects)
    (0x130, ori(91, 80, 0), "ori"),            # r80 = N, set by the driver (the code bytes stay verifiable)
    (0x134, ori(90, 0, 0), "ori"),             # keep the kernel's link
    (0x138, brsl(0, 0x138, 0x150), "brsl"),    # loop: call F
    (0x13C, ai(80, 80, -1), "ai"),
    (0x140, brnz(80, 0x140, 0x138), "brnz"),
    (0x144, ori(0, 90, 0), "ori"),
    (0x148, bi(0), "bi"),                      # back to the kernel
    (0x14C, LNOP, "lnop"),
    (0x150, il(79, 0), "il"),                  # F
    (0x154, brsl(77, 0x154, 0x168), "brsl"),   # r77 = G, branch to the packer (the idiom)
    (0x158, ai(3, 3, 1), "ai"),                # G: the producer, r5 items
    (0x15C, ai(5, 5, -1), "ai"),
    (0x160, brnz(5, 0x160, 0x158), "brnz"),
    (0x164, bi(78), "bi"),                     # back to the packer
    (0x168, il(5, 2), "il"),                   # P: the packer
    (0x16C, bisl(78, 77), "bisl"),             # batch 1: call the continuation
    (0x170, il(5, 3), "il"),
    (0x174, bisl(78, 77), "bisl"),             # batch 2
    (0x178, bi(0), "bi"),                      # leave to F's caller
    (0x17C, LNOP, "lnop"),
]
JOB = words([ila(2, 0x11), ila(2, 0x22), ila(2, 0x33), ila(2, 0x44),
             (0x100 >> 2) << 16, ((0x80 >> 2) << 16) | (0x180 >> 2), ((0x10 >> 2) << 16) | (0x130 >> 2), 0,
             LNOP, LNOP, LNOP, LNOP] + [w for _, w, _ in JOB_CODE])
assert len(JOB) == 0x80
SEG_VA = 0x00800000
K_EA, J_EA = SEG_VA + 0x100, SEG_VA + 0x400


def elf():
    seg = bytearray(0x800)
    seg[K_EA - SEG_VA:K_EA - SEG_VA + len(KERNEL)] = KERNEL
    seg[J_EA - SEG_VA:J_EA - SEG_VA + len(JOB)] = JOB
    phoff, data_off = 0x40, 0x100
    eh = b"\x7fELF" + bytes([2, 2, 1]) + bytes(9)
    eh += struct.pack(">HHIQQQIHHHHHH", 2, 21, 1, 0, phoff, 0, 0, 0x40, 0x38, 1, 0x40, 0, 0)
    ph = struct.pack(">IIQQQQQQ", 1, 5, data_off, SEG_VA, SEG_VA, len(seg), len(seg), 0x10)
    out = bytearray(eh + ph)
    out += bytes(data_off - len(out)) + seg
    return bytes(out)


failures = []
def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


DRIVER = r'''
#include "spu_interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
extern void spu_overlay_note_get(spu_context*, uint32_t, const uint8_t*, uint32_t);
extern int spu_run_with_halt(void (SPU_ABI *)(spu_context*), spu_context*);
extern void SPU_ABI spurs_pm_interp_entry(spu_context*);
extern const unsigned char kernel[], job[];
extern const unsigned kernel_len, job_len;
uint8_t* vm_base;
uint32_t ppu_vm_size;
void spu_thread_publish_ctx(uint32_t tid, void* c) { (void)tid; (void)c; abort(); }
void ppu_resv_break_store(uint64_t ea) { (void)ea; abort(); }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; abort(); }
unsigned long long ps3_ms_now(void) { return 0; }
uint32_t g_ww_lo = 0xFFFFFFFFu, g_ww_hi;
int g_resv_store_active;
uint32_t g_barrier_sync_watch;
uint32_t g_ydkj_real_spurs_ea, g_ydkj_real_taskset_ea, g_ydkj_real_taskid;
void spurs_ef_set_from_spu(uint32_t ea, uint16_t bits) { (void)ea; (void)bits; abort(); }
void ydkj_wake_all_event_flags(void) { abort(); }
void spurs_pm_build_context(spu_context* c, uint32_t a, uint32_t b, uint32_t d) { (void)c; (void)a; (void)b; (void)d; abort(); }
uint32_t g_spu_image_src_ea, g_spu_image_ls_start, g_spu_image_span;
static void word(spu_context* c, uint32_t pc, uint32_t w) {
    c->ls[pc] = w >> 24; c->ls[pc + 1] = w >> 16; c->ls[pc + 2] = w >> 8; c->ls[pc + 3] = w;
}
static spu_context ctx;
static uint32_t hist[0x10000];
extern _Thread_local uint32_t* g_spu_interp_pc_hist;
extern _Thread_local uint32_t g_spu_interp_pc_hist_lo, g_spu_interp_pc_hist_n;
static void on_alarm(int sig) { (void)sig; fprintf(stderr, "HANG pc=0x%05X depth=%d\n", ctx.pc, ctx.host_depth); _exit(3); }
int main(int argc, char** argv)
{
    const unsigned n = argc > 1 ? (unsigned)strtoul(argv[1], 0, 0) : 1;
    signal(SIGALRM, on_alarm); alarm(10);
    spu_context_init(&ctx, 0);
    ctx.policy_mode = 1;
    ctx.image_id = -1;                                /* patch 7034: an unlifted policy module */
    word(&ctx, 0xA00, IL_R3_0);                       /* the loader (interpreted) */
    word(&ctx, 0xA04, BRASL_KERNEL);
    word(&ctx, 0xA08, STOP_DEAD);
    memcpy(ctx.ls + KLSA, kernel, kernel_len);
    spu_overlay_note_get(&ctx, K_EA, ctx.ls + KLSA, kernel_len);
    memcpy(ctx.ls + 0x100, job, job_len);
    spu_overlay_note_get(&ctx, J_EA, ctx.ls + 0x100, job_len);
    ctx.gpr[80]._u32[0] = n;                          /* the job's loop count (the kernel and loader leave r80) */
    g_spu_interp_pc_hist = hist; g_spu_interp_pc_hist_lo = 0; g_spu_interp_pc_hist_n = 0x10000;   /* KZ3 1070 */
    spu_run_with_halt(spurs_pm_interp_entry, &ctx);
    printf("n=%u status=0x%X stop=0x%X r3=0x%X depth=%d interp_job_entry=%u\n", n, ctx.status, ctx.stop_code,
           ctx.gpr[3]._u32[0], ctx.host_depth, hist[0x130 >> 2]);
    return 0;
}
'''


def lift_and_build(tmp, tag, callback):
    d = tmp / tag
    d.mkdir()
    (d / "game.elf").write_bytes(elf())
    job = {"ea": hex(J_EA), "image_id": 60}
    if not callback:
        job["callback_brsl"] = False
    table = {"title": "synthetic",
             "kernel": {"ea": hex(K_EA), "lsa": hex(KLSA), "get_size": hex(len(KERNEL)), "entry": hex(KENTRY),
                        "code_end": hex(KEND), "span": hex(len(KERNEL)), "image_id": 51,
                        "data_ranges": ["0x%x-0x%x" % (KLSA, KENTRY)]},
             "jobs": [job]}
    (d / "table.json").write_text(json.dumps(table))
    r = subprocess.run([sys.executable, "-I", str(TOOL), str(d / "game.elf"), str(TREE), str(d / "out"),
                        str(d / "table.json")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit("FAIL lift_igjobs.py (%s)" % tag)
    man = json.loads((d / "out/igjobs_manifest.json").read_text())
    spu = TREE / "runtime/spu"
    consts = {"IL_R3_0": il(3, 0), "BRASL_KERNEL": brasl(0, KENTRY), "STOP_DEAD": stop(0xDEA),
              "KLSA": KLSA, "K_EA": K_EA, "J_EA": J_EA}
    src = DRIVER
    for k, v in consts.items():
        src = re.sub(r"\b%s\b" % k, "0x%Xu" % v, src)
    (d / "driver.c").write_text(src)
    (d / "images.c").write_text("".join(
        f"const unsigned char {n}[] = {{{','.join(str(x) for x in raw)}}};\nconst unsigned {n}_len = {len(raw)};\n"
        for n, raw in (("kernel", KERNEL), ("job", JOB))))
    sources = ["spu_channels.c", "spu_drain.c", "spu_lockstep.c", "spu_coherency.c", "spu_workload.c", "spurs_policy.c",
               "spurs_job.c", "spu_tsp_weak.c", "spu_vm_pagemap.c", "spurs_policy_blob_weak.c", "spu_interp.c"]
    lifted = [str(d / "out" / m["name"] / "spu_recomp.c") for m in man["regions"]]
    binary = d / "callback"
    r = subprocess.run([os.environ.get("CC", "cc"), "-std=gnu17", "-O1", "-UNDEBUG", "-w", "-I" + str(TREE / "include"),
                        "-I" + str(spu), "-I" + str(TREE / "runtime/platform"), "-I" + str(TREE / "runtime/diag"),
                        str(d / "driver.c"), str(d / "images.c"), str(d / "out/igjobs_register.c"), *lifted,
                        *[str(spu / s) for s in sources], str(TREE / "runtime/platform/win32_compat.c"),
                        str(TREE / "runtime/diag/kz3_wait_acct.c"), "-pthread", "-lm", "-rdynamic", "-o", str(binary)],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-4000:])
        raise SystemExit("FAIL link against the runtime (%s)" % tag)
    jsrc = (d / "out" / ("igjob_%08x" % J_EA) / "spu_recomp.c").read_text()
    log = (d / "out" / ("igjob_%08x.log" % J_EA)).read_text()
    return binary, jsrc, log


env = {k: v for k, v in os.environ.items() if not k.startswith(("SPU_", "SPURS_", "YDKJ_", "LBP_", "KZ3_", "RC_"))}


def run(binary, n):
    r = subprocess.run([str(binary), str(n)], capture_output=True, text=True, timeout=30, cwd=binary.parent, env=env)
    m = re.search(r"status=0x(\w+) stop=0x(\w+) r3=0x(\w+) depth=(-?\d+) interp_job_entry=(\d+)", r.stdout)
    unwinds = len(re.findall(r"\[spu-jobframe\] unwind", r.stderr))
    halt = "[spu-depth]" in r.stderr
    v = dict(rc=r.returncode, status=int(m.group(1), 16), stop=int(m.group(2), 16), r3=int(m.group(3), 16),
             depth=int(m.group(4)), interp=int(m.group(5)), unwinds=unwinds, halt=halt) if m else \
        dict(rc=r.returncode, status=-1, stop=-1, r3=-1, depth=-1, interp=-1, unwinds=unwinds, halt=halt)
    print("  n=%d -> %s" % (n, v))
    return v


def ok_run(v, n):
    return v["rc"] == 0 and v["interp"] == 0 and v["status"] == 2 and v["stop"] == 0x1234 and v["r3"] == 5 * n and v["depth"] == 0 \
        and not v["halt"] and v["unwinds"] == 0


# the encodings above are what they claim to be (the tree's own disassembler)
sys.path.insert(0, str(TREE / "tools"))
from spu_disasm import disassemble_spu
dis = {i.addr: i for i in disassemble_spu(words([w for _, w, _ in JOB_CODE]), 0x130)}
check(all(dis[a].mnemonic == mn for a, _, mn in JOB_CODE), "synthetic job decodes as written")
check(dis[0x154].operands.replace(" ", "").endswith("0x168") and dis[0x16C].operands.replace(" ", "") == "$r78,$r77",
      "synthetic job: brsl $r77 -> packer 0x168, packer `bisl $r78,$r77`")

with tempfile.TemporaryDirectory(prefix="igjob-callback-") as tmp:
    tmp = Path(os.environ.get("RC_TEST_KEEP") or tmp)   # RC_TEST_KEEP=DIR keeps the lifts and binaries there
    tmp.mkdir(parents=True, exist_ok=True)
    fixed, fsrc, flog = lift_and_build(tmp, "fixed", True)
    ctrl, csrc, clog = lift_and_build(tmp, "control", False)

    check("1 callback brsl site(s)" in flog and "0x154" in flog.split("callback brsl")[1].split("\n")[0],
          "fixed lift: the classifier names the one site 0x154 (the loop's `brsl $r0` and the packer stay calls)")
    check(re.search(r"ctx->gpr\[77\] = spu_link\(0x158\); SPU_HOP\(ctx, 0x168,", fsrc) is not None,
          "fixed lift: `brsl $r77` is `r77 = link; branch to the packer`, no host bracket")
    check("hop_ret_pc = 0x13C; ctx->host_depth++;" in fsrc, "fixed lift: the loop's `brsl $r0, F` is still a call bracket")
    check("SPU_RET_REG(ctx, 78)" in fsrc, "fixed lift: the producer's `bi $r78` is a link-register return")
    check(re.search(r"ctx->gpr\[77\] = spu_link\(0x158\); \{ int32_t _si", csrc) is not None and "callback brsl" not in clog,
          "control lift (callback_brsl false): `brsl $r77` is a host call bracket, as before 7106")

    print("fixed:")
    for n in (1, 10, 1500, 6000):
        v = run(fixed, n)
        check(ok_run(v, n), "fixed n=%d: job ran lifted, stop 0x1234, r3 = %d, depth 0, no [spu-depth] halt, no 7099 unwind" % (n, 5 * n))

    print("negative control (the lift before 7106):")
    v_small = run(ctrl, 10)
    v_big = run(ctrl, 1500)
    ctrl_fails = not ok_run(v_small, 10) and not ok_run(v_big, 1500)
    print(("PASS " if ctrl_fails else "FAIL ") + "negative control fails: n=10 needs 7099 unwinds (%d), n=1500 halt=%s r3=0x%X"
          % (v_small["unwinds"], v_big["halt"], v_big["r3"]))
    if not ctrl_fails:
        failures.append("negative control passed: the test does not see the leak")
    check(v_small["unwinds"] >= 1 and v_small["r3"] == 50,
          "control n=10 finishes only through 7099 unwinding the dead frames at the job's exit (the A4O shape)")
    check(v_big["halt"] and v_big["r3"] != 5 * 1500,
          "control n=1500: [spu-depth] halt before the job ends (the Nexus prison_transport shape)")

if failures:
    raise SystemExit("FAILED: " + "; ".join(failures))
print("PASS test_igjob_callback_brsl")

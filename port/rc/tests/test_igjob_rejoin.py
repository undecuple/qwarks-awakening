#!/usr/bin/env python3
"""Host test of tools/spu/lift_igjobs.py and patch 7039 (an interpreted policy module hands over to lifted code in
verified resident spans, and back). No game data: a synthetic big-endian ELF holds a hand-assembled "job kernel" (linked
for LS 0x3F580, entry 0x3F5A0) and a "job blob" in the format-A layout (4x `ila $r2`, then the job header quad: LS dest
0x100, size 0x40, entry 0x130). lift_igjobs.py lifts both and writes igjobs_register.c; the test checks the TU, then links
it with the lifted C and the patched SPU runtime and runs an image -1 policy context the way spurs_pm_interp_entry does:
an interpreted loader at 0xA00 calls the kernel, the kernel calls the job (lifted, resident), then an unlifted "job" at
0x2000 (interpreted through the 1069 fallback, back into the lifted kernel), then stops. The interpreter's pc histogram
(KZ3 patch 1070) shows which code ran interpreted.
    python3 port/rc/tests/test_igjob_rejoin.py PS3RECOMP_TREE      (run_igjob_rejoin_test.sh)"""
import json, os, re, struct, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TREE = Path(sys.argv[1]).resolve()
TOOL = REPO / "tools/spu/lift_igjobs.py"


def ila(rt, imm): return (0x21 << 25) | ((imm & 0x3FFFF) << 7) | rt
def il(rt, imm): return (0x081 << 23) | ((imm & 0xFFFF) << 7) | rt
def ai(rt, ra, imm): return (0x1C << 24) | ((imm & 0x3FF) << 14) | (ra << 7) | rt
def brasl(rt, tgt): return (0x062 << 23) | (((tgt >> 2) & 0xFFFF) << 7) | rt
def bisl(rt, ra): return (0x1A9 << 21) | (ra << 7) | rt
def stop(code): return code & 0x3FFF
BI_R0, LNOP = 0x35000000, 0x00200000


def words(ws): return b"".join(struct.pack(">I", w) for w in ws)


KLSA, KENTRY = 0x3F580, 0x3F5A0
KERNEL = words([0x12345678, 0x9ABCDEF0, 0xCAFEF00D, 0, 0xFFFFFFFF, 1, 2, 3,   # 0x3F580 data (decodes as junk)
                ila(15, 0x130), bisl(0, 15), ai(3, 3, 0x10),                    # 0x3F5A0 call the job
                ila(15, 0x2000), bisl(0, 15), ai(3, 3, 0x100),                  # 0x3F5AC call an unlifted job
                stop(0x1234), LNOP])                                            # 0x3F5B8
KEND = KLSA + len(KERNEL)
JOB = words([ila(2, 0x11), ila(2, 0x22), ila(2, 0x33), ila(2, 0x44),
             (0x100 >> 2) << 16, ((0x40 >> 2) << 16) | (0x140 >> 2), ((0x10 >> 2) << 16) | (0x130 >> 2), 0,
             LNOP, LNOP, LNOP, LNOP,
             ai(3, 3, 1), BI_R0, LNOP, LNOP])                                   # entry 0x130
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
#include <execinfo.h>
extern void spu_overlay_note_get(spu_context*, uint32_t, const uint8_t*, uint32_t);
extern int spu_run_with_halt(void (SPU_ABI *)(spu_context*), spu_context*);
extern void SPU_ABI spurs_pm_interp_entry(spu_context*);
extern _Thread_local uint32_t* g_spu_interp_pc_hist;
extern _Thread_local uint32_t g_spu_interp_pc_hist_lo, g_spu_interp_pc_hist_n;
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
static uint32_t hist[0x10000];
static spu_context ctx;
static void on_alarm(int sig)   /* a hang: say where the SPU is, then fail */
{
    (void)sig;
    fprintf(stderr, "HANG pc=0x%05X status=0x%X r3=0x%X r0=0x%05X hist 130=%u 2000=%u 3f5a0=%u 3f5b4=%u\n", ctx.pc,
            ctx.status, ctx.gpr[3]._u32[0], ctx.gpr[0]._u32[0], hist[0x130 >> 2], hist[0x2000 >> 2], hist[0x3F5A0 >> 2],
            hist[0x3F5B4 >> 2]);
    void* bt[32]; backtrace_symbols_fd(bt, backtrace(bt, 32), 2);
    _exit(3);
}
int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "lifted";
    signal(SIGALRM, on_alarm); alarm(5);
    spu_context_init(&ctx, 0);
    ctx.policy_mode = 1;
    ctx.image_id = -1;                                /* patch 7034: an unlifted policy module */
    word(&ctx, 0xA00, IL_R3_0);                       /* the loader (interpreted) */
    word(&ctx, 0xA04, BRASL_KERNEL);
    word(&ctx, 0xA08, STOP_DEAD);
    word(&ctx, 0x2000, AI_R3_2);                      /* a job with no lift */
    word(&ctx, 0x2004, BI_R0);
    memcpy(ctx.ls + KLSA, kernel, kernel_len);
    if (strcmp(mode, "nokernel")) spu_overlay_note_get(&ctx, K_EA, ctx.ls + KLSA, kernel_len);
    memcpy(ctx.ls + 0x100, job, job_len);
    if (!strcmp(mode, "badjob")) word(&ctx, 0x130, AI_R3_4);   /* other bytes at the job's EA: verification fails */
    spu_overlay_note_get(&ctx, J_EA, ctx.ls + 0x100, job_len);
    g_spu_interp_pc_hist = hist; g_spu_interp_pc_hist_lo = 0; g_spu_interp_pc_hist_n = 0x10000;
    spu_run_with_halt(spurs_pm_interp_entry, &ctx);
    printf("mode=%s status=0x%X stop=0x%X r3=0x%X hist: a00=%u a04=%u 130=%u 2000=%u 3f5a0=%u 3f5b4=%u\n", mode,
           ctx.status, ctx.stop_code, ctx.gpr[3]._u32[0], hist[0xA00 >> 2], hist[0xA04 >> 2], hist[0x130 >> 2],
           hist[0x2000 >> 2], hist[0x3F5A0 >> 2], hist[0x3F5B4 >> 2]);
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="igjob-rejoin-") as tmp:
    tmp = Path(os.environ.get("RC_TEST_KEEP") or tmp)   # RC_TEST_KEEP=DIR keeps the lift and the binary there
    tmp.mkdir(parents=True, exist_ok=True)
    (tmp / "game.elf").write_bytes(elf())
    table = {"title": "synthetic",
             "kernel": {"ea": hex(K_EA), "lsa": hex(KLSA), "get_size": hex(len(KERNEL)), "entry": hex(KENTRY),
                        "code_end": hex(KEND), "span": hex(len(KERNEL)), "image_id": 51,
                        "data_ranges": ["0x%x-0x%x" % (KLSA, KENTRY)]},
             "jobs": [{"ea": hex(J_EA), "image_id": 60}]}
    (tmp / "table.json").write_text(json.dumps(table))
    r = subprocess.run([sys.executable, "-I", str(TOOL), str(tmp / "game.elf"), str(TREE), str(tmp / "out"),
                        str(tmp / "table.json")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit("FAIL lift_igjobs.py")
    man = json.loads((tmp / "out/igjobs_manifest.json").read_text())
    check([(m["kind"], m["lsa"], m["span"], m["entry"]) for m in man["regions"]] ==
          [("kernel", hex(KLSA), hex(len(KERNEL)), hex(KENTRY)), ("job", "0x100", "0x40", "0x130")],
          "manifest: kernel at its table geometry, job geometry decoded from its header quad")
    reg = (tmp / "out/igjobs_register.c").read_text()
    calls = re.findall(r"spu_overlay_register_verified_region\(0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+), (\d+)", reg)
    check(calls == [("%08X" % K_EA, "%05X" % KLSA, "%X" % len(KERNEL), "51"), ("%08X" % J_EA, "00100", "40", "60")],
          "verified regions registered at (EA, LS address, span, image id)")
    for name, raw in (("igkernel_%08x" % K_EA, KERNEL), ("igjob_%08x" % J_EA, JOB)):
        arr = re.search(r"static const uint8_t k_%s\[(\d+)\] = \{([^}]*)\}" % name, reg)
        got = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", arr.group(2))) if arr else b""
        check(got == raw, f"{name}: verification bytes are the image")
    ksrc = (tmp / "out" / ("igkernel_%08x" % K_EA) / "spu_recomp.c").read_text()
    check("spu_func_0003F5A0(" in ksrc and "spu_func_0003F580(" not in ksrc, "kernel lifted from its entry, data skipped")

    spu = TREE / "runtime/spu"
    consts = {"IL_R3_0": il(3, 0), "BRASL_KERNEL": brasl(0, KENTRY), "STOP_DEAD": stop(0xDEA), "AI_R3_2": ai(3, 3, 2),
              "BI_R0": BI_R0, "AI_R3_4": ai(3, 3, 4), "KLSA": KLSA, "K_EA": K_EA, "J_EA": J_EA}
    src = DRIVER
    for k, v in consts.items():
        src = re.sub(r"\b%s\b" % k, "0x%Xu" % v, src)
    (tmp / "driver.c").write_text(src)
    (tmp / "images.c").write_text("".join(
        f"const unsigned char {n}[] = {{{','.join(str(x) for x in raw)}}};\nconst unsigned {n}_len = {len(raw)};\n"
        for n, raw in (("kernel", KERNEL), ("job", JOB))))
    sources = ["spu_channels.c", "spu_drain.c", "spu_lockstep.c", "spu_coherency.c", "spu_workload.c", "spurs_policy.c",
               "spurs_job.c", "spu_tsp_weak.c", "spu_vm_pagemap.c", "spurs_policy_blob_weak.c", "spu_interp.c"]
    lifted = [str(tmp / "out" / m["name"] / "spu_recomp.c") for m in man["regions"]]
    binary = tmp / "rejoin"
    r = subprocess.run([os.environ.get("CC", "cc"), "-std=gnu17", "-O1", "-UNDEBUG", "-w", "-I" + str(TREE / "include"),
                        "-I" + str(spu), "-I" + str(TREE / "runtime/platform"), str(tmp / "driver.c"), str(tmp / "images.c"),
                        str(tmp / "out/igjobs_register.c"), *lifted, *[str(spu / s) for s in sources],
                        str(TREE / "runtime/platform/win32_compat.c"), str(TREE / "runtime/diag/kz3_wait_acct.c"), "-I" + str(TREE / "runtime/diag"), "-pthread", "-lm", "-rdynamic", "-o", str(binary)],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-4000:])
        raise SystemExit("FAIL link against the runtime")
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SPU_", "SPURS_", "YDKJ_", "LBP_", "KZ3_", "RC_"))}

    def run(mode):
        r = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=20, cwd=tmp, env=env)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip()[-2000:])
        m = re.search(r"status=0x(\w+) stop=0x(\w+) r3=0x(\w+) hist: a00=(\d+) a04=(\d+) 130=(\d+) 2000=(\d+) "
                      r"3f5a0=(\d+) 3f5b4=(\d+)", r.stdout)
        # (status, r3, a00, a04, 130, 2000, 3f5a0, 3f5b4)
        return (int(m.group(1), 16), int(m.group(3), 16), *(int(m.group(i)) for i in range(4, 10))) if m else None

    v = run("lifted")
    check(v is not None and v[0] == 2 and v[1] == 0x113, "lifted: kernel, both jobs and the stop all ran (r3 0x113)")
    check(v is not None and v[2] == 1 and v[3] == 1, "lifted: the loader ran interpreted")
    check(v is not None and v[6] == 0 and v[7] == 0, "lifted: the resident kernel ran lifted (interpreter handed over), also after the interpreted job returned")
    check(v is not None and v[4] == 0, "lifted: the resident job ran lifted")
    check(v is not None and v[5] == 1, "lifted: the job with no lift ran interpreted and returned into the kernel")
    v = run("badjob")
    check(v is not None and v[0] == 2 and v[1] == 0x116 and v[4] == 1 and v[6] == 0 and v[7] == 0,
          "badjob: other bytes at the job's EA run interpreted, the kernel stays lifted")
    v = run("nokernel")
    check(v is not None and v[0] == 2 and v[1] == 0x113 and v[6] == 1 and v[7] == 1,
          "nokernel: without a resident kernel everything runs interpreted as before")
    env["RC_IGJOBS"] = "0"
    v = run("lifted")
    check(v is not None and v[0] == 2 and v[1] == 0x113 and v[4] == 1 and v[6] == 1 and v[7] == 1,
          "RC_IGJOBS=0: nothing registered, kernel and job run interpreted")

if failures:
    raise SystemExit("FAILED: " + "; ".join(failures))
print("PASS test_igjob_rejoin")

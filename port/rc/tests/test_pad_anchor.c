/* Host test for R&C patch 7062: PAD_SCRIPT anchors ("@fs:TEXT", libs/input/pad_script_anchor.h). It runs cellPad.c's
 * own script parser and player (pad_script_parse / pad_script_poll, cut from the patched cellPad.c by
 * run_pad_anchor_test.sh), not a copy.
 * 1. Parse: events after the anchor are marked anchored, events before it are not; the anchor text ends at a separator;
 *    a second anchor, an unknown kind and an empty text count as unparsable.
 * 2. Play: unanchored events fire on the pad clock as before; anchored events never fire before the anchor, however
 *    late; an open of another path does not fire it; the first matching open fires it at the next poll and the
 *    anchored events then fire at anchor + t; a later matching open does not move it.
 * 3. The same events without the anchor fire at their absolute seconds (the control: the anchor is what holds them).
 * 4. tools/pad/tods-newgame-anchored.txt (argv[1]) parses with no bad token: the 6 menu events unanchored, the rest
 *    anchored to Metropolis' streaming_sound.dat.
 * 5. R&C patch 7064, the frame clock (pad_anchor_frame_clock, 60 flips = 1 s): anchored events and their holds count
 *    flips since the anchor whatever the wall clock does; unanchored events stay on the pad clock.
 * No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAD_ANCHOR_NO_FS_HOOK 1
#include "pad_script_cut.inc"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static PadScript s;

static unsigned poll_at(double now, int* fired)
{
    unsigned m = 0; unsigned char ax[6] = {0}; int ax_set = 0;
    *fired = pad_script_poll(&s, now, &m, ax, &ax_set);
    return m | (ax_set && ax[1] < 64 ? 0x10000u : 0u);   /* bit 16: left stick up held */
}

int main(int argc, char** argv)
{
    static const char* LVL = "/dev_hdd0/game/BCUS98127/USRDIR/built/levels/metropolis/streaming_sound.dat";
    /* 1. parse */
    memset(&s, 0, sizeof s);
    pad_script_parse(&s, "20:0x0008 120:0x4000  # menu\n@fs:levels/metropolis/streaming_sound.dat\n"
                         "0:0x0800 5:0,-1,0,0/2 10:0x8000");
    CHECK(s.n == 5 && s.bad == 0, "parsed %d events, %d bad (want 5, 0)", s.n, s.bad);
    CHECK(s.anchor.set && !strcmp(s.anchor.text, "levels/metropolis/streaming_sound.dat"), "anchor text '%s'", s.anchor.text);
    CHECK(!s.ev[0].anchored && !s.ev[1].anchored && s.ev[2].anchored && s.ev[3].anchored && s.ev[4].anchored,
          "anchored flags %d %d %d %d %d (want 0 0 1 1 1)", s.ev[0].anchored, s.ev[1].anchored, s.ev[2].anchored,
          s.ev[3].anchored, s.ev[4].anchored);
    {
        PadScript b; memset(&b, 0, sizeof b);
        pad_script_parse(&b, "@fs:a.dat,1:0x1 @fs:b.dat 2:0x2 @xx:c 3:0x4 @fs: 4:0x8");
        CHECK(b.n == 4 && b.bad == 3 && !strcmp(b.anchor.text, "a.dat"), "second/unknown/empty anchor: n %d bad %d text '%s'",
              b.n, b.bad, b.anchor.text);
    }
    /* 2. play */
    int f = 0; unsigned m;
    m = poll_at(20.0, &f);  CHECK(f == 1 && (m & 0x0008), "START at 20 s: fired %d mask 0x%X", f, m);
    m = poll_at(120.0, &f); CHECK(f == 1 && (m & 0x4000), "CROSS at 120 s: fired %d", f);
    m = poll_at(600.0, &f); CHECK(f == 0 && !(m & 0x0800), "an anchored event fired before the anchor (at 600 s)");
    CHECK(pad_anchor_note(&s.anchor, "/dev_hdd0/game/BCUS98127/USRDIR/built/levels/metropolis/level_cached.dat") == 0,
          "another path fired the anchor");
    m = poll_at(601.0, &f); CHECK(f == 0, "fired %d without the anchor", f);
    CHECK(pad_anchor_note(&s.anchor, LVL) == 1, "the level path did not fire the anchor");
    m = poll_at(610.0, &f);
    CHECK(s.anchor.resolved && s.anchor.at == 610.0, "anchor at %.1f (want 610.0, the first poll after the open)", s.anchor.at);
    CHECK(f == 1 && (m & 0x0800), "@+0 event at the anchor poll: fired %d mask 0x%X", f, m);
    CHECK(pad_anchor_note(&s.anchor, LVL) == 0, "a second open fired the anchor again");
    m = poll_at(614.9, &f); CHECK(f == 0 && !(m & 0x10000u), "@+5 stick fired early at 614.9");
    m = poll_at(615.0, &f); CHECK(f == 1 && (m & 0x10000u), "@+5 stick at 615.0: fired %d", f);
    CHECK(s.anchor.at == 610.0, "the anchor moved to %.1f", s.anchor.at);
    m = poll_at(620.0, &f); CHECK(f == 1 && (m & 0x8000), "@+10 SQUARE at 620.0: fired %d", f);
    /* 3. control: the same in-level events without the anchor fire at their absolute seconds (0, 5, 10) */
    memset(&s, 0, sizeof s);
    pad_script_parse(&s, "0:0x0800 5:0,-1,0,0/2 10:0x8000");
    m = poll_at(600.0, &f);
    CHECK(f == 3, "without the anchor %d of 3 in-level events fired at 600 s (want all 3)", f);

    /* 4. the ToD script */
    if (argc > 1) {
        char* txt = pad_script_read_file(argv[1]);
        CHECK(txt != NULL, "cannot read %s", argv[1]);
        if (txt) {
            memset(&s, 0, sizeof s);
            pad_script_parse(&s, txt); free(txt);
            int na = 0; for (int i = 0; i < s.n; i++) na += s.ev[i].anchored;
            printf("%s: %d events, %d anchored, %d bad, anchor '%s'\n", argv[1], s.n, na, s.bad, s.anchor.text);
            CHECK(s.bad == 0 && s.dropped == 0 && s.n - na == 6 && na > 300 &&
                  !strcmp(s.anchor.text, "levels/metropolis/streaming_sound.dat"), "the ToD anchored script");
        }
    }

    /* 5. the frame clock (R&C patch 7064) */
    memset(&s, 0, sizeof s);
    pad_script_parse(&s, "20:0x0008 @fs:levels/metropolis/streaming_sound.dat 0:0x0800 5:0,-1,0,0/2 10:0x8000");
    pad_anchor_frame_clock(&s.anchor, 60.0);
    pad_anchor_frames(&s.anchor, 900);  m = poll_at(20.0, &f);   CHECK(f == 1 && (m & 0x0008), "fclock: START on the pad clock");
    pad_anchor_note(&s.anchor, LVL);
    pad_anchor_frames(&s.anchor, 1000); m = poll_at(610.0, &f);
    CHECK(s.anchor.resolved && s.anchor.at_f == 1000 && f == 1 && (m & 0x0800), "fclock: anchor at flip %.0f, fired %d",
          s.anchor.at_f, f);
    pad_anchor_frames(&s.anchor, 1299); m = poll_at(700.0, &f);  /* 90 wall s later but 299 flips: not yet 5 s */
    CHECK(f == 0 && !(m & 0x10000u), "fclock: @+5 stick fired at 299 flips (wall clock leaked in)");
    pad_anchor_frames(&s.anchor, 1300); m = poll_at(700.0, &f);
    CHECK(f == 1 && (m & 0x10000u), "fclock: @+5 stick at 300 flips: fired %d", f);
    pad_anchor_frames(&s.anchor, 1419); m = poll_at(900.0, &f);  /* hold /2 = 120 flips, wall time irrelevant */
    CHECK(f == 0 && (m & 0x10000u), "fclock: the 2 s hold ended before 120 flips");
    pad_anchor_frames(&s.anchor, 1421); m = poll_at(900.0, &f);
    CHECK(!(m & 0x10000u), "fclock: the 2 s hold outlived 121 flips");
    pad_anchor_frames(&s.anchor, 1599); m = poll_at(5000.0, &f); CHECK(f == 0, "fclock: @+10 fired at 599 flips");
    pad_anchor_frames(&s.anchor, 1600); m = poll_at(5000.0, &f);
    CHECK(f == 1 && (m & 0x8000), "fclock: @+10 SQUARE at 600 flips: fired %d", f);

    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("test_pad_anchor: all checks pass\n");
    return 0;
}

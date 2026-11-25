/* Host test for R&C patch 7114: a licence type 2 EDAT with no licence data is refused (-6, nothing written) and
 * the log line names no key, no key file and no procedure. Everything is synthetic: a bare NPD header (license 2,
 * a made-up content id, a dev_hash no klicensee verifies) and no key of any kind. Built by run_edat_hint_test.sh with
 * the patched tree's edat.c included directly; argv[1] = a scratch directory. */
#include "edat.c"

static void put32(uint8_t* p, uint32_t v)
{ p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stdout, "usage: test_edat_licence_hint SCRATCH_DIR\n"); return 2; }
    char in[1024], out[1024], log[1024];
    snprintf(in, sizeof in, "%s/synthetic.edat", argv[1]);
    snprintf(out, sizeof out, "%s/synthetic.dec", argv[1]);
    snprintf(log, sizeof log, "%s/stderr.txt", argv[1]);
    uint8_t hdr[0x100 + 0x40] = {0};
    memcpy(hdr, "NPD\0", 4);
    put32(hdr + 0x04, 3);                       /* version */
    put32(hdr + 0x08, 2);                       /* license type 2 (local licence) */
    memcpy(hdr + 0x10, "XX0000-TEST00000_00-SYNTHETICEDAT0002", 36);
    for (int i = 0; i < 16; i++) hdr[0x60 + i] = (uint8_t)(0xC0 + i);   /* dev_hash no klicensee verifies */
    put32(hdr + 0x84, 0x4000);                  /* block size */
    hdr[0x8F] = 0x20;                           /* file size 32 */
    FILE* f = fopen(in, "wb");
    if (!f || fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr) { fprintf(stdout, "FAIL: cannot write %s\n", in); return 1; }
    fclose(f);
    fflush(stderr);
    if (!freopen(log, "w", stderr)) { fprintf(stdout, "FAIL: cannot redirect stderr\n"); return 1; }
    int rc = edat_decrypt_file(in, out);
    fflush(stderr);
    char text[8192] = {0};
    FILE* l = fopen(log, "rb");
    size_t n = l ? fread(text, 1, sizeof text - 1, l) : 0;
    if (l) fclose(l);
    text[n] = 0;
    int fails = 0;
#define CHECK(c, what) do { if (c) fprintf(stdout, "ok   %s\n", what); else { fprintf(stdout, "FAIL %s\n", what); fails++; } } while (0)
    CHECK(rc == -6, "licence type 2 without licence data: refused (-6)");
    struct stat st;
    CHECK(stat(out, &st) != 0, "nothing is written");
    CHECK(strstr(text, "license type 2: no licence data for ") != NULL && strstr(text, "the file stays encrypted") != NULL,
          "the log line says only that the file has no licence data and stays encrypted");
    const char* banned[] = {".rap", "rap ", "rifkey", "PS3_RIF_KEY", "RIF key", "derived", "hex chars", "raw bytes"};
    for (size_t i = 0; i < sizeof banned / sizeof banned[0]; i++) {
        char what[96];
        snprintf(what, sizeof what, "the log names no key procedure (no \"%s\")", banned[i]);
        CHECK(strstr(text, banned[i]) == NULL, what);
    }
    if (fails) fprintf(stdout, "--- stderr ---\n%s--------------\n", text);
    return fails ? 1 : 0;
}

/* Ratchet & Clank port: cellL10n L10nConvertStr for the titles whose runtime import is unresolved (the Collection games).
 * Header-only, included by port/rc/<game>/hle_<game>.cpp, which registers rc_l10n::hle_L10nConvertStr for NID 0x931FF25A. */
#pragma once
#include "ppu_recomp.h"
#include <stdint.h>
#include <stdio.h>

/* cellL10n L10nConvertStr(s32 src_code, const void* src, size_t* src_len, s32 dst_code, void* dst, size_t* dst_len)
 * -> L10nResult. NID 0x931FF25A. Shared by the Collection games (RC1 call site func_00551A64 @0x00551AF8, save-data strings:
 * src_code 5 or 1, dst_code 0); each game registers it in its own hle_<game>.cpp. Contract (cellL10n reference, [src:rpcs3] read only): src_len is
 * a byte count; *dst_len in = destination capacity in bytes, out = bytes written (or needed when dst is 0, a size query);
 * no terminating NUL is written; results ConversionOK 0, SRCIllegal 1, DSTExhausted 2, ConverterUnknown 3. Codes (the SDK
 * enum, which has UCS4 at 4): UTF8 0, UTF16 1, UTF32 2, UCS2 3, UCS4 4, ISO_8859_1 5. PS3 UTF-16/32 are big-endian.
 * The runtime's libs/font/cellL10n.h numbers the enum without UCS4 (ISO_8859_1 = 6), so its l10n_convert_str is not used.
 * Handled: sources UTF8, UTF16, UTF32, UCS2, UCS4, ISO-8859-1; destinations the same set. Others: ConverterUnknown. */
namespace rc_l10n {
enum { L_UTF8 = 0, L_UTF16 = 1, L_UTF32 = 2, L_UCS2 = 3, L_UCS4 = 4, L_ISO_8859_1 = 5 };
enum { ConversionOK = 0, SRCIllegal = 1, DSTExhausted = 2, ConverterUnknown = 3 };

static inline bool known(int32_t c) { return c >= L_UTF8 && c <= L_ISO_8859_1; }

/* Next code point from guest memory [p, end); returns bytes consumed, 0 on an illegal or incomplete sequence. */
static inline uint32_t decode(int32_t code, uint32_t p, uint32_t end, uint32_t* cp)
{
    uint32_t n = end - p;
    switch (code) {
    case L_ISO_8859_1: *cp = vm_read8(p); return 1;
    case L_UTF32: case L_UCS4:
        if (n < 4) return 0;
        *cp = vm_read32(p);
        return (*cp > 0x10FFFFu || (*cp >= 0xD800u && *cp <= 0xDFFFu)) ? 0 : 4;
    case L_UCS2:
        if (n < 2) return 0;
        *cp = vm_read16(p);
        return (*cp >= 0xD800u && *cp <= 0xDFFFu) ? 0 : 2;
    case L_UTF16: {
        if (n < 2) return 0;
        uint32_t w = vm_read16(p);
        if (w < 0xD800u || w > 0xDFFFu) { *cp = w; return 2; }
        if (w > 0xDBFFu || n < 4) return 0;
        uint32_t w2 = vm_read16(p + 2);
        if (w2 < 0xDC00u || w2 > 0xDFFFu) return 0;
        *cp = 0x10000u + ((w - 0xD800u) << 10) + (w2 - 0xDC00u);
        return 4;
    }
    case L_UTF8: {
        uint32_t b = vm_read8(p), len, c;
        if (b < 0x80u) { *cp = b; return 1; }
        if ((b & 0xE0u) == 0xC0u) { len = 2; c = b & 0x1Fu; }
        else if ((b & 0xF0u) == 0xE0u) { len = 3; c = b & 0x0Fu; }
        else if ((b & 0xF8u) == 0xF0u) { len = 4; c = b & 0x07u; }
        else return 0;
        if (n < len) return 0;
        for (uint32_t i = 1; i < len; i++) {
            uint32_t t = vm_read8(p + i);
            if ((t & 0xC0u) != 0x80u) return 0;
            c = (c << 6) | (t & 0x3Fu);
        }
        static const uint32_t min_cp[5] = { 0, 0, 0x80u, 0x800u, 0x10000u };
        if (c < min_cp[len] || c > 0x10FFFFu || (c >= 0xD800u && c <= 0xDFFFu)) return 0;
        *cp = c; return len;
    }
    }
    return 0;
}

/* Encode cp for dst_code into out[]; returns the byte count, 0 if the code point has no form there. */
static inline uint32_t encode(int32_t code, uint32_t cp, uint8_t out[4])
{
    switch (code) {
    case L_ISO_8859_1: if (cp > 0xFFu) return 0; out[0] = (uint8_t)cp; return 1;
    case L_UTF32: case L_UCS4: out[0] = (uint8_t)(cp >> 24); out[1] = (uint8_t)(cp >> 16); out[2] = (uint8_t)(cp >> 8); out[3] = (uint8_t)cp; return 4;
    case L_UCS2: if (cp > 0xFFFFu) return 0; out[0] = (uint8_t)(cp >> 8); out[1] = (uint8_t)cp; return 2;
    case L_UTF16:
        if (cp < 0x10000u) { out[0] = (uint8_t)(cp >> 8); out[1] = (uint8_t)cp; return 2; }
        { uint32_t v = cp - 0x10000u, hi = 0xD800u + (v >> 10), lo = 0xDC00u + (v & 0x3FFu);
          out[0] = (uint8_t)(hi >> 8); out[1] = (uint8_t)hi; out[2] = (uint8_t)(lo >> 8); out[3] = (uint8_t)lo; return 4; }
    case L_UTF8:
        if (cp < 0x80u)    { out[0] = (uint8_t)cp; return 1; }
        if (cp < 0x800u)   { out[0] = (uint8_t)(0xC0u | (cp >> 6)); out[1] = (uint8_t)(0x80u | (cp & 0x3Fu)); return 2; }
        if (cp < 0x10000u) { out[0] = (uint8_t)(0xE0u | (cp >> 12)); out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
                             out[2] = (uint8_t)(0x80u | (cp & 0x3Fu)); return 3; }
        out[0] = (uint8_t)(0xF0u | (cp >> 18)); out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu)); out[3] = (uint8_t)(0x80u | (cp & 0x3Fu)); return 4;
    }
    return 0;
}

static inline void hle_L10nConvertStr(ppu_context* ctx)
{
    const int32_t src_code = (int32_t)ctx->gpr[3], dst_code = (int32_t)ctx->gpr[6];
    const uint32_t src = (uint32_t)ctx->gpr[4], src_len_p = (uint32_t)ctx->gpr[5];
    const uint32_t dst = (uint32_t)ctx->gpr[7], dst_len_p = (uint32_t)ctx->gpr[8];
    int32_t rc = ConversionOK;
    uint32_t wrote = 0;
    if (!known(src_code) || !known(dst_code) || !src_len_p || !dst_len_p) {
        rc = ConverterUnknown;
    } else {
        const uint32_t cap = vm_read32(dst_len_p), end = src + vm_read32(src_len_p);
        for (uint32_t p = src; p < end; ) {
            uint32_t cp, used = decode(src_code, p, end, &cp);
            if (!used) { rc = SRCIllegal; break; }
            uint8_t b[4]; uint32_t k = encode(dst_code, cp, b);
            if (!k) { rc = SRCIllegal; break; }   /* no form in the destination code (e.g. > U+00FF to ISO-8859-1) */
            if (dst) {
                if (wrote + k > cap) { rc = DSTExhausted; break; }
                for (uint32_t i = 0; i < k; i++) vm_write8(dst + wrote + i, b[i]);
            }
            wrote += k;
            p += used;
        }
        vm_write32(dst_len_p, wrote);
    }
    static int logged = 0;
    if (logged < 4) {
        logged++;
        fprintf(stderr, "[hle] L10nConvertStr(src_code=%d, src=0x%08X, len=%u, dst_code=%d, dst=0x%08X) -> %d, %u bytes\n",
                src_code, src, src_len_p ? vm_read32(src_len_p) : 0u, dst_code, dst, rc, wrote);
    }
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}
} // namespace rc_l10n


/* CryptoAPI hashes for the Android port (wincrypt.h): MD5 (RFC 1321) and SHA-1 (FIPS 180-4), plain C. */
#include "windows.h"
#include "wincrypt.h"

typedef struct { int alg; uint64_t len; uint32_t h[5]; uint8_t buf[64]; size_t n; } Hash;

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static void md5_block(uint32_t *h, const uint8_t *p) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        uint32_t t = d; d = c; c = b;
        b = b + rol(a + f + K[i] + w[g], R[i]);
        a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}
static void sha1_block(uint32_t *h, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
static void block(Hash *s, const uint8_t *p) { if (s->alg == CALG_MD5) md5_block(s->h, p); else sha1_block(s->h, p); }
BOOL CryptCreateHash(HCRYPTPROV p, ALG_ID alg, HCRYPTKEY key, DWORD flags, HCRYPTHASH *out) {
    (void)p; (void)key; (void)flags;
    if (alg != CALG_MD5 && alg != CALG_SHA1) { *out = 0; return FALSE; }
    Hash *s = calloc(1, sizeof *s);
    s->alg = (int)alg;
    s->h[0] = 0x67452301; s->h[1] = 0xefcdab89; s->h[2] = 0x98badcfe; s->h[3] = 0x10325476; s->h[4] = 0xc3d2e1f0;
    *out = (HCRYPTHASH)(uintptr_t)s;
    return TRUE;
}
BOOL CryptHashData(HCRYPTHASH h, const BYTE *data, DWORD n, DWORD flags) {
    (void)flags;
    Hash *s = (Hash *)(uintptr_t)h;
    if (!s) return FALSE;
    s->len += n;
    while (n) {
        size_t k = 64 - s->n < n ? 64 - s->n : n;
        memcpy(s->buf + s->n, data, k);
        s->n += k; data += k; n -= (DWORD)k;
        if (s->n == 64) { block(s, s->buf); s->n = 0; }
    }
    return TRUE;
}
BOOL CryptGetHashParam(HCRYPTHASH h, DWORD param, BYTE *out, DWORD *len, DWORD flags) {
    (void)flags;
    Hash *s0 = (Hash *)(uintptr_t)h;
    if (!s0 || param != HP_HASHVAL) return FALSE;
    Hash s = *s0;                                         /* Windows finalises the hash; a copy keeps it usable */
    DWORD dl = s.alg == CALG_MD5 ? 16 : 20;
    if (!out || *len < dl) { *len = dl; return out == NULL; }
    uint64_t bits = s.len * 8;
    uint8_t pad = 0x80;
    s.buf[s.n++] = pad;
    if (s.n > 56) { memset(s.buf + s.n, 0, 64 - s.n); block(&s, s.buf); s.n = 0; }
    memset(s.buf + s.n, 0, 56 - s.n);
    for (int i = 0; i < 8; i++) s.buf[56 + i] = (uint8_t)(s.alg == CALG_MD5 ? bits >> (8 * i) : bits >> (56 - 8 * i));
    block(&s, s.buf);
    for (DWORD i = 0; i < dl / 4; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(s.alg == CALG_MD5 ? s.h[i] >> (8 * j) : s.h[i] >> (24 - 8 * j));
    *len = dl;
    return TRUE;
}
BOOL CryptDestroyHash(HCRYPTHASH h) { free((void *)(uintptr_t)h); return TRUE; }

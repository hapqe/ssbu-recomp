/* CryptoAPI hashes for the Android port (wincrypt.h): MD5 (RFC 1321) and SHA-1 (FIPS 180-4), plain C; and the SHA-256 of CNG (bcrypt.dll) that nnro.c uses. */
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

/* ---------------------------------------------------------------- CNG (bcrypt.dll): SHA-256 (FIPS 180-4) for nnro.c
 * LoadLibraryA("bcrypt.dll") gives libmain.so itself (win32_compat.c), so GetProcAddress finds these two. */
static void sha256_block(uint32_t h[8], const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rol(w[i - 15], 25) ^ rol(w[i - 15], 14) ^ (w[i - 15] >> 3), s1 = rol(w[i - 2], 15) ^ rol(w[i - 2], 13) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (rol(e, 26) ^ rol(e, 21) ^ rol(e, 7)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rol(a, 30) ^ rol(a, 19) ^ rol(a, 10)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}
static void sha256(const uint8_t *p, size_t n, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t last[128] = {0};
    size_t full = n & ~(size_t)63, rest = n - full, pad = rest < 56 ? 64 : 128;
    for (size_t i = 0; i < full; i += 64) sha256_block(h, p + i);
    memcpy(last, p + full, rest);
    last[rest] = 0x80;
    for (int i = 0; i < 8; i++) last[pad - 1 - i] = (uint8_t)((uint64_t)n * 8 >> (8 * i));
    for (size_t i = 0; i < pad; i += 64) sha256_block(h, last + i);
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
}
LONG WINAPI BCryptOpenAlgorithmProvider(void **alg, const WCHAR *id, const WCHAR *impl, ULONG flags) {
    (void)impl; (void)flags;
    static const WCHAR want[] = {'S', 'H', 'A', '2', '5', '6', 0};
    for (int i = 0; id && i < 7; i++) if (id[i] != want[i]) return (LONG)0xC00000BBL;   /* STATUS_NOT_SUPPORTED: SHA-256 only */
    if (!id) return (LONG)0xC000000DL;
    *alg = (void *)(uintptr_t)1;
    return 0;
}
LONG WINAPI BCryptHash(void *alg, UCHAR *secret, ULONG secret_len, UCHAR *in, ULONG in_len, UCHAR *out, ULONG out_len) {
    if (alg != (void *)(uintptr_t)1 || secret || secret_len || out_len != 32) return (LONG)0xC000000DL;   /* STATUS_INVALID_PARAMETER */
    sha256(in, in_len, out);
    return 0;
}

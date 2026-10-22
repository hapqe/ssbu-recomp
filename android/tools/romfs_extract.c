/* The RomFS of a Switch title with an update applied, on the device itself: a C port of tools/extract/romfs_extract.py for
 * arm64 (the ARMv8 AES instructions), so the game folder's romfs/ is made on the phone from the packages already on it
 * instead of copying ~15 GB from the PC. Also the update's ExeFS converted as scripts/setup.py step_convert does
 * (tools/extract/nso2elf.py), and what a package holds, so the launcher can set up the game folder with no PC.
 *
 *     romfs_extract <base.nsp|.xci> <update.nsp|.xci> <output dir> <prod.keys> [title.keys] [--list]
 *     romfs_extract --exefs <update.nsp|.xci> <binaries dir> <prod.keys> [title.keys]
 *         every ExeFS file of the package's Program NCA, and main.elf / sdk.elf as nso2elf.py writes them;
 *         prints "game_version 13.0.x" for a build the port knows (exit 3 for another one)
 *     romfs_extract --info <package> <prod.keys> [title.keys]
 *         one line per NCA: "nca <title id> <content type> <size> <name>" (0 Program, 1 Meta, 2 Control, 4 Data, 5 PublicData)
 *
 * Built by android/build_apk.sh (NDK clang) into the APK as libswitchimport.so, which LauncherActivity runs; also usable
 * with adb shell. Same reading as the Python tool: the Program NCA of each package (largest NCA whose header says Program),
 * its key from the ticket / title.keys or the key area, the update's relocation (BKTR) table over the base RomFS, then
 * every file of the RomFS. Progress goes to stderr as "progress <bytes done> <bytes in all>" lines. */
#include <arm_neon.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void die(const char *fmt, const char *a) { fprintf(stderr, "romfs_extract: "); fprintf(stderr, fmt, a); fprintf(stderr, "\n"); exit(1); }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* ---------------------------------------------------------------- AES-128 (ARMv8 crypto extension) */
typedef struct { uint8x16_t e[11], d[11]; } Aes;
static uint32_t sub_word(uint32_t w) {        /* SubBytes of one word: AESE with a zero key on the word in every column */
    uint8x16_t x = vreinterpretq_u8_u32(vdupq_n_u32(w));
    return vgetq_lane_u32(vreinterpretq_u32_u8(vaeseq_u8(x, vdupq_n_u8(0))), 0);
}
static void aes_init(Aes *a, const uint8_t key[16]) {
    uint32_t w[44];
    static const uint8_t rcon[10] = {1, 2, 4, 8, 16, 32, 64, 128, 0x1b, 0x36};
    memcpy(w, key, 16);
    for (int i = 4; i < 44; i++) {
        uint32_t t = w[i - 1];
        if (i % 4 == 0) t = sub_word((t >> 8) | (t << 24)) ^ rcon[i / 4 - 1];
        w[i] = w[i - 4] ^ t;
    }
    for (int r = 0; r < 11; r++) a->e[r] = vld1q_u8((const uint8_t *)&w[r * 4]);
    a->d[0] = a->e[10];
    for (int r = 1; r < 10; r++) a->d[r] = vaesimcq_u8(a->e[10 - r]);
    a->d[10] = a->e[0];
}
static inline uint8x16_t aes_enc(const Aes *a, uint8x16_t b) {
    for (int r = 0; r < 9; r++) b = vaesmcq_u8(vaeseq_u8(b, a->e[r]));
    return veorq_u8(vaeseq_u8(b, a->e[9]), a->e[10]);
}
static inline uint8x16_t aes_dec(const Aes *a, uint8x16_t b) {
    for (int r = 0; r < 9; r++) b = vaesimcq_u8(vaesdq_u8(b, a->d[r]));
    return veorq_u8(vaesdq_u8(b, a->d[9]), a->d[10]);
}
static void ecb_dec(const uint8_t key[16], const uint8_t *in, uint8_t *out, size_t n) {
    Aes a; aes_init(&a, key);
    for (size_t i = 0; i < n; i += 16) vst1q_u8(out + i, aes_dec(&a, vld1q_u8(in + i)));
}
/* CTR with a 128-bit big-endian counter, as pycryptodome's MODE_CTR(nonce=b'', initial_value=...) */
static void ctr_xor(const Aes *a, uint64_t hi, uint64_t lo, uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i += 16) {
        uint8_t c[16];
        for (int k = 0; k < 8; k++) { c[k] = hi >> (56 - 8 * k); c[8 + k] = lo >> (56 - 8 * k); }
        vst1q_u8(buf + i, veorq_u8(vld1q_u8(buf + i), aes_enc(a, vld1q_u8(c))));
        if (!++lo) hi++;
    }
}
/* Nintendo's XTS: the sector number big-endian in the tweak */
static void xts_dec(const uint8_t key[32], uint8_t *data, size_t n, uint64_t sector) {
    Aes k1, k2; aes_init(&k1, key); aes_init(&k2, key + 16);
    for (size_t s = 0; s < n / 0x200; s++) {
        uint8_t t[16] = {0};
        for (int k = 0; k < 8; k++) t[15 - k] = (sector + s) >> (8 * k);
        vst1q_u8(t, aes_enc(&k2, vld1q_u8(t)));
        for (int b = 0; b < 0x200; b += 16) {
            uint8_t *p = data + s * 0x200 + b;
            uint8x16_t tw = vld1q_u8(t);
            vst1q_u8(p, veorq_u8(aes_dec(&k1, veorq_u8(vld1q_u8(p), tw)), tw));
            int carry = t[15] >> 7;
            for (int k = 15; k > 0; k--) t[k] = (t[k] << 1) | (t[k - 1] >> 7);
            t[0] = (t[0] << 1) ^ (carry ? 0x87 : 0);
        }
    }
}

/* ---------------------------------------------------------------- keys */
typedef struct { char name[64]; uint8_t v[32]; int n; } Key;
static Key g_keys[512]; static int g_nkeys;
static void load_keys(const char *path, int required) {
    FILE *f = fopen(path, "r");
    if (!f) { if (required) die("cannot open %s", path); return; }
    char line[512];
    while (fgets(line, sizeof line, f) && g_nkeys < 512) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        Key *k = &g_keys[g_nkeys];
        int j = 0;
        for (char *s = line; *s; s++) if (*s != ' ' && *s != '\t' && j < 63) k->name[j++] = (*s >= 'A' && *s <= 'Z') ? *s + 32 : *s;
        k->name[j] = 0;
        k->n = 0;
        for (char *s = eq + 1; *s && k->n < 32;) {
            while (*s == ' ' || *s == '\t') s++;
            unsigned x;
            if (sscanf(s, "%2x", &x) != 1) break;
            k->v[k->n++] = x; s += 2;
        }
        g_nkeys++;
    }
    fclose(f);
}
static const uint8_t *key(const char *name, int need) {
    for (int i = 0; i < g_nkeys; i++) if (!strcmp(g_keys[i].name, name)) return g_keys[i].v;
    if (need) die("key %s not found in the keys files", name);
    return NULL;
}

/* ---------------------------------------------------------------- packages (NSP = PFS0, XCI = HFS0 secure partition) */
typedef struct { char name[128]; uint64_t off, size; } Entry;
typedef struct { int fd; Entry e[512]; int n; } Pkg;
static void pread_all(int fd, void *buf, size_t n, uint64_t off) {
    uint8_t *p = buf;
    while (n) {
        ssize_t r = pread(fd, p, n, off);
        if (r <= 0) { fprintf(stderr, "romfs_extract: read of %zu bytes at %#llx failed (%s)\n", n, (unsigned long long)off, r ? strerror(errno) : "end of file"); exit(1); }
        p += r; n -= r; off += r;
    }
}
static void read_fs(Pkg *k, uint64_t off, int hfs) {
    uint8_t h[16];
    pread_all(k->fd, h, 16, off);
    if (memcmp(h, hfs ? "HFS0" : "PFS0", 4)) die("%s header not found", hfs ? "HFS0" : "PFS0");
    uint32_t count = rd32(h + 4), ssize = rd32(h + 8), esz = hfs ? 0x40 : 24;
    uint8_t *t = malloc(count * esz + ssize);
    pread_all(k->fd, t, count * esz + ssize, off + 16);
    uint64_t base = off + 16 + count * esz + ssize;
    k->n = 0;
    for (uint32_t i = 0; i < count && i < 512; i++) {
        const uint8_t *e = t + i * esz;
        snprintf(k->e[i].name, sizeof k->e[i].name, "%s", (char *)t + count * esz + rd32(e + 16));
        k->e[i].off = base + rd64(e); k->e[i].size = rd64(e + 8);
        k->n++;
    }
    free(t);
}
static void open_pkg(Pkg *k, const char *path) {
    if ((k->fd = open(path, O_RDONLY)) < 0) die("cannot open %s", path);
    uint8_t h[0x138];
    pread_all(k->fd, h, sizeof h, 0);
    if (!memcmp(h + 0x100, "HEAD", 4)) {
        read_fs(k, rd64(h + 0x130), 1);
        for (int i = 0; i < k->n; i++) if (!strcmp(k->e[i].name, "secure")) { read_fs(k, k->e[i].off, 1); return; }
        die("%s: the XCI has no secure partition", path);
    }
    read_fs(k, 0, 0);
}
static int ends(const char *s, const char *x) { size_t a = strlen(s), b = strlen(x); return a >= b && !strcmp(s + a - b, x); }

/* ---------------------------------------------------------------- NCA */
typedef struct { uint64_t off, size; uint8_t fs[0x200]; uint64_t ctr_hi; } Sec;
typedef struct { Pkg *pkg; Entry *e; uint8_t h[0xC00]; Aes key; Sec sec[4]; int nsec; } Nca;
static void nca_keys(Nca *n, Pkg *k);
static void open_nca(Nca *n, Pkg *k) {
    const uint8_t *hk = key("header_key", 1);
    n->pkg = k; n->e = NULL;
    for (int i = 0; i < k->n; i++) {                /* the largest NCA whose header says Program */
        if (!ends(k->e[i].name, ".nca") || (n->e && k->e[i].size <= n->e->size)) continue;
        uint8_t h[0xC00];
        pread_all(k->fd, h, sizeof h, k->e[i].off);
        xts_dec(hk, h, sizeof h, 0);
        if (!memcmp(h + 0x200, "NCA3", 4) && h[0x205] == 0) { n->e = &k->e[i]; memcpy(n->h, h, sizeof h); }
    }
    if (!n->e) die("%s", "no Program NCA in the package (wrong header_key?)");
    nca_keys(n, k);
}
static void nca_keys(Nca *n, Pkg *k) {
    const uint8_t *h = n->h;
    int gen = (h[0x206] > h[0x220] ? h[0x206] : h[0x220]) - 1;
    if (gen < 0) gen = 0;
    char name[64];
    uint8_t tkey[16], plain[0x40];
    static const uint8_t zero[16];
    if (memcmp(h + 0x230, zero, 16)) {              /* rights id: the ticket's title key, else title.keys */
        const uint8_t *src = NULL;
        uint8_t tik[0x2C0];
        for (int i = 0; i < k->n && !src; i++)
            if (ends(k->e[i].name, ".tik") && k->e[i].size >= sizeof tik) {
                pread_all(k->fd, tik, sizeof tik, k->e[i].off);
                if (!memcmp(tik + 0x2A0, h + 0x230, 16)) src = tik + 0x180;
            }
        if (!src) {
            char hex[33];
            for (int i = 0; i < 16; i++) sprintf(hex + 2 * i, "%02x", h[0x230 + i]);
            src = key(hex, 0);
            if (!src) die("no ticket or title key for rights id %s", hex);
        }
        snprintf(name, sizeof name, "titlekek_%02x", gen);
        ecb_dec(key(name, 1), src, tkey, 16);
    } else {
        snprintf(name, sizeof name, "key_area_key_application_%02x", gen);
        ecb_dec(key(name, 1), h + 0x300, plain, 0x40);
        memcpy(tkey, plain + 0x20, 16);
    }
    aes_init(&n->key, tkey);
    n->nsec = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t start = rd32(h + 0x240 + i * 16), end = rd32(h + 0x244 + i * 16);
        if (!end) continue;
        Sec *s = &n->sec[n->nsec++];
        s->off = (uint64_t)start * 0x200; s->size = (uint64_t)(end - start) * 0x200;
        memcpy(s->fs, h + 0x400 + i * 0x200, 0x200);
        s->ctr_hi = 0;
        for (int b = 0; b < 8; b++) s->ctr_hi |= (uint64_t)s->fs[0x140 + b] << (8 * b);     /* fs[0x140:0x148] reversed, big-endian */
    }
}
static Sec *section(Nca *n, int type) {
    for (int i = 0; i < n->nsec; i++) if (n->sec[i].fs[2] == type) return &n->sec[i];
    die("%s", "section not found");
    return NULL;
}
/* decrypted read of size bytes at section offset off; ctr_val >= 0 replaces the counter's low 32 bits of the upper half */
static void nca_read(Nca *n, Sec *s, uint64_t off, size_t size, int64_t ctr_val, uint8_t *out) {
    static __thread uint8_t *tmp; static __thread size_t cap;
    if (!size) return;
    uint64_t abs = s->off + off, al = abs & ~15ULL;
    size_t len = (abs - al + size + 15) & ~(size_t)15;
    if (len > cap) { free(tmp); cap = len; tmp = malloc(cap); }
    pread_all(n->pkg->fd, tmp, len, n->e->off + al);
    uint64_t hi = s->ctr_hi;
    if (ctr_val >= 0) hi = (hi & 0xFFFFFFFF00000000ULL) | (uint32_t)ctr_val;
    ctr_xor(&n->key, hi, al >> 4, tmp, len);
    memcpy(out, tmp + (abs - al), size);
}

/* ---------------------------------------------------------------- the update's BKTR over the base RomFS */
typedef struct { uint64_t virt, phys; uint32_t patch; } Rel;
typedef struct { uint64_t off; int64_t ctr; } Sub;
typedef struct { Nca *base, *upd; Sec *bs, *us; Rel *rel; int nrel; Sub *sub; int nsub; uint64_t total, level6; } Romfs;
static void romfs_init(Romfs *r, Nca *base, Nca *upd) {
    r->base = base; r->upd = upd; r->bs = section(base, 0); r->us = section(upd, 0);
    const uint8_t *fs = r->us->fs;
    if (fs[4] != 4) die("%s", "the update package has no patch RomFS: give the update second");
    if (memcmp(fs + 8, "IVFC", 4)) die("%s", "no IVFC header");
    r->level6 = rd64(fs + 0x8 + 0x10 + 5 * 0x18);
    uint64_t r_off = rd64(fs + 0x100), r_size = rd64(fs + 0x108), s_off = rd64(fs + 0x120), s_size = rd64(fs + 0x128);
    if (memcmp(fs + 0x110, "BKTR", 4) || memcmp(fs + 0x130, "BKTR", 4)) die("%s", "no BKTR tables");
    uint8_t *reloc = malloc(r_size), *subs = malloc(s_size);
    nca_read(upd, r->us, r_off, r_size, -1, reloc);
    nca_read(upd, r->us, s_off, s_size, -1, subs);
    r->total = rd64(reloc + 8);
    r->nrel = 0; r->rel = NULL;
    for (uint32_t b = 0; b < rd32(reloc + 4); b++) {
        const uint8_t *bk = reloc + 0x4000 * (b + 1);
        uint32_t cnt = rd32(bk + 4);
        r->rel = realloc(r->rel, (r->nrel + cnt) * sizeof(Rel));
        for (uint32_t e = 0; e < cnt; e++) {
            const uint8_t *p = bk + 0x10 + e * 0x14;
            r->rel[r->nrel++] = (Rel){rd64(p), rd64(p + 8), rd32(p + 16)};
        }
    }
    r->nsub = 0; r->sub = NULL;
    for (uint32_t b = 0; b < rd32(subs + 4); b++) {
        const uint8_t *bk = subs + 0x4000 * (b + 1);
        uint32_t cnt = rd32(bk + 4);
        r->sub = realloc(r->sub, (r->nsub + cnt + 1) * sizeof(Sub));
        for (uint32_t e = 0; e < cnt; e++) {
            const uint8_t *p = bk + 0x10 + e * 0x10;
            r->sub[r->nsub++] = (Sub){rd64(p), rd32(p + 12)};
        }
    }
    r->sub = realloc(r->sub, (r->nsub + 1) * sizeof(Sub));
    r->sub[r->nsub++] = (Sub){r_off, -1};           /* the tables themselves use the header counter */
    free(reloc); free(subs);
    printf("virtual RomFS section: %.2f GB, %d relocation entries, %d subsections\n", r->total / 1073741824.0, r->nrel, r->nsub);
}
static int find_rel(Romfs *r, uint64_t off) { int lo = 0, hi = r->nrel - 1; while (lo < hi) { int m = (lo + hi + 1) / 2; if (r->rel[m].virt <= off) lo = m; else hi = m - 1; } return lo; }
static int find_sub(Romfs *r, uint64_t off) { int lo = 0, hi = r->nsub - 1; while (lo < hi) { int m = (lo + hi + 1) / 2; if (r->sub[m].off <= off) lo = m; else hi = m - 1; } return lo; }
static void read_patch(Romfs *r, uint64_t off, uint64_t size, uint8_t *out) {
    while (size) {
        int i = find_sub(r, off);
        uint64_t end = i + 1 < r->nsub ? r->sub[i + 1].off : off + size, n = end - off < size ? end - off : size;
        nca_read(r->upd, r->us, off, n, r->sub[i].ctr, out);
        off += n; size -= n; out += n;
    }
}
static void romfs_read(Romfs *r, uint64_t off, uint64_t size, uint8_t *out) {
    while (size) {
        int i = find_rel(r, off);
        Rel *e = &r->rel[i];
        uint64_t end = i + 1 < r->nrel ? r->rel[i + 1].virt : r->total, n = end - off < size ? end - off : size;
        if (e->patch) read_patch(r, e->phys + off - e->virt, n, out);
        else nca_read(r->base, r->bs, e->phys + off - e->virt, n, -1, out);
        off += n; size -= n; out += n;
    }
}

/* ---------------------------------------------------------------- RomFS walk + extraction */
static void mkdirs(char *path) {
    for (char *p = path + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(path, 0775); *p = '/'; }
}
static uint8_t *g_dirs, *g_files; static uint64_t g_data; static Romfs *g_r; static const char *g_out; static int g_list;
static uint64_t g_done, g_count, g_total; static time_t g_t0;
static void sum_sizes(uint32_t d);
static void extract_file(const char *path, uint64_t off, uint64_t size) {
    g_count++;
    if (g_list) { printf("  %14llu  %s\n", (unsigned long long)size, path); return; }
    char target[4096];
    snprintf(target, sizeof target, "%s%s", g_out, path);
    mkdirs(target);
    struct stat st;
    if (!stat(target, &st) && (uint64_t)st.st_size == size) { g_done += size; return; }      /* resumed run: whole files stay */
    int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) die("cannot write %s", target);
    static uint8_t *buf;
    if (!buf) buf = malloc(32 << 20);
    for (uint64_t pos = 0; pos < size;) {
        uint64_t n = size - pos < (32 << 20) ? size - pos : (32 << 20);
        romfs_read(g_r, off + pos, n, buf);
        for (uint64_t w = 0; w < n;) { ssize_t k = write(fd, buf + w, n - w); if (k <= 0) die("write failed: %s", strerror(errno)); w += k; }
        pos += n; g_done += n;
        static time_t last;
        if (time(NULL) != last) { last = time(NULL); fprintf(stderr, "progress %llu %llu\n", (unsigned long long)g_done, (unsigned long long)g_total); }
    }
    close(fd);
}
static void walk(uint32_t d, const char *parent) {
    while (d != 0xFFFFFFFF) {
        const uint8_t *e = g_dirs + d;
        char here[4096];
        uint32_t name_len = rd32(e + 20);
        if (name_len) snprintf(here, sizeof here, "%s/%.*s", parent, (int)name_len, e + 24);
        else snprintf(here, sizeof here, "%s", parent);
        for (uint32_t f = rd32(e + 12); f != 0xFFFFFFFF;) {
            const uint8_t *fe = g_files + f;
            char path[4096];
            snprintf(path, sizeof path, "%s/%.*s", here, (int)rd32(fe + 28), fe + 32);
            extract_file(path, g_r->level6 + g_data + rd64(fe + 8), rd64(fe + 16));
            f = rd32(fe + 4);
        }
        if (rd32(e + 8) != 0xFFFFFFFF) walk(rd32(e + 8), here);
        d = rd32(e + 4);                            /* sibling */
    }
}

/* ---------------------------------------------------------------- the size of every file (progress in all) */
static void sum_sizes(uint32_t d) {
    while (d != 0xFFFFFFFF) {
        const uint8_t *e = g_dirs + d;
        for (uint32_t f = rd32(e + 12); f != 0xFFFFFFFF; f = rd32(g_files + f + 4)) g_total += rd64(g_files + f + 16);
        if (rd32(e + 8) != 0xFFFFFFFF) sum_sizes(rd32(e + 8));
        d = rd32(e + 4);
    }
}

/* ---------------------------------------------------------------- SHA-256 (the NSO segment hashes) */
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))
static void sha_block(uint32_t st[8], const uint8_t *p) {
    uint32_t w[64], a[8];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(a, st, sizeof a);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + K256[i] + w[i];
        uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        memmove(a + 1, a, 7 * sizeof a[0]); a[4] += t1; a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) st[i] += a[i];
}
static void sha256(const uint8_t *d, size_t n, uint8_t out[32]) {
    uint32_t st[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha_block(st, d + i);
    uint8_t t[128] = {0};
    size_t r = n - i, tl = r < 56 ? 64 : 128;
    memcpy(t, d + i, r); t[r] = 0x80;
    for (int k = 0; k < 8; k++) t[tl - 1 - k] = (uint8_t)((uint64_t)n * 8 >> (8 * k));
    sha_block(st, t); if (tl == 128) sha_block(st, t + 64);
    for (int k = 0; k < 8; k++) { out[4 * k] = st[k] >> 24; out[4 * k + 1] = st[k] >> 16; out[4 * k + 2] = st[k] >> 8; out[4 * k + 3] = st[k]; }
}

/* ---------------------------------------------------------------- LZ4 block (the NSO segments) */
static int lz4_dec(const uint8_t *s, size_t sn, uint8_t *d, size_t dn) {
    size_t i = 0, o = 0;
    while (i < sn) {
        unsigned tok = s[i++];
        size_t lit = tok >> 4;
        if (lit == 15) { unsigned b; do { if (i >= sn) return -1; b = s[i++]; lit += b; } while (b == 255); }
        if (i + lit > sn || o + lit > dn) return -1;
        memcpy(d + o, s + i, lit); i += lit; o += lit;
        if (i >= sn) break;                             /* the last sequence has literals only */
        if (i + 2 > sn) return -1;
        size_t off = s[i] | s[i + 1] << 8; i += 2;
        size_t ml = tok & 15;
        if (ml == 15) { unsigned b; do { if (i >= sn) return -1; b = s[i++]; ml += b; } while (b == 255); }
        ml += 4;
        if (!off || off > o || o + ml > dn) return -1;
        for (size_t k = 0; k < ml; k++, o++) d[o] = d[o - off];
    }
    return (int)(o == dn ? 0 : -1);
}

/* ---------------------------------------------------------------- NSO -> ELF, byte for byte as tools/extract/nso2elf.py */
typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void bput(Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static void bzero_(Buf *b, size_t n) { while (n) { static const uint8_t z[256]; size_t k = n < 256 ? n : 256; bput(b, z, k); n -= k; } }
static void put16(Buf *b, uint16_t v) { bput(b, &v, 2); }
static void put32(Buf *b, uint32_t v) { bput(b, &v, 4); }
static void put64(Buf *b, uint64_t v) { bput(b, &v, 8); }
/* returns the size of segment 0 (the code), 0 on a failure; *mismatch = a segment's hash is wrong */
static uint32_t nso2elf(const uint8_t *d, size_t dn, const char *dst, int *mismatch) {
    if (dn < 0x100 || memcmp(d, "NSO0", 4)) return 0;
    uint32_t flags = rd32(d + 12);
    uint8_t *seg[3]; uint32_t vaddr[3], size[3];
    *mismatch = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t foff = rd32(d + 0x10 + 0x10 * i), moff = rd32(d + 0x14 + 0x10 * i), sz = rd32(d + 0x18 + 0x10 * i), csz = rd32(d + 0x60 + 4 * i);
        if ((uint64_t)foff + csz > dn) return 0;
        seg[i] = malloc(sz ? sz : 1); vaddr[i] = moff; size[i] = sz;
        if (flags & (1u << i)) { if (lz4_dec(d + foff, csz, seg[i], sz)) return 0; }
        else { if (csz != sz) return 0; memcpy(seg[i], d + foff, sz); }
        uint8_t h[32];
        sha256(seg[i], sz, h);
        if (memcmp(h, d + 0xA0 + 0x20 * i, 32)) *mismatch = 1;
        printf("seg %d vaddr %x size %x sha256 %s\n", i, moff, sz, memcmp(h, d + 0xA0 + 0x20 * i, 32) ? "MISMATCH" : "OK");
    }
    uint32_t bss = rd32(d + 0x3c), tv = vaddr[0], rv = vaddr[1], dv = vaddr[2];
    size_t memn = (size_t)dv + size[2];
    uint8_t *mem = calloc(1, memn);
    for (int i = 0; i < 3; i++) memcpy(mem + vaddr[i], seg[i], size[i]);
    uint32_t mod = rd32(seg[0] + 4);
    if (mod + 16 > memn || memcmp(mem + mod, "MOD0", 4)) return 0;
    int32_t dyn_off, bss_s, bss_e;
    memcpy(&dyn_off, mem + mod + 4, 4); memcpy(&bss_s, mem + mod + 8, 4); memcpy(&bss_e, mem + mod + 12, 4);
    uint64_t dyn = (uint64_t)mod + (int64_t)dyn_off, tag[32] = {0}; int have[32] = {0}; uint64_t n = 0;
    for (;;) {
        if (dyn + n * 16 + 16 > memn) return 0;
        int64_t t; uint64_t v;
        memcpy(&t, mem + dyn + n * 16, 8); memcpy(&v, mem + dyn + n * 16 + 8, 8); n++;
        if (t == 0) break;
        if (t > 0 && t < 32 && !have[t]) { have[t] = 1; tag[t] = v; }
    }
    uint64_t dynsz = n * 16, symtab = tag[6], strtab = tag[5], strsz = tag[10];
    uint64_t nsym = have[4] ? rd32(mem + tag[4] + 4) : (strtab - symtab) / 24;
    const uint64_t OFF = 0x1000;
    Buf names = {0}, secs = {0};
    bput(&names, "", 1);
    bzero_(&secs, 64);
    int nsecs = 1;
#define SEC(nm, typ, fl, addr, off, sz, link, info, align, ent) do { uint32_t ni_ = (uint32_t)names.n; bput(&names, nm, strlen(nm) + 1); \
        put32(&secs, ni_); put32(&secs, typ); put64(&secs, fl); put64(&secs, addr); put64(&secs, off); put64(&secs, sz); put32(&secs, link); put32(&secs, info); put64(&secs, align); put64(&secs, ent); nsecs++; } while (0)
    if (!(rv <= symtab && symtab + nsym * 24 <= strtab)) return 0;
    SEC(".text", 1, 6, tv, OFF + tv, size[0], 0, 0, 16, 0);
    SEC(".rodata", 1, 2, rv, OFF + rv, symtab - rv, 0, 0, 16, 0);
    SEC(".dynsym", 11, 2, symtab, OFF + symtab, nsym * 24, 4, 1, 8, 24);
    SEC(".dynstr", 3, 2, strtab, OFF + strtab, strsz, 0, 0, 1, 0);
    uint64_t r1 = strtab + strsz;
    SEC(".rodata.1", 1, 2, r1, OFF + r1, rv + size[1] - r1, 0, 0, 1, 0);
    SEC(".data", 1, 3, dv, OFF + dv, dyn - dv, 0, 0, 16, 0);
    SEC(".dynamic", 6, 3, dyn, OFF + dyn, dynsz, 4, 0, 8, 16);
    uint64_t d1 = dyn + dynsz;
    SEC(".data.1", 1, 3, d1, OFF + d1, dv + size[2] - d1, 0, 0, 1, 0);
    SEC(".bss", 8, 3, (uint64_t)(int64_t)bss_s, OFF + dv + size[2], (uint64_t)(int64_t)(bss_e - bss_s), 0, 0, 16, 0);
    int shstr_i = nsecs;
    uint64_t shstr_off = OFF + memn;
    SEC(".shstrtab", 3, 0, 0, shstr_off, 0, 0, 0, 1, 0);
    uint64_t nl = names.n; memcpy(secs.p + secs.n - 64 + 32, &nl, 8);         /* the size of .shstrtab, now that it is complete */
    uint64_t shoff = (shstr_off + names.n + 7) & ~7ULL;
    Buf o = {0};
    static const uint8_t ident[16] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    bput(&o, ident, 16);
    put16(&o, 3); put16(&o, 183); put32(&o, 1); put64(&o, 0); put64(&o, 0x40); put64(&o, shoff); put32(&o, 0);
    put16(&o, 0x40); put16(&o, 0x38); put16(&o, 4); put16(&o, 0x40); put16(&o, (uint16_t)nsecs); put16(&o, (uint16_t)shstr_i);
#define PH(t, fl, v, fsz, msz, al) do { put32(&o, t); put32(&o, fl); put64(&o, OFF + (v)); put64(&o, v); put64(&o, v); put64(&o, fsz); put64(&o, msz); put64(&o, al); } while (0)
    uint64_t dmem = size[2]; if ((uint64_t)(bss_e - (int64_t)dv) > dmem) dmem = (uint64_t)(bss_e - (int64_t)dv); if (size[2] + (uint64_t)bss > dmem) dmem = size[2] + (uint64_t)bss;
    PH(1, 5, tv, size[0], size[0], 0x1000);
    PH(1, 4, rv, size[1], size[1], 0x1000);
    PH(1, 6, dv, size[2], dmem, 0x1000);
    PH(2, 6, dyn, dynsz, dynsz, 8);
    bzero_(&o, OFF - o.n);
    bput(&o, mem, memn);
    bput(&o, names.p, names.n);
    bzero_(&o, shoff - o.n);
    bput(&o, secs.p, secs.n);
    FILE *f = fopen(dst, "wb");
    if (!f || fwrite(o.p, 1, o.n, f) != o.n || fclose(f)) die("cannot write %s", dst);
    printf("wrote %s %zu\n", dst, o.n);
    uint32_t code = size[0];
    for (int i = 0; i < 3; i++) free(seg[i]);
    free(mem); free(names.p); free(secs.p); free(o.p);
    return code;
}

/* ---------------------------------------------------------------- --exefs, --info */
static int exefs_main(int argc, char **argv) {
    load_keys(argv[4], 1);
    if (argc > 5) load_keys(argv[5], 0);
    static Pkg up; static Nca un;
    open_pkg(&up, argv[2]); open_nca(&un, &up);
    printf("program NCA %s\n", un.e->name);
    Sec *s = NULL;
    for (int i = 0; i < un.nsec; i++) if (un.sec[i].fs[2] == 1 && un.sec[i].fs[3] == 2) { s = &un.sec[i]; break; }      /* PFS0 with a SHA-256 hash table */
    if (!s) die("%s", "the package's Program NCA has no ExeFS");
    uint64_t poff = rd64(s->fs + 0x40), psize = rd64(s->fs + 0x48);      /* hash data at 0x8: master hash, block size, layer count, then (offset, size) of the hash table and of the PFS0 */
    uint8_t *p = malloc(psize);
    nca_read(&un, s, poff, psize, -1, p);
    if (memcmp(p, "PFS0", 4)) die("%s", "ExeFS header not found: wrong keys?");
    uint32_t count = rd32(p + 4), ssize = rd32(p + 8);
    const uint8_t *strs = p + 16 + count * 24, *data = strs + ssize;
    const char *out = argv[3];
    mkdir(out, 0775);
    uint32_t main_code = 0, sdk_code = 0; int bad = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = p + 16 + i * 24;
        const char *name = (const char *)strs + rd32(e + 16);
        uint64_t off = rd64(e), sz = rd64(e + 8);
        if (data + off + sz > p + psize || strchr(name, '/')) die("bad ExeFS entry %s", name);
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", out, name);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(data + off, 1, sz, f) != sz || fclose(f)) die("cannot write %s", path);
        printf("%s %llu\n", name, (unsigned long long)sz);
        if (!strcmp(name, "main") || !strcmp(name, "sdk")) {
            int mm;
            snprintf(path, sizeof path, "%s/%s.elf", out, name);
            uint32_t c = nso2elf(data + off, sz, path, &mm);
            if (!c) die("%s: not an NSO this tool can convert", name);
            bad |= mm;
            if (!strcmp(name, "main")) main_code = c; else sdk_code = c;
        }
    }
    const char *ver = bad ? NULL : main_code == 0x39c7e90 ? "13.0.4" : main_code == 0x39c8440 ? "13.0.5" : NULL;   /* scripts/setup.py GAME_BUILDS */
    if (!ver || sdk_code != 0x540950) {
        printf("unknown build: main code %#x, sdk code %#x%s (the port runs 13.0.4 and 13.0.5)\n", main_code, sdk_code, bad ? ", a segment hash mismatch" : "");
        return 3;
    }
    printf("game_version %s\n", ver);
    return 0;
}
static int info_main(int argc, char **argv) {
    load_keys(argv[3], 1);
    if (argc > 4) load_keys(argv[4], 0);
    const uint8_t *hk = key("header_key", 1);
    static Pkg k;
    open_pkg(&k, argv[2]);
    for (int i = 0; i < k.n; i++) {
        if (ends(k.e[i].name, ".tik")) printf("tik %.16s\n", k.e[i].name);
        if (!ends(k.e[i].name, ".nca") || k.e[i].size < 0xC00) continue;
        uint8_t h[0xC00];
        pread_all(k.fd, h, sizeof h, k.e[i].off);
        xts_dec(hk, h, sizeof h, 0);
        if (memcmp(h + 0x200, "NCA3", 4)) { printf("nca ? ? %llu %s\n", (unsigned long long)k.e[i].size, k.e[i].name); continue; }
        printf("nca %016llx %d %llu %s\n", (unsigned long long)rd64(h + 0x210), h[0x205], (unsigned long long)k.e[i].size, k.e[i].name);
    }
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc >= 5 && !strcmp(argv[1], "--exefs")) return exefs_main(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "--info")) return info_main(argc, argv);
    if (argc < 5) { fprintf(stderr, "usage: romfs_extract <base.nsp|.xci> <update.nsp|.xci> <output dir> <prod.keys> [title.keys] [--list]\n"
                                    "       romfs_extract --exefs <update.nsp|.xci> <binaries dir> <prod.keys> [title.keys]\n"
                                    "       romfs_extract --info <package> <prod.keys> [title.keys]\n"); return 2; }
    load_keys(argv[4], 1);
    if (argc > 5 && strcmp(argv[5], "--list")) load_keys(argv[5], 0);
    g_list = !strcmp(argv[argc - 1], "--list");
    static Pkg bp, up; static Nca bn, un; static Romfs r;
    open_pkg(&bp, argv[1]); open_pkg(&up, argv[2]);
    open_nca(&bn, &bp); open_nca(&un, &up);
    printf("base   program NCA %s\nupdate program NCA %s\n", bn.e->name, un.e->name);
    romfs_init(&r, &bn, &un);
    g_r = &r;
    uint8_t hdr[0x50];
    romfs_read(&r, r.level6, 0x50, hdr);
    if (rd64(hdr) != 0x50) die("%s", "RomFS header not found: wrong keys or wrong base/update pair");
    uint64_t dir_off = rd64(hdr + 24), dir_size = rd64(hdr + 32), file_off = rd64(hdr + 56), file_size = rd64(hdr + 64);
    g_data = rd64(hdr + 72);
    g_dirs = malloc(dir_size); g_files = malloc(file_size);
    romfs_read(&r, r.level6 + dir_off, dir_size, g_dirs);
    romfs_read(&r, r.level6 + file_off, file_size, g_files);
    g_out = argv[3];
    if (!g_list) mkdir(g_out, 0775);
    g_t0 = time(NULL);
    if (!g_list) sum_sizes(0);
    walk(0, "");
    printf("\n%llu files, %.2f GB%s\n", (unsigned long long)g_count, g_done / 1073741824.0, g_list ? "" : " written");
    return 0;
}

/* The RomFS of a Switch title with an update applied, on the device itself: a C port of tools/extract/romfs_extract.py for
 * arm64 (the ARMv8 AES instructions), so the game folder's romfs/ is made on the phone from the packages already on it
 * instead of copying ~15 GB from the PC.
 *
 *     romfs_extract <base.nsp|.xci> <update.nsp|.xci> <output dir> <prod.keys> [title.keys] [--list]
 *
 * Built by android/tools/build_romfs_extract.sh (NDK clang, static) and run with adb shell. Same reading as the Python tool:
 * the Program NCA of each package (largest NCA whose header says Program), its key from the ticket / title.keys or the key
 * area, the update's relocation (BKTR) table over the base RomFS, then every file of the RomFS. */
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
static uint64_t g_done, g_count; static time_t g_t0;
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
        double secs = difftime(time(NULL), g_t0);
        fprintf(stderr, "\r%.2f GB  %.0f MB/s   ", g_done / 1073741824.0, g_done / 1048576.0 / (secs > 0 ? secs : 1));
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

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: romfs_extract <base.nsp|.xci> <update.nsp|.xci> <output dir> <prod.keys> [title.keys] [--list]\n"); return 2; }
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
    walk(0, "");
    printf("\n%llu files, %.2f GB%s\n", (unsigned long long)g_count, g_done / 1073741824.0, g_list ? "" : " written");
    return 0;
}

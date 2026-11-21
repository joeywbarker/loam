/* LOAM cloud core: the same physics as the OS (../loam.h), as a shared library,
 * on a bigger world.  Snapshots are atomic (write temp, fsync, rename). */
#define LW 256
#define LH 256
#define MAXW 4096
#define DECAY_ROWS 43          /* full cooling sweep every ~6 ticks, as in the OS */
#define SUN 32                 /* rain scales with area (1 per ~2000 cells)       */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../loam.h"

void hw_beep(int a) { (void)a; }

static World G;
static uint16_t cells[LW * LH];
static int ready;

#define EXPORT __attribute__((visibility("default")))

static void genesis_big(void) {
    for (int i = 0; i < LW * LH; i++) { cells[i] = BLANK; G.occ[i] = 0; }
    for (int j = 0; j < MAXW; j++) G.w[j].alive = 0;
    G.tick = 0; G.key_air = 0; G.key_ttl = 0;
    static const int INK[5] = { 12, 9, 14, 13, 11 };
    int n = 0;
    for (int ty = 0; ty < 4; ty++)
        for (int tx = 0; tx < 4; tx++) {
            int x0 = tx * 64 + 8, y0 = ty * 64 + 8, w = 30 + (tx * 7 + ty * 5) % 18, h = 12 + (tx * 3 + ty * 11) % 14;
            if (tx == 1 && ty == 1) {                 /* the plaque, near the middle of the world */
                text(&G, x0, y0 + 4, "L  O  A  M", HUMAN);
                text(&G, x0 - 6, y0 + 6, "a world that keeps running while you are away", HUMAN);
                text(&G, x0 - 6, y0 + 8, "type here. breathe life. come back later.", HUMAN);
                continue;
            }
            ring(&G, x0, y0, x0 + w, y0 + h, (tx + ty) & 1, INK[n++ % 5]);
        }
}

EXPORT int lw_init(uint32_t seed) {
    memset(&G, 0, sizeof G);
    G.cells = cells;
    G.rng = seed ? seed : 0x2545F491u;
    genesis_big();
    ready = 1;
    return 0;
}
EXPORT void lw_genesis(void) { genesis_big(); }
EXPORT void lw_tick(int n) { while (n-- > 0) world_tick(&G); }
EXPORT uint32_t lw_tickcount(void) { return G.tick; }
EXPORT void lw_stats(int *wisps, int *energy) { world_stats(&G, wisps, energy); }
EXPORT int lw_width(void) { return LW; }
EXPORT int lw_height(void) { return LH; }

/* copy a wrapped window of the world (w*h cells, row-major) */
EXPORT int lw_view(int x, int y, int w, int h, uint16_t *out) {
    if (w < 1 || h < 1 || w > LW || h > LH) return -1;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) out[j * w + i] = cells[IDX(wx(x + i), wy(y + j))];
    return 0;
}
EXPORT int lw_type(int x, int y, int ch, int ink) {
    if (ch < 32 || ch > 126) return -1;
    cput(&G, x, y, MK(ch, ink, 0)); return 0;
}
EXPORT int lw_erase(int x, int y) { cput(&G, x, y, BLANK); return 0; }
EXPORT int lw_breathe(int x, int y, int dir, int ink) { return spawn(&G, x, y, dir, ink, 120) >= 0 ? 0 : -1; }

/* ---- snapshots ---- */
#define MAGIC 0x4D4F4C57u   /* "WLOM" */
typedef struct { uint32_t magic, ver, lw, lh, maxw, tick, rng, sum; } Head;

static uint32_t sum32(const void *p, size_t n, uint32_t s) {
    const uint8_t *b = p; while (n--) s = s * 31 + *b++; return s;
}
EXPORT int lw_save(const char *path) {
    if (!ready) return -1;
    char tmp[1024];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    Head h = { MAGIC, 1, LW, LH, MAXW, G.tick, G.rng, 0 };
    h.sum = sum32(G.w, sizeof G.w, sum32(cells, sizeof cells, 0));
    FILE *f = fopen(tmp, "wb");
    if (!f) return -2;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(G.w, sizeof G.w, 1, f) == 1 &&
             fwrite(cells, sizeof cells, 1, f) == 1 && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return -3; }
    return rename(tmp, path) == 0 ? 0 : -4;
}
EXPORT int lw_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static Wisp w2[MAXW]; static uint16_t c2[LW * LH]; Head h;
    int ok = fread(&h, sizeof h, 1, f) == 1 && h.magic == MAGIC && h.ver == 1 && h.lw == LW && h.lh == LH &&
             h.maxw == MAXW && fread(w2, sizeof w2, 1, f) == 1 && fread(c2, sizeof c2, 1, f) == 1;
    fclose(f);
    if (!ok || sum32(w2, sizeof w2, sum32(c2, sizeof c2, 0)) != h.sum) return -2;
    memset(&G, 0, sizeof G);
    G.cells = cells;
    memcpy(cells, c2, sizeof cells); memcpy(G.w, w2, sizeof w2);
    G.tick = h.tick; G.rng = h.rng ? h.rng : 0x2545F491u;
    for (int j = 0; j < MAXW; j++) {
        Wisp *w = &G.w[j];
        if (!w->alive) continue;
        if (w->x < 0 || w->x >= LW || w->y < 0 || w->y >= LH || G.occ[IDX(w->x, w->y)]) { w->alive = 0; continue; }
        G.occ[IDX(w->x, w->y)] = j + 1;
    }
    ready = 1;
    return 0;
}

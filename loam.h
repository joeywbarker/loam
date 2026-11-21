/* LOAM -- the world.  Original code.
 *
 * One idea: the screen IS the memory.  A cell is a 16-bit VGA text cell:
 *
 *      bit 15      0 (unused, keeps the hardware from blinking)
 *      bits 12-14  HEAT   (0..7)   how recently life touched this cell
 *      bits  8-11  OWNER  (0..15)  colour of whoever wrote it
 *      bits  0-7   GLYPH  (a byte) data AND instruction -- same thing
 *
 * There are no processes, files, syscalls or allocator.  There are WISPS:
 * little bodies that stand on a cell, execute its glyph, and step forward.
 * A wisp spends one unit of energy per tick.  Energy comes from food ('.'),
 * and food grows back where life has recently walked.  Writing into a cell
 * owned by someone else costs extra energy (protection by price, not by ring).
 */
#ifndef LOAM_H
#define LOAM_H
#include <stdint.h>

#ifndef LW
#define LW 80
#endif
#ifndef LH
#define LH 24
#endif
#ifndef MAXW
#define MAXW 128
#endif

#define MK(g, o, h) ((uint16_t)(((g) & 0xFF) | (((o) & 15) << 8) | (((h) & 7) << 12)))
#define GLYPH(c) ((c) & 0xFF)
#define OWNER(c) (((c) >> 8) & 15)
#define HEAT(c)  (((c) >> 12) & 7)
#define SETHEAT(c, h) ((uint16_t)(((c) & 0x8FFF) | (((h) & 7) << 12)))

#define BLANK MK(' ', 7, 0)
#define FOODC MK('.', 10, 0)
#define HUMAN 15                 /* owner colour of the person at the keyboard */

/* ---- tunables (the "constants of physics") ---- */
#define FOOD      14             /* energy in one '.'                   */
#define EMAX      300
#define BUD_MIN   40             /* energy needed to bud (instruction s) */
#ifndef DECAY_ROWS
#define DECAY_ROWS 4             /* rows cooled per tick: trail lasts ~42 ticks */
#endif
#ifndef SUN
#define SUN       1              /* random food drops per tick           */
#endif
#define SPOIL_MASK 31            /* old food rots 1-in-32 per visit      */

typedef struct {
    int16_t  x, y, energy;
    uint16_t under;              /* the cell this wisp stands on (it carries it) */
    uint8_t  dir, lin, acc, alive, wait, skip;
} Wisp;

typedef struct {
    uint16_t *cells;             /* the screen */
    Wisp      w[MAXW];
    uint16_t  occ[LW * LH];      /* wisp index + 1, derived from w[] */
    uint32_t  rng, tick;
    uint8_t   key_air, key_ttl;  /* last typed glyph hangs in the air briefly */
} World;

void hw_beep(int acc);           /* provided by the platform */

static const int DX[4] = { 1, 0, -1, 0 };
static const int DY[4] = { 0, 1, 0, -1 };
static const uint8_t BODY[4] = { 0x10, 0x1F, 0x11, 0x1E };   /* > v < ^ pointers */

#define IDX(x, y) ((y) * LW + (x))
static inline int wx(int x) { while (x < 0) x += LW; while (x >= LW) x -= LW; return x; }
static inline int wy(int y) { while (y < 0) y += LH; while (y >= LH) y -= LH; return y; }

static inline uint32_t rnd(World *g) {
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g->rng = x;
}

/* read / write the *world* value of a cell (looking under any wisp) */
static inline uint16_t cget(World *g, int x, int y) {
    int i = IDX(wx(x), wy(y)), o = g->occ[i];
    return o ? g->w[o - 1].under : g->cells[i];
}
static inline void cput(World *g, int x, int y, uint16_t v) {
    int i = IDX(wx(x), wy(y)), o = g->occ[i];
    if (o) g->w[o - 1].under = v; else g->cells[i] = v;
}

static inline void draw_body(World *g, Wisp *w) {
    g->cells[IDX(w->x, w->y)] = (uint16_t)(BODY[w->dir & 3] | ((w->lin & 15) << 8));
}

static int spawn(World *g, int x, int y, int dir, int lin, int energy) {
    x = wx(x); y = wy(y);
    int i = IDX(x, y);
    if (g->occ[i]) return -1;
    uint16_t u = g->cells[i];
    if (GLYPH(u) == '#') return -1;
    int k = -1;
    for (int j = 0; j < MAXW; j++) if (!g->w[j].alive) { k = j; break; }
    if (k < 0) return -1;
    Wisp *w = &g->w[k];
    w->x = x; w->y = y; w->dir = dir & 3; w->lin = lin & 15; w->acc = 0;
    w->alive = 1; w->wait = 0; w->skip = 0; w->energy = energy; w->under = u;
    if (GLYPH(u) == '.') { w->energy += FOOD; w->under = BLANK; }
    g->occ[i] = k + 1;
    draw_body(g, w);
    return k;
}

static void kill(World *g, Wisp *w) {            /* death is recycling */
    int i = IDX(w->x, w->y);
    uint16_t u = w->under;
    if (GLYPH(u) == ' ') u = FOODC;
    g->cells[i] = u;
    g->occ[i] = 0;
    w->alive = 0;
}

static void try_move(World *g, Wisp *w, int idx) {
    int n = 1 + w->skip;
    int nx = wx(w->x + DX[w->dir] * n), ny = wy(w->y + DY[w->dir] * n);
    int ni = IDX(nx, ny);
    if (g->occ[ni]) { w->wait = 1; return; }                  /* crowded: wait */
    if (GLYPH(g->cells[ni]) == '#') {                         /* bark: bounce  */
        w->dir = (w->dir + 2) & 3; w->wait = 0; w->skip = 0;
        draw_body(g, w); return;
    }
    int oi = IDX(w->x, w->y);
    g->cells[oi] = SETHEAT(w->under, 7);                      /* leave a warm trail */
    g->occ[oi] = 0;
    uint16_t c = g->cells[ni];
    if (GLYPH(c) == '.') {                                    /* eat */
        w->energy += FOOD; if (w->energy > EMAX) w->energy = EMAX;
        c = BLANK;
    }
    w->under = c; w->x = nx; w->y = ny; w->wait = 0; w->skip = 0;
    g->occ[ni] = idx + 1;
    draw_body(g, w);
}

static void wisp_step(World *g, Wisp *w, int idx) {
    if (--w->energy <= 0) { kill(g, w); return; }             /* metabolism */
    if (w->wait) { try_move(g, w, idx); return; }             /* retry, don't re-execute */

    int gl = GLYPH(w->under), skip = 0;
    switch (gl) {
    case '>': w->dir = 0; break;
    case 'v': w->dir = 1; break;
    case '<': w->dir = 2; break;
    case '^': w->dir = 3; break;
    case ']': w->dir = (w->dir + 1) & 3; break;               /* turn right */
    case '[': w->dir = (w->dir + 3) & 3; break;               /* turn left  */
    case '+': w->acc++; break;
    case '-': w->acc--; break;
    case '*': w->acc <<= 1; break;
    case '/': w->acc >>= 1; break;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': w->acc = gl - '0'; break;
    case '\'':                                                /* literal: take next glyph, hop over it */
        w->acc = GLYPH(cget(g, w->x + DX[w->dir], w->y + DY[w->dir])); skip = 1; break;
    case '?': if (!w->acc) skip = 1; break;                   /* if acc==0 hop over next cell */
    case 'r': {                                               /* read the cell on my right hand */
        int h = (w->dir + 1) & 3;
        w->acc = GLYPH(cget(g, w->x + DX[h], w->y + DY[h])); break; }
    case 'h':                                                 /* feel heat ahead */
        w->acc = HEAT(cget(g, w->x + DX[w->dir], w->y + DY[w->dir])); break;
    case 'w': {                                               /* write acc onto my right hand */
        int h = (w->dir + 1) & 3, tx = wx(w->x + DX[h]), ty = wy(w->y + DY[h]);
        if (g->occ[IDX(tx, ty)]) break;
        uint16_t cur = cget(g, tx, ty);
        int cg = GLYPH(cur), cost = 2;
        if (cg != ' ' && cg != '.' && OWNER(cur) != w->lin)
            cost = (OWNER(cur) == HUMAN) ? 24 : 6;            /* foreign ground is expensive */
        if (w->energy > cost) {
            w->energy -= cost;
            cput(g, tx, ty, MK(w->acc ? w->acc : ' ', w->lin, 7));
        }
        break; }
    case 's': {                                               /* bud: split off a child behind me */
        if (w->energy < BUD_MIN) break;
        int b = (w->dir + 2) & 3;
        int e = w->energy - 4, half = e / 2;
        int k = spawn(g, w->x + DX[b], w->y + DY[b], w->dir, w->lin, half);
        if (k >= 0) w->energy = e - half;
        break; }
    case 'k': w->acc = g->key_air; break;                     /* sense the keyboard */
    case '~': hw_beep(w->acc); break;                         /* sing */
    case 'x': kill(g, w); return;
    default: break;
    }
    w->skip = skip;
    try_move(g, w, idx);
}

static void sweep(World *g) {        /* time passes: trails cool, cooled ground sprouts */
    for (int r = 0; r < DECAY_ROWS; r++) {
        int y = (int)(((uint64_t)g->tick * DECAY_ROWS + r) % LH);
        for (int x = 0; x < LW; x++) {
            int i = IDX(x, y);
            if (g->occ[i]) continue;
            uint16_t c = g->cells[i];
            int h = HEAT(c);
            if (h) {
                c = SETHEAT(c, h - 1);
                if (h == 1 && GLYPH(c) == ' ' && OWNER(c) == 7) c = FOODC;   /* fertility follows life */
                g->cells[i] = c;
            } else if (GLYPH(c) == '.' && (rnd(g) & SPOIL_MASK) == 0) {
                g->cells[i] = BLANK;                           /* old food rots */
            }
        }
    }
    for (int s = 0; s < SUN; s++) {
        int i = rnd(g) % (LW * LH);
        uint16_t c = g->cells[i];
        if (!g->occ[i] && GLYPH(c) == ' ' && OWNER(c) == 7 && HEAT(c) == 0) g->cells[i] = FOODC;
    }
}

static void world_tick(World *g) {
    g->tick++;
    if (g->key_ttl && !--g->key_ttl) g->key_air = 0;
    int start = g->tick % MAXW;
    for (int k = 0; k < MAXW; k++) {
        int j = (start + k) % MAXW;
        if (g->w[j].alive) wisp_step(g, &g->w[j], j);
    }
    sweep(g);
}

static void world_stats(World *g, int *n, int *e) {
    *n = 0; *e = 0;
    for (int j = 0; j < MAXW; j++) if (g->w[j].alive) { (*n)++; *e += g->w[j].energy; }
}

/* ---- genesis: the first world ---- */
static void put(World *g, int x, int y, int ch, int ink) { cput(g, x, y, MK(ch, ink, 0)); }
static void text(World *g, int x, int y, const char *s, int ink) {
    for (; *s; s++) put(g, x++, y, *s, ink);
}
static void ring(World *g, int x0, int y0, int x1, int y1, int cw, int ink) {
    for (int x = x0; x <= x1; x++)
        if ((x - x0) % 5 == 2) { cput(g, x, y0, FOODC); cput(g, x, y1, FOODC); }
    for (int y = y0; y <= y1; y++)
        if ((y - y0) % 5 == 2) { cput(g, x0, y, FOODC); cput(g, x1, y, FOODC); }
    if (cw) {
        put(g, x0, y0, '>', ink); put(g, x1, y0, 'v', ink);
        put(g, x1, y1, '<', ink); put(g, x0, y1, '^', ink);
        put(g, (x0 + x1) / 2, y0, 's', ink);
        spawn(g, x0 + 1, y0, 0, ink, 160);
        spawn(g, x1 - 1, y1, 2, ink, 160);
    } else {
        put(g, x0, y0, 'v', ink); put(g, x0, y1, '>', ink);
        put(g, x1, y1, '^', ink); put(g, x1, y0, '<', ink);
        put(g, (x0 + x1) / 2, y1, 's', ink);
        spawn(g, x1 - 1, y0, 2, ink, 160);
        spawn(g, x0 + 1, y1, 0, ink, 160);
    }
}

static void genesis(World *g) {
    for (int i = 0; i < LW * LH; i++) { g->cells[i] = BLANK; g->occ[i] = 0; }
    for (int j = 0; j < MAXW; j++) g->w[j].alive = 0;
    g->tick = 0; g->key_air = 0; g->key_ttl = 0;
    if (!g->rng) g->rng = 0x2545F491u;
    text(g, 34, 1, "L  O  A  M", HUMAN);
    text(g, 17, 2, "an operating system whose memory is its screen", HUMAN);
    ring(g, 4, 6, 38, 14, 1, 12);       /* red colony, clockwise        */
    ring(g, 52, 10, 77, 20, 0, 9);      /* blue colony, anticlockwise   */
    ring(g, 44, 4, 62, 8, 1, 14);       /* yellow colony                */
    text(g, 4, 18, "type anywhere. ENTER breathes life.", HUMAN);
    text(g, 4, 19, "wisps eat '.', run the glyph they stand on,", HUMAN);
    text(g, 4, 20, "and starve if they find nothing.", HUMAN);
}
#endif

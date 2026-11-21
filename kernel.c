/* LOAM kernel -- original code.  Platform glue for the world in loam.h:
 * VGA text memory as the heap, PS/2 keyboard, PIT clock, PC speaker,
 * and ATA disk for continuous snapshots.  No interrupts, no IDT:
 * the machine just breathes in a loop.
 */
#include <stdint.h>
#include "loam.h"

/* ---------- freestanding basics ---------- */
void *memset(void *d, int c, unsigned long n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) {
    uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d;
}

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void insw(uint16_t p, void *d, unsigned n) {
    __asm__ volatile("rep insw" : "+D"(d), "+c"(n) : "d"(p) : "memory");
}
static inline void outsw(uint16_t p, const void *s, unsigned n) {
    __asm__ volatile("rep outsw" : "+S"(s), "+c"(n) : "d"(p) : "memory");
}

/* ---------- the world lives in video memory ---------- */
#define VGA ((volatile uint16_t *)0xB8000)
static World G;

/* ---------- speaker ---------- */
static int beep_ttl;
void hw_beep(int a) {
    uint32_t f = 180 + (uint32_t)a * 24, d = 1193182u / f;
    outb(0x43, 0xB6); outb(0x42, d & 0xFF); outb(0x42, d >> 8);
    outb(0x61, inb(0x61) | 3);
    beep_ttl = 4;
}

/* ---------- clock: PIT channel 0, polled ---------- */
#define TICK_HZ 30
static uint16_t pit_last;
static uint16_t pit_read(void) { outb(0x43, 0); uint8_t lo = inb(0x40); uint8_t hi = inb(0x40); return (uint16_t)(lo | (hi << 8)); }
static void pit_init(void) {
    uint32_t d = 1193182u / TICK_HZ;
    outb(0x43, 0x34); outb(0x40, d & 0xFF); outb(0x40, (d >> 8) & 0xFF);
    pit_last = pit_read();
}
static int pit_ticks(void) {         /* how many ticks since last call (counter counts down) */
    uint16_t c = pit_read();
    int t = (c > pit_last);
    pit_last = c;
    return t;
}

/* ---------- disk: ATA PIO, primary master ---------- */
#define STATE_LBA 128
#define SNAP_SECTORS 12
static int ata_wait(int want_drq) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t s = inb(0x1F7);
        if (s == 0xFF) return -1;
        if (s & 0x01) return -1;               /* ERR */
        if (!(s & 0x80) && (!want_drq || (s & 0x08))) return 0;
    }
    return -1;
}
static int ata_setup(uint32_t lba, int n, uint8_t cmd) {
    outb(0x3F6, 0x02);                         /* no interrupts */
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 4; i++) inb(0x1F7);    /* 400 ns */
    if (ata_wait(0)) return -1;
    outb(0x1F2, n);
    outb(0x1F3, lba & 0xFF); outb(0x1F4, (lba >> 8) & 0xFF); outb(0x1F5, (lba >> 16) & 0xFF);
    outb(0x1F7, cmd);
    return 0;
}
static int ata_read(uint32_t lba, int n, void *buf) {
    if (ata_setup(lba, n, 0x20)) return -1;
    uint16_t *p = buf;
    for (int s = 0; s < n; s++) { if (ata_wait(1)) return -1; insw(0x1F0, p, 256); p += 256; }
    return 0;
}
static int ata_write(uint32_t lba, int n, const void *buf) {
    if (ata_setup(lba, n, 0x30)) return -1;
    const uint16_t *p = buf;
    for (int s = 0; s < n; s++) { if (ata_wait(1)) return -1; outsw(0x1F0, p, 256); p += 256; }
    if (ata_wait(0)) return -1;
    outb(0x1F7, 0xE7);                         /* flush cache */
    return ata_wait(0);
}

/* ---------- persistence: the machine never shuts down ---------- */
#define SNAP_MAGIC 0x4D414F4Cu                 /* "LOAM" */
typedef struct {
    uint32_t magic, ver, tick, rng, cx, cy, facing, ink, paused, sum;
    Wisp     w[MAXW];
    uint16_t cells[LW * LH];
} SnapBody;
typedef union { SnapBody b; uint8_t raw[SNAP_SECTORS * 512]; } Snap;
_Static_assert(sizeof(SnapBody) <= SNAP_SECTORS * 512, "snapshot too large");
static Snap snap;

/* user state */
static int cx = 40, cy = 3, facing = 0, ink = 0, paused = 0;
#define NINKS 6
static const uint8_t INKS[NINKS] = { 15, 12, 9, 14, 13, 11 };

static uint32_t snap_sum(void) {
    uint32_t s = 0; const uint32_t *p = (const uint32_t *)snap.raw;
    for (unsigned i = 0; i < sizeof(Snap) / 4; i++) s = s * 31 + p[i];
    return s;
}
static int save_state(void) {
    memset(&snap, 0, sizeof snap);
    snap.b.magic = SNAP_MAGIC; snap.b.ver = 1;
    snap.b.tick = G.tick; snap.b.rng = G.rng;
    snap.b.cx = cx; snap.b.cy = cy; snap.b.facing = facing; snap.b.ink = ink; snap.b.paused = paused;
    memcpy(snap.b.w, G.w, sizeof G.w);
    for (int i = 0; i < LW * LH; i++) snap.b.cells[i] = VGA[i];
    snap.b.sum = 0; snap.b.sum = snap_sum();
    return ata_write(STATE_LBA, SNAP_SECTORS, snap.raw);
}
static int load_state(void) {
    if (ata_read(STATE_LBA, SNAP_SECTORS, snap.raw)) return -1;
    if (snap.b.magic != SNAP_MAGIC || snap.b.ver != 1) return -1;
    uint32_t want = snap.b.sum; snap.b.sum = 0;
    if (snap_sum() != want) return -1;
    for (int i = 0; i < LW * LH; i++) VGA[i] = snap.b.cells[i];
    memcpy(G.w, snap.b.w, sizeof G.w);
    memset(G.occ, 0, sizeof G.occ);
    for (int j = 0; j < MAXW; j++) {
        Wisp *w = &G.w[j];
        if (!w->alive) continue;
        if (w->x < 0 || w->x >= LW || w->y < 0 || w->y >= LH || G.occ[IDX(w->x, w->y)]) { w->alive = 0; continue; }
        G.occ[IDX(w->x, w->y)] = j + 1;
    }
    G.tick = snap.b.tick; G.rng = snap.b.rng ? snap.b.rng : 0x2545F491u;
    cx = snap.b.cx % LW; cy = snap.b.cy % LH; facing = snap.b.facing & 3; ink = snap.b.ink % NINKS; paused = snap.b.paused & 1;
    return 0;
}

/* ---------- keyboard: PS/2 set 1, polled ---------- */
static const char SC_LO[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
    'q','w','e','r','t','y','u','i','o','p','[',']', 10, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ' };
static const char SC_HI[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, 9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 10, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ' };
static int shift, ext_pending, saved_flash;

static void move_cursor(int dir) { cx = wx(cx + DX[dir]); cy = wy(cy + DY[dir]); }
static void do_save(void) { if (!save_state()) saved_flash = 20; }

static void on_scancode(uint8_t sc) {
    if (sc == 0xE0) { ext_pending = 1; return; }
    int ext = ext_pending; ext_pending = 0; (void)ext;
    int release = sc & 0x80; sc &= 0x7F;
    if (sc == 0x2A || sc == 0x36) { shift = !release; return; }
    if (release) return;
    switch (sc) {
    case 0x48: facing = 3; move_cursor(3); return;       /* arrows: move and face */
    case 0x50: facing = 1; move_cursor(1); return;
    case 0x4B: facing = 2; move_cursor(2); return;
    case 0x4D: facing = 0; move_cursor(0); return;
    case 0x53: cput(&G, cx, cy, BLANK); return;           /* Delete */
    case 0x01: paused = !paused; return;                   /* Esc */
    case 0x0F: ink = (ink + 1) % NINKS; return;                /* Tab */
    case 0x1C: spawn(&G, cx, cy, facing, INKS[ink], 120); return;   /* Enter: breathe life */
    case 0x3C: do_save(); return;                          /* F2 */
    case 0x3F: genesis(&G); cx = 40; cy = 3; facing = 0; do_save(); return;  /* F5 */
    case 0x0E: move_cursor((facing + 2) & 3); cput(&G, cx, cy, BLANK); return; /* Backspace */
    }
    if (sc < 58) {
        char ch = shift ? SC_HI[sc] : SC_LO[sc];
        if (ch >= 32 && ch < 127) {
            cput(&G, cx, cy, MK(ch, INKS[ink], 0));
            G.key_air = (uint8_t)ch; G.key_ttl = 12;
            move_cursor(facing);
        }
    }
}
static void poll_keys(void) {
    while (inb(0x64) & 1) {
        uint8_t st = inb(0x64); (void)st;
        uint8_t sc = inb(0x60);
        on_scancode(sc);
    }
}

/* ---------- status bar (row 24, outside the torus) ---------- */
static void num(char *s, int col, uint32_t v, int width, char pad) {
    for (int i = width - 1; i >= 0; i--) { s[col + i] = v ? (char)('0' + v % 10) : (i == width - 1 ? '0' : pad); v /= 10; }
}
static void lit(char *s, int col, const char *t) { while (*t) s[col++] = *t++; }
static void draw_status(void) {
    char s[LW + 1];
    for (int i = 0; i < LW; i++) s[i] = ' ';
    int n, e; world_stats(&G, &n, &e);
    lit(s, 1, "LOAM");
    s[6] = 't'; num(s, 7, G.tick, 7, '0');
    s[15] = 'w'; num(s, 16, n, 3, '0');
    s[20] = 'e'; num(s, 21, e, 5, '0');
    lit(s, 27, "ink ");
    s[32] = "\x1A\x19\x1B\x18"[facing];
    lit(s, 36, "ESC pause F2 save F5 new TAB ink ENTER life");
    volatile uint16_t *row = VGA + LW * LH;
    for (int i = 0; i < LW; i++) row[i] = (uint16_t)((uint8_t)s[i] | (0x70 << 8));
    row[31] = (uint16_t)(0xDB | (INKS[ink] << 8));
    if (paused) { const char *p = "ESC PLAY"; for (int i = 0; i < 8; i++) row[36 + i] = (uint16_t)((uint8_t)p[i] | (0x4F << 8)); }
    if (saved_flash) row[79] = (uint16_t)('*' | (0x72 << 8));
}
static void place_cursor(void) {
    uint16_t pos = (uint16_t)(cy * LW + cx);
    outb(0x3D4, 0x0F); outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E); outb(0x3D5, pos >> 8);
}

/* ---------- palette: heat is colour, so we choose the colours ---------- */
static void set_palette(void) {
    /* bg/fg colours 0..7 become an ember ramp: cold -> hot.  (colour 6 lives at DAC 0x14) */
    static const uint8_t idx[8] = { 0, 1, 2, 3, 4, 5, 0x14, 7 };
    static const uint8_t rgb[8][3] = {
        {  0,  0,  0 }, {  5,  3, 14 }, { 10,  5, 26 }, { 25,  6, 30 },
        { 40,  8, 22 }, { 54, 26,  8 }, { 61, 46, 10 }, { 63, 61, 42 } };
    for (int i = 0; i < 8; i++) {
        outb(0x3C8, idx[i]);
        outb(0x3C9, rgb[i][0]); outb(0x3C9, rgb[i][1]); outb(0x3C9, rgb[i][2]);
    }
}

/* ---------- main ---------- */
extern char __bss_start[], __bss_end[];
void kernel_main(void) {
    G.cells = (uint16_t *)VGA;
    G.rng = 0x2545F491u;
    set_palette();
    pit_init();
    if (load_state()) { genesis(&G); cx = 40; cy = 3; do_save(); }
    int since_save = 0;
    for (;;) {
        poll_keys();
        if (!pit_ticks()) { place_cursor(); continue; }
        if (!paused) world_tick(&G);
        if (beep_ttl && !--beep_ttl) outb(0x61, inb(0x61) & ~3);
        if (saved_flash) saved_flash--;
        if (++since_save >= 300) { since_save = 0; do_save(); }   /* every ~10 s */
        draw_status();
        place_cursor();
    }
}

__attribute__((section(".text.start"), noreturn)) void kernel_entry(void) {
    uint64_t n = (uint64_t)(__bss_end - __bss_start); char *p = __bss_start;
    __asm__ volatile("rep stosb" : "+D"(p), "+c"(n) : "a"(0) : "memory");
    kernel_main();
    for (;;) __asm__ volatile("hlt");
}

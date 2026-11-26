/* PC simulator: runs firmware/src/app (modes, effects, clocks, gfx) unchanged and dumps frames.
 * Everything is driven through modes_handle_cmd() with the same packets the BLE tools send.
 * Output: frames.bin = repeated [32-byte label][32*32*3 RGB].  See make_sheet.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gfx.h"
#include "effects.h"
#include "modes.h"
#include "clockface.h"
#include "app_port.h"
#include "settings.h"
#include "anim.h"

static int quiet;
static uint8_t last_reply[16];
static unsigned last_bright = 999, last_orient = 999;

void app_set_brightness_pct(unsigned p) { last_bright = p; }
void app_set_orientation(unsigned o)    { last_orient = o; }
void app_reply(uint16_t c, uint8_t s, const uint8_t *x, unsigned n)
{
    memset(last_reply, 0, sizeof last_reply);
    last_reply[0] = (uint8_t)(c >> 8); last_reply[1] = (uint8_t)c; last_reply[2] = s;
    memcpy(last_reply + 3, x, n > 11 ? 11 : n);
    if (quiet) return;
    printf("  reply cmd %04X status %u:", c, s);
    for (unsigned i = 0; i < n; i++) printf(" %02X", x[i]);
    printf("\n");
}

/* ---- emulated internal flash for settings.c and anim.c: NOR rules (erase -> 0xFF, program only clears bits) ---- */
#define FL_BASE ANIM_BASE
static uint8_t flash_mem[0x200000u - ANIM_BASE];
static int fl_erases, fl_programs, fl_bad, fl_cut = -1;

const void *app_flash_ptr(uint32_t a) { return flash_mem + (a - FL_BASE); }

int app_flash_program(uint32_t a, const void *src, uint32_t n)
{
    if (a < FL_BASE || a + n > FL_BASE + sizeof flash_mem || (a & 3u) || (n & 3u)) return -1;
    const uint8_t *b = src;
    uint32_t todo = (fl_cut >= 0 && (uint32_t)fl_cut < n) ? (uint32_t)fl_cut : n;   /* power cut mid-write */
    for (uint32_t i = 0; i < todo; i++) {
        uint8_t *m = &flash_mem[a - FL_BASE + i];
        if ((*m & b[i]) != b[i]) fl_bad++;                 /* would need an erase: data comes out wrong */
        *m &= b[i];
    }
    fl_programs++;
    return 0;
}

void app_flash_forget(void) { }

int app_flash_write_sector(uint32_t a, const void *src, uint32_t n)
{
    if ((a & 0xFFFu) || n > 4096u || a < FL_BASE || a + 4096u > FL_BASE + sizeof flash_mem) return -1;
    memset(&flash_mem[a - FL_BASE], 0xFF, 4096);
    fl_erases++;
    return app_flash_program(a, src, (n + 3u) & ~3u);
}

static fb_t fb;
static FILE *out;
static uint32_t now = 1000;

static int cmd(uint16_t c, const uint8_t *payload, unsigned n)
{
    static uint8_t p[4 + 8 + 4096] __attribute__((aligned(4)));
    unsigned len = 4 + n;
    p[0] = (uint8_t)len; p[1] = (uint8_t)(len >> 8); p[2] = (uint8_t)c; p[3] = (uint8_t)(c >> 8);
    if (n) memcpy(p + 4, payload, n);
    int r = modes_handle_cmd(c, p, len, now);
    if (r != 1 && r != 2 && !quiet) printf("  cmd %04X -> %d (!)\n", c, r);
    return r;
}

static void run(const char *label, uint32_t ms, const uint32_t *snaps, int nsnap)
{
    uint32_t t0 = now;
    int k = 0;
    for (uint32_t t = 0; t <= ms; t += 25) {
        now = t0 + t;
        modes_render(&fb, now);
        if (k < nsnap && t >= snaps[k]) {
            char name[32] = { 0 };
            snprintf(name, sizeof name, "%s", label);
            fwrite(name, 1, 32, out);
            fwrite(fb, 1, sizeof fb, out);
            k++;
        }
    }
}

/* A text strip like the web UI sends (it draws any script with the browser's fonts): here the 5x7 font,
 * 11 rows, 2 bytes per column, bit y = row y. Returns the width in columns. */
#define STRIP_H 11
static int make_strip(uint8_t *cols, int max_w, const char *s)
{
    static fb_t tmp;
    fb_t *keep = gfx_fb();
    int w = 0;
    gfx_target(&tmp);
    for (; *s && w < max_w; s++) {
        gfx_fill(0);
        int adv = gfx_char(0, 2, *s, RGB(255, 255, 255));
        for (int x = 0; x < adv && w < max_w; x++, w++) {
            uint16_t bits = 0;
            for (int y = 0; y < STRIP_H; y++) if (tmp[y][x][0]) bits |= (uint16_t)(1u << y);
            cols[2 * w] = (uint8_t)bits; cols[2 * w + 1] = (uint8_t)(bits >> 8);
        }
    }
    gfx_target(keep);
    return w;
}

/* ---- "sim settings": power-cycle tests of settings.c against the emulated flash ----------------- */
static int nfail;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); nfail++; } } while (0)

static void reboot(int apply)               /* RAM is lost on a power cycle; flash_mem stays */
{
    now += 5000;
    last_bright = last_orient = 999;
    modes_init(now);
    settings_load(now, apply);
}

static void idle(uint32_t ms)               /* main loop: render + settings tick every 25 ms */
{
    for (uint32_t t = 0; t < ms; t += 25) { now += 25; modes_render(&fb, now); settings_tick(now, 1); }
}

static void status(uint8_t s[11]) { cmd(0xF106, 0, 0); memcpy(s, last_reply + 3, 11); }
static void sinfo(uint8_t s[8])   { settings_info(s); }
static void bright(uint8_t pct)   { cmd(0x8004, &pct, 1); }

static int settings_test(void)
{
    uint8_t st[11], si[8];
    quiet = 1;
    for (unsigned i = 0; i < sizeof flash_mem; i++) flash_mem[i] = (uint8_t)(i * 7u + 3u);   /* unknown old content */

    printf("1. first boot on old flash content\n");
    reboot(1);
    idle(2500);
    status(st);
    CHECK(st[0] == MODE_EFFECT && st[2] == 80 && st[8] == 2, "factory settings: effect, 80 %, orientation 2");
    sinfo(si);
    CHECK(si[4] == 16 && fl_programs == 0, "no valid record, nothing written, next save erases first");

    printf("2. change settings, saved once after 3 s of quiet\n");
    bright(55);
    uint8_t o = 1; cmd(0x8006, &o, 1);
    uint8_t ck[7] = { 3, 0, 0, 26, 11, 22, 7 }; cmd(0x0106, ck, 7);        /* clock face 3, 12 h, no date */
    idle(2000);
    CHECK(fl_programs == 0, "nothing written while changes are recent");
    idle(2000);
    CHECK(fl_erases == 1 && fl_programs == 1, "one erase + one record after the quiet time");
    idle(60000);
    CHECK(fl_programs == 1, "no further writes in 60 s without changes");

    printf("3. power cycle\n");
    reboot(1);
    CHECK(last_bright == 55 && last_orient == 1, "brightness 55 and orientation 1 applied at boot");
    idle(2500);
    status(st);
    CHECK(st[0] == MODE_CLOCK && st[9] == 3 && !(st[7] & 4) && !(st[7] & 8), "back on clock face 3, 12 h, no date");

    printf("4. effect autoplay does not wear the flash\n");
    uint8_t ap[3] = { 1, 5, 0 }; cmd(0xF104, ap, 3);                       /* autoplay every 5 s */
    idle(4000);
    int p0 = fl_programs;
    idle(120000);
    CHECK(fl_programs == p0, "no writes in 2 min of autoplay (24 effect changes)");

    printf("5. 40 saves: the log wraps and erases only when full\n");
    int e0 = fl_erases;
    for (int i = 0; i < 40; i++) { bright((uint8_t)(20 + i)); idle(3600); }
    CHECK(fl_erases - e0 == 2, "40 records from slot 2 on -> 2 sector erases (16 slots per erase)");
    CHECK(fl_bad == 0, "never programmed over unerased bytes");
    reboot(1);
    CHECK(last_bright == 59, "after power cycle: the last value (59)");

    printf("6. damaged newest record\n");
    sinfo(si);
    unsigned newest = (si[4] + 15u) % 16u;
    flash_mem[SETTINGS_SECTOR - FL_BASE + newest * 256u + 40u] ^= 0x10;
    reboot(1);
    CHECK(last_bright == 58, "falls back to the previous record (58)");

    printf("7. power cut in the middle of a save\n");
    bright(77); idle(3600);
    fl_cut = 100;
    bright(66); idle(3600);
    fl_cut = -1;
    reboot(1);
    CHECK(last_bright == 77, "half-written record ignored, previous one used (77)");
    bright(44); idle(3600);
    CHECK(fl_bad == 0, "next save goes to a fresh slot");
    reboot(1);
    CHECK(last_bright == 44, "and is used after the next power cycle (44)");

    printf("8. picture\n");
    uint8_t fun = 1; cmd(0x0104, &fun, 1);
    uint8_t px[5 * 32]; unsigned n = 0;
    for (int i = 0; i < 32; i++) { px[n++] = (uint8_t)i; px[n++] = (uint8_t)(i / 2); px[n++] = 200; px[n++] = (uint8_t)(i * 8); px[n++] = 30; }
    cmd(0xF105, px, n);
    uint8_t before[32 * 32 * 3]; memcpy(before, modes_canvas(), sizeof before);
    int e1 = fl_erases;
    idle(5000);
    CHECK(fl_erases == e1, "picture not written within 5 s");
    idle(5000);
    CHECK(fl_erases == e1 + 1, "picture written once after 8 s of quiet");
    reboot(1);
    idle(2500);
    status(st);
    CHECK(st[0] == MODE_CANVAS && memcmp(before, modes_canvas(), sizeof before) == 0, "after power cycle: same picture on screen");

    printf("9. boot after a crash reset\n");
    reboot(0);
    CHECK(last_bright == 80, "saved settings not applied (factory 80 %)");
    idle(4000);
    reboot(1);
    CHECK(last_bright == 80, "factory settings replaced the saved record");

    printf("10. factory reset command\n");
    bright(30); idle(3600);
    uint8_t a5 = 0xA5; cmd(0xF108, &a5, 1);
    idle(3600);
    reboot(1);
    idle(2500);
    status(st);
    CHECK(last_bright == 80 && st[0] == MODE_EFFECT && st[8] == 2, "F108 A5: factory settings after power cycle");
    uint8_t bad = 0x11;
    CHECK(cmd(0xF108, &bad, 1) == 0, "F108 without the A5 key is refused");

    printf("11. garbage in every slot\n");
    for (unsigned i = 0; i < 4096; i++) flash_mem[SETTINGS_SECTOR - FL_BASE + i] = (uint8_t)(i * 13u);
    reboot(1);
    CHECK(last_bright == 80, "factory settings");
    bright(90); idle(3600);
    reboot(1);
    CHECK(last_bright == 90 && fl_bad == 0, "first save erases the sector, then works");

    printf("12. a record written by 99.12-99.13 (layout 1) still loads\n");
    {
        modes_persist_t p;
        bright(33); idle(3600);
        modes_snapshot(&p);
        p.ver = 1;
        uint8_t r[256];
        memset(r, 0, sizeof r);
        uint32_t magic = 0x31475453u, sq = 500; uint16_t ln = MODES_PERSIST_V1_LEN;
        memcpy(r, &magic, 4); memcpy(r + 4, &sq, 4); memcpy(r + 8, &ln, 2);
        memcpy(r + 12, &p, MODES_PERSIST_V1_LEN);
        uint32_t c = 0xFFFFFFFFu;
        for (int i = 0; i < 252; i++) { c ^= r[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
        c = ~c; memcpy(r + 252, &c, 4);
        memset(&flash_mem[SETTINGS_SECTOR - FL_BASE], 0xFF, 4096);
        memcpy(&flash_mem[SETTINGS_SECTOR - FL_BASE], r, 256);
        bright(70);
        reboot(1);
        CHECK(last_bright == 33, "layout-1 record restored (33 %)");
        bright(61); idle(3600);
        reboot(1);
        CHECK(last_bright == 61 && fl_bad == 0, "and new records follow it");
    }

    printf("13. text strip (0xF10A, e.g. Arabic from the web UI) survives a power cycle\n");
    {
        uint8_t pk[8 + 2 * 40];
        pk[0] = 255; pk[1] = 120; pk[2] = 200; pk[3] = 5; pk[4] = 2;   /* colour, speed, flags: RTL */
        pk[5] = 11; pk[6] = 40; pk[7] = 0;                             /* h 11, w 40, 2 bytes per column */
        for (int i = 0; i < 80; i++) pk[8 + i] = (uint8_t)(i * 37 + 5);
        int e0 = fl_erases;
        CHECK(cmd(0xF10A, pk, sizeof pk) == 1, "0xF10A accepted");
        unsigned len; const uint8_t *tb = modes_textbits(&len);
        CHECK(tb && len == 108 + 80 && tb[0] == 0xA7, "strip stored as a one-message playlist");
        uint8_t keep[188]; memcpy(keep, tb, 188);
        idle(5000);
        CHECK(fl_erases > e0, "strip written to flash after the quiet time");
        reboot(1);
        idle(2500);
        status(st);
        tb = modes_textbits(&len);
        CHECK(st[0] == MODE_TEXT && tb && len == 188 && memcmp(tb, keep, 188) == 0, "after power cycle: same strip, text mode");
        uint8_t bad[8] = { 1, 1, 1, 1, 0, 30, 2, 0 };
        CHECK(cmd(0xF10A, bad, sizeof bad) == 0, "wrong length refused");
        const char *m = "Back to English";
        uint8_t tx[40] = { 255, 255, 255, 4, 0 }; memcpy(tx + 5, m, strlen(m));
        cmd(0xF102, tx, 5 + (unsigned)strlen(m));
        CHECK(modes_textbits(&len) == 0, "normal text replaces the strip");
    }

    printf("14. message playlist (0xF10B) survives a power cycle\n");
    {
        uint8_t stl[16] = { 7, 3, 18, 1, 255, 0, 0, 0, 0, 255, 6, 8, 0x40, 2 + 15, 120, 2 };   /* bg: lava lamp */
        uint8_t pk[3 + 16 + 1 + 40];
        pk[0] = 0; pk[1] = 2; pk[2] = 1; memcpy(pk + 3, stl, 16); pk[19] = 1;
        const char *msg = "IT IS {time}";
        memcpy(pk + 20, msg, strlen(msg));
        CHECK(cmd(0xF10B, pk, 20 + (unsigned)strlen(msg)) == 1, "message 1 of 2 (panel font + token) stored");
        uint8_t sp[3 + 16 + 3 + 2 * 64];
        sp[0] = 1; sp[1] = 2; sp[2] = 0; memcpy(sp + 3, stl, 16); sp[3] = 13;
        int w = make_strip(sp + 22, 64, "Strip!");
        sp[19] = STRIP_H; sp[20] = (uint8_t)w; sp[21] = 0;
        CHECK(cmd(0xF10B, sp, 22 + 2 * (unsigned)w) == 1, "message 2 of 2 (strip) stored, playlist starts");
        uint8_t bad[24] = { 2, 2, 1 };
        CHECK(cmd(0xF10B, bad, sizeof bad) == 0, "out-of-order message refused");
        unsigned len; const uint8_t *tb = modes_textbits(&len);
        uint8_t keep[400]; memcpy(keep, tb, len); unsigned klen = len;
        idle(5000);
        reboot(1);
        idle(2500);
        status(st);
        tb = modes_textbits(&len);
        CHECK(st[0] == MODE_TEXT && tb && len == klen && memcmp(tb, keep, len) == 0, "after power cycle: same playlist, text mode");
    }

    printf("%s: %d failed\n", nfail ? "SETTINGS TEST FAILED" : "settings test passed", nfail);
    return nfail ? 1 : 0;
}

/* ---- "sim anim <dir>": animation upload, storage and player against files made by webui/anim.js --------- */
typedef struct { uint8_t *opa, *rgb; uint16_t *del; uint32_t len; unsigned frames; } vec_t;

static uint8_t *slurp(const char *dir, const char *name, const char *ext, uint32_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s%s", dir, name, ext);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s (run: node tools/sim/anim_vectors.js %s)\n", path, dir); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) n = 0;
    fclose(f);
    *len = (uint32_t)n;
    return b;
}

static int load_vec(const char *dir, const char *name, vec_t *v)
{
    uint32_t n1, n2;
    v->opa = slurp(dir, name, ".opa", &v->len);
    v->rgb = slurp(dir, name, ".rgb", &n1);
    v->del = (uint16_t *)slurp(dir, name, ".del", &n2);
    v->frames = n2 / 2;
    return v->opa && v->rgb && v->del && n1 == v->frames * 3072u;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* the web UI's upload: begin, then 4 KiB pieces. Returns 0 = stored, else the failing step's reason code
 * (+100 for begin); stop_after >= 0 stops sending after that many pieces. */
static int upload(unsigned slot, const uint8_t *f, uint32_t len, int play, int stop_after)
{
    uint8_t b[6] = { (uint8_t)slot };
    put32(b + 1, len);
    b[5] = (uint8_t)(play ? 1 : 0);
    cmd(ANIM_CMD_BEGIN, b, 6);
    if (last_reply[2] != 1) return 100 + last_reply[3];
    static uint8_t pk[8 + 4096];
    int pieces = 0;
    for (uint32_t off = 0; off < len; off += 4096) {
        if (stop_after >= 0 && pieces == stop_after) return -1;
        uint32_t n = len - off < 4096 ? len - off : 4096;
        put32(pk, off);
        memcpy(pk + 4, f + off, n);
        cmd(ANIM_CMD_DATA, pk, 4 + n);
        uint32_t echo = last_reply[3] | (last_reply[4] << 8) | (last_reply[5] << 16) | ((uint32_t)last_reply[6] << 24);
        if (echo != off) return 98;
        if (last_reply[2] == 0) return last_reply[7];
        if ((last_reply[2] == 2) != (off + n == len)) return 97;
        pieces++;
    }
    return 0;
}

static void anim_list(uint32_t *valid, uint32_t *used)
{
    cmd(ANIM_CMD_LIST, 0, 0);
    *valid = last_reply[5] | (last_reply[6] << 8) | ((uint32_t)last_reply[7] << 16) | ((uint32_t)last_reply[8] << 24);
    *used = last_reply[9] | (last_reply[10] << 8) | ((uint32_t)last_reply[11] << 16) | ((uint32_t)last_reply[12] << 24);
}

static void play(uint8_t slot, uint8_t speed) { uint8_t b[2] = { slot, speed }; cmd(ANIM_CMD_PLAY, b, 2); }

/* render for two rounds and compare every 25 ms tick with the frame that must be on screen then */
static int play_matches(const vec_t *v, const char *label)
{
    uint32_t total = 0;
    for (unsigned i = 0; i < v->frames; i++) total += v->del[i];
    int bad = 0;
    uint32_t t0 = now;
    for (uint32_t t = 0; t < 2 * total; t += 25) {
        now = t0 + t;
        modes_render(&fb, now);
        uint32_t in = t % total, acc = 0;
        unsigned k = 0;
        while (k + 1 < v->frames && acc + v->del[k] <= in) acc += v->del[k++];
        if (memcmp(fb, v->rgb + k * 3072u, 3072) != 0) bad++;
        if (out && label[0] && t < total && (t == 0 || in - acc < 25)) {   /* one picture per frame, first round */
            char name[32] = { 0 };
            snprintf(name, sizeof name, "%s %u", label, k);
            fwrite(name, 1, 32, out);
            fwrite(fb, 1, sizeof fb, out);
        }
    }
    return bad;
}

static int anim_test(const char *dir)
{
    vec_t A, B, C;
    if (!load_vec(dir, "bounce", &A) || !load_vec(dir, "plasma", &B) || !load_vec(dir, "still", &C)) return 2;
    uint8_t st[11];
    uint32_t valid, used;
    quiet = 1;
    out = fopen("anim_frames.bin", "wb");
    for (unsigned i = 0; i < sizeof flash_mem; i++) flash_mem[i] = (uint8_t)(i * 7u + 3u);   /* unknown old content */

    printf("1. first boot on old flash content\n");
    reboot(1);
    anim_list(&valid, &used);
    CHECK(last_reply[2] == 1 && valid == 0 && used == 0 && last_reply[3] == 20 && last_reply[4] == 32, "store empty: 20 slots of 32 KiB");
    CHECK(fl_programs == 0 && fl_erases == 0, "nothing written");

    printf("2. upload a small animation into slot 0 and play it\n");
    int r = upload(0, A.opa, A.len, 1, -1);
    CHECK(r == 0, "uploaded (begin, 1 piece, stored)");
    anim_list(&valid, &used);
    CHECK(valid == 1u && used == 1u, "slot 0 holds it");
    uint8_t q[2] = { 0, 0 };
    cmd(ANIM_CMD_INFO, q, 2);
    CHECK(last_reply[2] == 1 && last_reply[3] == 0 && (unsigned)(last_reply[5] | (last_reply[6] << 8)) == A.frames &&
          (uint32_t)(last_reply[7] | (last_reply[8] << 8) | (last_reply[9] << 16)) == A.len, "info: frames and size");
    q[1] = 1; cmd(ANIM_CMD_INFO, q, 2);
    CHECK(last_reply[2] == 1 && memcmp(last_reply + 5, "bounce \xE2\x9C", 9) == 0, "info part 1: name bytes 0-8");
    status(st);
    CHECK(st[0] == MODE_ANIM && st[1] == 0, "play-when-done: animation mode, slot 0 on screen");
    CHECK(play_matches(&A, "bounce") == 0, "every frame on time and pixel-exact (2 rounds)");

    printf("3. a bigger one across several slots, at half speed\n");
    r = upload(3, B.opa, B.len, 0, -1);
    CHECK(r == 0, "uploaded into slot 3 (19 pieces)");
    anim_list(&valid, &used);
    CHECK(valid == ((1u << 0) | (1u << 3)) && used == ((1u << 0) | (7u << 3)), "slots 3-5 in use");
    play(3, 10);
    CHECK(last_reply[2] == 1, "play slot 3");
    CHECK(play_matches(&B, "plasma") == 0, "every frame on time and pixel-exact");
    play(ANIM_KEEP, 5);
    {
        vec_t slow = B;
        static uint16_t d2[100];
        for (unsigned i = 0; i < B.frames; i++) d2[i] = (uint16_t)(B.del[i] * 2u);
        slow.del = d2;
        play(3, 5);
        CHECK(play_matches(&slow, "") == 0, "speed 0.5: every frame twice as long");
        play(3, 10);
    }

    printf("4. power cycle\n");
    idle(4000);
    reboot(1);
    idle(2500);
    status(st);
    anim_list(&valid, &used);
    CHECK(valid == ((1u << 0) | (1u << 3)), "both still there after the boot scan");
    CHECK(st[0] == MODE_ANIM && st[1] == 3, "back to playing slot 3");

    printf("5. a new upload over part of an old one replaces it\n");
    r = upload(4, C.opa, C.len, 0, -1);
    CHECK(r == 0, "uploaded into slot 4 (inside slot 3's animation)");
    anim_list(&valid, &used);
    CHECK(valid == ((1u << 0) | (1u << 4)) && used == ((1u << 0) | (1u << 4)), "slot 3's animation is gone");
    reboot(1);
    anim_list(&valid, &used);
    CHECK(valid == ((1u << 0) | (1u << 4)), "and stays gone after a power cycle");
    play(4, 10);
    CHECK(play_matches(&C, "still") == 0, "the still picture shows");

    printf("6. an upload that stops half way never becomes valid\n");
    r = upload(8, B.opa, B.len, 0, 7);
    anim_list(&valid, &used);
    CHECK(r == -1 && last_reply[13] == 1 && !(valid & (1u << 8)), "7 of 19 pieces: not listed, upload running");
    idle(16000);
    anim_list(&valid, &used);
    CHECK(last_reply[13] == 0, "given up after 15 s without data");
    reboot(1);
    anim_list(&valid, &used);
    CHECK(!(valid & (1u << 8)), "not valid after a power cycle either");

    printf("7. wrong requests\n");
    uint8_t b[6] = { 19 };
    put32(b + 1, B.len); b[5] = 0;
    cmd(ANIM_CMD_BEGIN, b, 6);
    CHECK(last_reply[2] == 0, "too big for the slots left: refused");
    b[0] = 20; put32(b + 1, 1000);
    cmd(ANIM_CMD_BEGIN, b, 6);
    CHECK(last_reply[2] == 0, "slot 20: refused");
    b[0] = 9; put32(b + 1, A.len);
    cmd(ANIM_CMD_BEGIN, b, 6);
    static uint8_t pk[8 + 4096];
    put32(pk, 4096);
    memcpy(pk + 4, A.opa, A.len);
    cmd(ANIM_CMD_DATA, pk, 4 + A.len);
    CHECK(last_reply[2] == 0 && last_reply[7] == 6, "wrong offset refused");
    put32(pk, 0);
    memcpy(pk + 4, A.opa, A.len);
    pk[4 + 28] = 0x44;                                     /* state must be FFFFFFFF as sent */
    cmd(ANIM_CMD_DATA, pk, 4 + A.len);
    CHECK(last_reply[2] == 0 && last_reply[7] == 8, "header with the state already set refused");
    cmd(ANIM_CMD_BEGIN, b, 6);
    memcpy(pk + 4, A.opa, A.len);
    pk[4 + 200] ^= 0x55;                                   /* damaged on the way: CRC check after the write */
    cmd(ANIM_CMD_DATA, pk, 4 + A.len);
    anim_list(&valid, &used);
    CHECK(last_reply[2] == 1 && !(valid & (1u << 9)), "damaged data: refused after the read-back check");
    cmd(ANIM_CMD_DATA, pk, 4 + A.len);
    CHECK(last_reply[2] == 0 && last_reply[7] == 5, "piece without an upload refused");
    uint8_t s9 = 9;
    q[0] = 9; q[1] = 0; cmd(ANIM_CMD_INFO, q, 2);
    CHECK(last_reply[2] == 0, "info on an empty slot: status 0");
    play(s9, 10);
    CHECK(last_reply[2] == 0, "play on an empty slot refused");

    printf("8. damaged flash is found at boot\n");
    flash_mem[ANIM_BASE - FL_BASE + 0 * ANIM_SLOT_SIZE + 300] ^= 0x01;
    reboot(1);
    anim_list(&valid, &used);
    CHECK(valid == (1u << 4), "slot 0 left out (CRC), slot 4 kept");

    printf("9. play all, delete\n");
    CHECK(upload(10, A.opa, A.len, 0, -1) == 0 && upload(12, B.opa, B.len, 0, -1) == 0, "two more uploaded");
    play(ANIM_ALL, 10);
    unsigned seen = 0;
    for (int i = 0; i < 70 * 40; i++) { idle(25); status(st); if (st[1] < 20) seen |= 1u << st[1]; }
    CHECK(seen == ((1u << 4) | (1u << 10) | (1u << 12)) && (st[7] & 16), "all three played in turn within 70 s");
    uint8_t d = 10;
    cmd(ANIM_CMD_DELETE, &d, 1);
    anim_list(&valid, &used);
    CHECK(last_reply[2] == 1 && valid == ((1u << 4) | (1u << 12)), "slot 10 deleted");
    reboot(1);
    anim_list(&valid, &used);
    CHECK(valid == ((1u << 4) | (1u << 12)), "still deleted after a power cycle");
    uint8_t a5 = 0xA5; cmd(0xF108, &a5, 1);
    anim_list(&valid, &used);
    CHECK(valid == ((1u << 4) | (1u << 12)), "factory settings keep the animations");
    d = ANIM_ALL;
    cmd(ANIM_CMD_DELETE, &d, 1);
    anim_list(&valid, &used);
    CHECK(valid == 0, "delete all");
    uint8_t m[2] = { MODE_ANIM, ANIM_ALL };
    cmd(0xF101, m, 2);
    modes_render(&fb, now);
    int lit = 0;
    for (unsigned i = 0; i < sizeof fb; i++) lit += ((uint8_t *)fb)[i] != 0;
    CHECK(lit > 0, "nothing stored: a placeholder instead of a black panel");
    CHECK(fl_bad == 0, "never programmed over unerased bytes");

    fclose(out);
    printf("%s: %d failed\n", nfail ? "ANIM TEST FAILED" : "anim test passed", nfail);
    return nfail ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "settings") == 0) return settings_test();
    if (argc > 2 && strcmp(argv[1], "anim") == 0) return anim_test(argv[2]);
    out = fopen("frames.bin", "wb");
    static const uint32_t S5[5] = { 1000, 2000, 3500, 5000, 6000 };
    static const uint32_t SPL[3] = { 300, 900, 1500 };

    modes_init(now);
    run("splash", 1600, SPL, 3);

    uint8_t off[3] = { 0, 20, 0 };
    cmd(0xF104, off, 3);                                         /* autoplay off */
    for (unsigned e = 0; e < effect_count; e++) {
        uint8_t slot[3] = { 1, 0, (uint8_t)e };
        cmd(0x8008, slot, 3);                                    /* vendor app's "show slot" command */
        run(effects[e].name, 6000, S5, 5);
    }

    {                                                            /* text engine (0xF10B) */
        static const char *const FXN[23] = { "none", "scroll L", "scroll R", "scroll U", "scroll D", "typewriter", "wipe",
            "opening", "closing", "blinds", "dissolve", "mesh", "slice", "drop", "grow up", "grow down", "zoom", "fade",
            "rain", "explode", "laser", "flip", "matrix" };
        static const uint32_t TS[6] = { 300, 650, 2000, 3300, 3600, 3900 };
        for (int fx = 1; fx <= 22; fx++) {                       /* every entry + exit, "HELLO" in the panel font */
            uint8_t pk[3 + 16 + 1 + 8] = { 0, 1, 1, (uint8_t)fx, 0, (uint8_t)fx, 1, 255, 255, 255, 255, 255, 255, 5, 0, 0, 0, 0, 1 };
            pk[19] = 0; memcpy(pk + 20, "HELLO", 5);
            cmd(0xF10B, pk, 25);
            char name[32]; snprintf(name, sizeof name, "text %s", FXN[fx]);
            run(name, 4000, TS, 6);
        }
        static const char *const HN[11] = { "still", "blink", "breathe", "wave", "bounce", "shake", "shine", "sparkle", "flicker", "jelly", "glitch" };
        static const uint32_t HS[3] = { 1300, 1800, 2400 };
        for (int h = 1; h <= 10; h++) {                          /* hold effects, colour modes in turn, outline */
            uint8_t pk[3 + 16 + 1 + 8] = { 0, 1, 1, 0, (uint8_t)h, 0, (uint8_t)(h % 9), 255, 120, 0, 60, 120, 255, 5, 12, 0x20, 0, 0, 1 };
            pk[19] = 1; memcpy(pk + 20, "HI", 2);
            cmd(0xF10B, pk, 22);
            char name[32]; snprintf(name, sizeof name, "hold %s c%d", HN[h], h % 9);
            run(name, 2500, HS, 3);
        }
        static const uint32_t BS[4] = { 1500, 3000, 6000, 9000 };
        {                                                        /* over a background animation, live time */
            uint8_t pk[3 + 16 + 1 + 17] = { 0, 1, 1, 1, 3, 0, 0, 255, 255, 255, 0, 0, 0, 5, 0, 0x60, 2 + 15, 110, 1 };
            pk[19] = 0; memcpy(pk + 20, "TIME {time} {day}", 17);
            cmd(0xF10B, pk, 37);
            run("text marquee over lava", 9100, BS, 4);
        }
        {                                                        /* strip (as the web UI sends), right to left, fire */
            uint8_t sp[3 + 16 + 3 + 2 * 96] = { 0, 1, 0, 2, 0, 0, 5, 255, 255, 255, 255, 255, 255, 5, 0, 0x14, 0, 0, 1 };
            int w = make_strip(sp + 22, 96, "right to left");
            sp[19] = STRIP_H; sp[20] = (uint8_t)w; sp[21] = 0;
            cmd(0xF10B, sp, 22 + 2 * (unsigned)w);
            run("text strip marquee rtl fire", 9100, BS, 4);
        }
    }

    modes_set_time(12, 34, 56, now);
    for (uint8_t st = 0; st < CLOCK_STYLES; st++) {
        uint8_t c[7] = { st, 1, 1, 26, 11, 22, 7 };              /* pypixelcolor set_clock_mode */
        cmd(0x0106, c, 7);
        char name[16]; snprintf(name, sizeof name, "clock %u", st);
        run(name, 6000, S5, 5);
    }

    {                                                            /* sky clock through the day, flip mid-flip */
        static const uint8_t T[4][2] = { { 6, 30 }, { 9, 10 }, { 18, 50 }, { 23, 10 } };
        static const uint32_t ONE[1] = { 100 };
        for (int k = 0; k < 4; k++) {
            modes_set_time(T[k][0], T[k][1], 0, now);
            uint8_t c[7] = { 8, 1, 1, 26, 11, 22, 7 };
            cmd(0x0106, c, 7);
            char name[16]; snprintf(name, sizeof name, "sky %02u:%02u", T[k][0], T[k][1]);
            run(name, 150, ONE, 1);
        }
        static const uint32_t FL[5] = { 900, 1080, 1150, 1260, 1400 };
        modes_set_time(12, 34, 59, now);
        uint8_t c[7] = { 5, 1, 1, 26, 11, 22, 7 };
        cmd(0x0106, c, 7);
        run("flip at :00", 1450, FL, 5);
    }

    const char *msg = "Hello panel! 123";
    uint8_t tx[64] = { 255, 200, 80, 5, 1 };
    memcpy(tx + 5, msg, strlen(msg));
    cmd(0xF102, tx, 5 + (unsigned)strlen(msg));
    run("text", 6000, S5, 5);

    uint8_t sc[3] = { 7, 0, 12 };
    cmd(0x800A, sc, 3);
    run("score", 2000, S5, 2);

    uint8_t tstart = 1;
    cmd(0x8009, &tstart, 1);
    run("timer", 6000, S5, 5);

    uint8_t rh[12] = { 0, 2, 5, 9, 13, 15, 12, 8, 6, 3, 1, 0 };
    for (uint8_t st = 0; st < 3; st++) {
        rh[0] = st;
        cmd(0x0201, rh, 12);
        char name[16]; snprintf(name, sizeof name, "rhythm %u", st);
        run(name, 1000, S5, 1);
    }

    uint8_t fun = 1;
    cmd(0x0104, &fun, 1);                                        /* fun mode: clear canvas */
    uint8_t px[5 * 64]; unsigned n = 0;
    for (int i = 0; i < 32; i++) {                               /* a smiley-ish drawing via F105 */
        px[n++] = (uint8_t)i; px[n++] = (uint8_t)i; px[n++] = 255; px[n++] = 0; px[n++] = 0;
        px[n++] = (uint8_t)(31 - i); px[n++] = (uint8_t)i; px[n++] = 0; px[n++] = 0; px[n++] = 255;
    }
    cmd(0xF105, px, n);
    uint8_t one[6] = { 0, 0, 255, 0, 16, 16 };                   /* pypixelcolor set_pixel */
    cmd(0x0105, one, 6);
    static const uint32_t S1[1] = { 200 };
    run("canvas", 500, S1, 1);

    uint8_t tp[2] = { MODE_TEST, 0 };
    cmd(0xF101, tp, 2);
    run("test 0", 500, S1, 1);

    cmd(0xF106, 0, 0);                                            /* status reply */
    fclose(out);
    printf("frames written\n");
    return 0;
}

/* Animation store and player.
 *
 * File format "OPA1" (little-endian, made by webui/anim.js; the same file is what is stored in flash):
 *    0  u32 magic "OPA1"          4  u8 w, u8 h (32, 32)       6  u16 frames
 *    8  u16 colours (1..256)     10  u8 flags (0), u8 0       12  u32 length of the whole file
 *   16  u32 CRC-32 of bytes [64, length)                      20  u32 0, u32 0
 *   28  u32 state: sent as FFFFFFFF; the panel writes "DONE" once the whole file is in flash and checked
 *   32  name, UTF-8, NUL-padded (32 bytes, the panel reports the first 27)
 *   64  palette: colours x (r g b)
 *       frames: u16 delay_ms, u16 n, n bytes of drawing ops on a 32x32 grid of palette indexes,
 *       applied from pixel 0 on, row by row:
 *         00cccccc  skip c+1 pixels (they keep the previous frame)
 *         01cccccc  v           c+1 pixels of colour v
 *         10cccccc  v1 .. vc+1  c+1 pixels, one colour each
 *       Pixels after the last op keep the previous frame. Before the first frame (and on every loop) the
 *       grid is all colour 0.
 *
 * Upload: 0xF10C begin (slot, length, flags), then 0xF10D with 4 KiB pieces in order. Each piece is written
 * at once (erase + program, the panel goes dark for ~0.5 s); after the last one the panel reads the file back,
 * checks the CRC and only then writes the state word. A half-sent file never becomes valid.
 * Every read of stored data is bounds-checked, so a damaged file can only show wrong pixels.
 * The flash cache can return bytes from before a write, so the final CRC check and the start of playback
 * first push the cache out (app_flash_forget); the directory of what is stored lives in RAM.
 */
#include "anim.h"
#include "gfx.h"
#include "app_port.h"

#define MAGIC        0x3141504Fu        /* "OPA1" */
#define STATE_DONE   0x454E4F44u        /* "DONE" */
#define HDR          64u
#define H_FRAMES     6u
#define H_NCOL       8u
#define H_LEN        12u
#define H_CRC        16u
#define H_STATE      28u
#define H_NAME       32u
#define NAME_MAX     27u
#define PIECE        4096u
#define NPX          (ANIM_W * ANIM_H)
#define UPLOAD_TIMEOUT_MS 15000u
#define SWITCH_MS    12000u             /* "play all": next animation after this long (at the end of a round) */
#define MIN_DELAY_MS 20u

enum { R_OK = 0, R_MORE, R_DONE, R_ARG, R_BUSY, R_NOUP, R_OFFSET, R_LENGTH, R_HEADER, R_FLASH, R_VERIFY };

typedef struct {
    uint32_t len, crc;                  /* len 0 = nothing starts in this slot */
    uint16_t frames, ncol;
    char     name[NAME_MAX];
} dir_t;

static dir_t   dir[ANIM_SLOTS];
static uint8_t ready;

static struct {
    uint8_t  active, slot, flags;
    uint16_t frames, ncol;
    uint32_t len, next, deadline, crc;
    char     name[NAME_MAX];
} up;

static struct {
    uint8_t  slot, all;
    uint16_t frame, delay;
    uint32_t off, due, since;
    uint8_t  idx[NPX];
} pl = { .slot = 0xFF };

/* source buffers for flash writes must be in RAM, never in flash */
static uint32_t ram_done = STATE_DONE;
static uint32_t ram_ff = 0xFFFFFFFFu;

/* ------------------------------------------------------------------------------------------------ */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static uint32_t crc32(const uint8_t *p, uint32_t n)      /* zlib CRC-32, 16-entry table */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; c = (c >> 4) ^ T[c & 15u]; c = (c >> 4) ^ T[c & 15u]; }
    return ~c;
}

static uint32_t slot_addr(unsigned s) { return ANIM_BASE + s * ANIM_SLOT_SIZE; }
static const uint8_t *slot_ptr(unsigned s) { return app_flash_ptr(slot_addr(s)); }
static unsigned nslots(uint32_t len) { return (len + ANIM_SLOT_SIZE - 1u) / ANIM_SLOT_SIZE; }

/* header plausible for a file starting in slot s; done = 1: complete (state DONE), 0: as sent (state FF..) */
static int hdr_ok(const uint8_t *h, unsigned s, int done)
{
    uint32_t frames = rd16(h + H_FRAMES), ncol = rd16(h + H_NCOL), len = rd32(h + H_LEN);
    if (rd32(h) != MAGIC || h[4] != ANIM_W || h[5] != ANIM_H) return 0;
    if (frames == 0 || ncol == 0 || ncol > 256u) return 0;
    if (len < HDR + 3u * ncol + 4u * frames || len > (ANIM_SLOTS - s) * ANIM_SLOT_SIZE) return 0;
    return rd32(h + H_STATE) == (done ? STATE_DONE : 0xFFFFFFFFu);
}

static void set_dir(unsigned s, uint32_t len, uint32_t crc, uint32_t frames, uint32_t ncol, const uint8_t *name)
{
    dir[s].len = len; dir[s].crc = crc; dir[s].frames = (uint16_t)frames; dir[s].ncol = (uint16_t)ncol;
    for (unsigned i = 0; i < NAME_MAX; i++) dir[s].name[i] = (char)name[i];
}

void anim_init(void)
{
    for (unsigned i = 0; i < ANIM_SLOTS; i++) dir[i].len = 0;
    up.active = 0;
    pl.slot = 0xFF;
    for (unsigned i = 0; i < ANIM_SLOTS; ) {
        const uint8_t *h = slot_ptr(i);
        if (hdr_ok(h, i, 1)) {
            uint32_t len = rd32(h + H_LEN);
            if (crc32(h + HDR, len - HDR) == rd32(h + H_CRC)) {
                set_dir(i, len, rd32(h + H_CRC), rd16(h + H_FRAMES), rd16(h + H_NCOL), h + H_NAME);
                i += nslots(len);
                continue;
            }
        }
        i++;
    }
    ready = 1;
}

int anim_valid(unsigned slot)
{
    if (slot == ANIM_ALL) {
        for (unsigned i = 0; i < ANIM_SLOTS; i++) if (dir[i].len) return 1;
        return 0;
    }
    return slot < ANIM_SLOTS && dir[slot].len != 0;
}

/* first valid slot after `after` (0xFF: from the start), wrapping; -1 = none */
static int next_valid(unsigned after, int wrap)
{
    unsigned start = after == 0xFFu ? 0u : after + 1u;
    for (unsigned i = start; i < ANIM_SLOTS; i++) if (dir[i].len) return (int)i;
    if (wrap) for (unsigned i = 0; i < start && i < ANIM_SLOTS; i++) if (dir[i].len) return (int)i;
    return -1;
}

/* everything overlapping slots [a, b) is about to be overwritten. A file starting before a keeps its header,
 * so it is erased (one sector) or it would come back after a power cycle with a damaged tail. */
static int forget_range(unsigned a, unsigned b)
{
    int rc = 0;
    for (unsigned j = 0; j < ANIM_SLOTS; j++) {
        if (!dir[j].len) continue;
        unsigned e = j + nslots(dir[j].len);
        if (j >= b || e <= a) continue;
        dir[j].len = 0;
        if (j < a && app_flash_write_sector(slot_addr(j), &ram_ff, 4) != 0) rc = -1;
    }
    return rc;
}

/* ---- upload --------------------------------------------------------------------------------------- */
void anim_abort_upload(void) { up.active = 0; }
int  anim_uploading(void)    { return up.active; }

void anim_tick(uint32_t now)
{
    if (up.active && (int32_t)(now - up.deadline) >= 0) up.active = 0;
}

static int begin(unsigned slot, uint32_t len, unsigned flags, uint32_t now)
{
    if (!ready) return R_BUSY;
    if (slot >= ANIM_SLOTS || len < HDR + 3u + 4u || len > (ANIM_SLOTS - slot) * ANIM_SLOT_SIZE) return R_ARG;
    up.active = 0;
    if (forget_range(slot, slot + nslots(len)) != 0) return R_FLASH;
    up.slot = (uint8_t)slot;
    up.flags = (uint8_t)flags;
    up.len = len;
    up.next = 0;
    up.deadline = now + UPLOAD_TIMEOUT_MS;
    up.active = 1;
    return R_OK;
}

static int piece(uint32_t off, const uint8_t *d, unsigned n, uint32_t now)
{
    if (!up.active) return R_NOUP;
    if (off != up.next) return R_OFFSET;
    uint32_t want = up.len - off < PIECE ? up.len - off : PIECE;
    if (n != want) return R_LENGTH;
    if (off == 0) {
        if (n < HDR || !hdr_ok(d, up.slot, 0) || rd32(d + H_LEN) != up.len) { up.active = 0; return R_HEADER; }
        up.crc = rd32(d + H_CRC); up.frames = (uint16_t)rd16(d + H_FRAMES); up.ncol = (uint16_t)rd16(d + H_NCOL);
        for (unsigned i = 0; i < NAME_MAX; i++) up.name[i] = (char)d[H_NAME + i];
    }
    if (app_flash_write_sector(slot_addr(up.slot) + off, d, n) != 0) { up.active = 0; return R_FLASH; }
    up.next += n;
    up.deadline = now + UPLOAD_TIMEOUT_MS;
    if (up.next < up.len) return R_MORE;

    up.active = 0;                                          /* complete: check what is really in flash */
    app_flash_forget();                                     /* not what the cache kept from before */
    if (crc32(slot_ptr(up.slot) + HDR, up.len - HDR) != up.crc) return R_VERIFY;
    if (app_flash_program(slot_addr(up.slot) + H_STATE, &ram_done, 4) != 0) return R_FLASH;
    set_dir(up.slot, up.len, up.crc, up.frames, up.ncol, (const uint8_t *)up.name);
    return R_DONE;
}

static int delete_slot(unsigned s)
{
    if (!dir[s].len) return 0;
    dir[s].len = 0;
    return app_flash_write_sector(slot_addr(s), &ram_ff, 4);
}

/* 0xF10E reply: slots, slot_kb, valid(4) used(4), flags (bit 0: upload running) */
static void reply_list(void)
{
    uint8_t x[11] = { ANIM_SLOTS, ANIM_SLOT_SIZE / 1024u };
    uint32_t valid = 0, used = 0;
    for (unsigned i = 0; i < ANIM_SLOTS; i++) {
        if (!dir[i].len) continue;
        valid |= 1u << i;
        for (unsigned k = i; k < i + nslots(dir[i].len) && k < ANIM_SLOTS; k++) used |= 1u << k;
    }
    wr32(x + 2, valid);
    wr32(x + 6, used);
    x[10] = up.active ? 1u : 0u;
    app_reply(ANIM_CMD_LIST, ready, x, sizeof x);
}

/* 0xF10F slot part -> part 0: slot 0 frames(2) len(3) crc(4); parts 1-3: slot part name[9] */
static void reply_info(unsigned s, unsigned part)
{
    uint8_t x[11] = { (uint8_t)s, (uint8_t)part };
    int ok = s < ANIM_SLOTS && dir[s].len && part <= 3u;
    if (ok && part == 0) {
        x[2] = (uint8_t)dir[s].frames; x[3] = (uint8_t)(dir[s].frames >> 8);
        x[4] = (uint8_t)dir[s].len; x[5] = (uint8_t)(dir[s].len >> 8); x[6] = (uint8_t)(dir[s].len >> 16);
        wr32(x + 7, dir[s].crc);
    } else if (ok) {
        for (unsigned i = 0; i < 9; i++) x[2 + i] = (uint8_t)dir[s].name[(part - 1u) * 9u + i];
    }
    app_reply(ANIM_CMD_INFO, ok ? 1u : 0u, x, sizeof x);
}

int anim_cmd(uint16_t cmd, const uint8_t *p, unsigned len, uint32_t now)
{
    switch (cmd) {
    case ANIM_CMD_BEGIN: {                                  /* slot len(4) flags (bit 0: play when done) */
        uint8_t r = (uint8_t)(len < 10 ? R_ARG : begin(p[4], rd32(p + 5), p[9], now));
        app_reply(cmd, r == R_OK ? 1u : 0u, &r, 1);
        return 2; }
    case ANIM_CMD_DATA: {                                   /* off(4) data: reply status off(4) reason */
        uint32_t off = len >= 8 ? rd32(p + 4) : 0;
        int r = len >= 8 ? piece(off, p + 8, len - 8, now) : R_LENGTH;
        uint8_t x[5];
        wr32(x, off);
        x[4] = (uint8_t)r;
        app_reply(cmd, r == R_MORE ? 1u : r == R_DONE ? 2u : 0u, x, sizeof x);
        if (r == R_DONE && (up.flags & 1u)) return ANIM_PLAY_REQ | up.slot;
        return 2; }
    case ANIM_CMD_LIST:
        reply_list();
        return 2;
    case ANIM_CMD_INFO:
        reply_info(len >= 5 ? p[4] : 0xFFu, len >= 6 ? p[5] : 0u);
        return 2;
    case ANIM_CMD_DELETE: {                                 /* slot, or ANIM_ALL */
        int ok = ready && len >= 5 && !up.active && (p[4] == ANIM_ALL || p[4] < ANIM_SLOTS);
        for (unsigned s = 0; ok && s < ANIM_SLOTS; s++)
            if ((p[4] == ANIM_ALL || p[4] == s) && delete_slot(s) != 0) ok = 0;
        app_reply(cmd, ok ? 1u : 0u, 0, 0);
        return 2; }
    default:
        return -1;
    }
}

/* ---- player --------------------------------------------------------------------------------------- */
static void pl_rewind(void)
{
    pl.frame = 0;
    pl.off = HDR + 3u * dir[pl.slot].ncol;
    for (unsigned i = 0; i < NPX; i++) pl.idx[i] = 0;
}

void anim_start(unsigned slot, uint32_t now)
{
    pl.all = slot == ANIM_ALL;
    int s = pl.all ? next_valid(0xFFu, 0) : (slot < ANIM_SLOTS && dir[slot].len ? (int)slot : -1);
    pl.slot = s < 0 ? 0xFFu : (uint8_t)s;
    pl.due = now;
    pl.since = now;
    if (s >= 0) { app_flash_forget(); pl_rewind(); }        /* the slot may have been rewritten since it was read */
}

unsigned anim_showing(void) { return pl.slot; }

int anim_next(uint32_t now)
{
    int s = next_valid(pl.slot, 0);
    if (s < 0) return 0;
    int all = pl.all;
    anim_start((unsigned)s, now);
    pl.all = (uint8_t)all;
    return 1;
}

/* decode the next frame into pl.idx; 0 = ok, -1 = damaged */
static int decode_frame(void)
{
    const dir_t *e = &dir[pl.slot];
    const uint8_t *b = slot_ptr(pl.slot);
    if (pl.frame >= e->frames) pl_rewind();
    uint32_t off = pl.off;
    if (off + 4u > e->len) return -1;
    uint32_t delay = rd16(b + off), n = rd16(b + off + 2), end = off + 4u + n;
    if (end > e->len) return -1;
    const uint8_t *p = b + off + 4, *q = b + end;
    unsigned i = 0;
    while (p < q) {
        unsigned op = *p++, cnt = (op & 63u) + 1u;
        if (i + cnt > NPX) return -1;
        switch (op >> 6) {
        case 0: i += cnt; break;
        case 1: { if (p >= q) return -1; uint8_t v = *p++; while (cnt--) pl.idx[i++] = v; break; }
        case 2: if ((uint32_t)(q - p) < cnt) return -1; while (cnt--) pl.idx[i++] = *p++; break;
        default: return -1;
        }
    }
    pl.off = end;
    pl.frame++;
    pl.delay = (uint16_t)delay;
    return 0;
}

static void draw_empty(uint32_t now)
{
    rgb_t c = gfx_hsv((uint8_t)(now / 40u), 120, 90);
    gfx_fill(0);
    gfx_rect(6, 5, 20, 14, c, 0);                           /* a film frame */
    for (int y = 7; y < 18; y += 3) { gfx_px(8, y, c); gfx_px(23, y, c); }
    gfx_tiny_text((32 - gfx_tiny_width("NO ANIM")) / 2, 23, "NO ANIM", RGB(110, 110, 120));
}

void anim_draw(uint32_t now, unsigned speed10)
{
    if (pl.slot == 0xFF || !dir[pl.slot].len) {             /* nothing chosen, or it was deleted / overwritten */
        int s = next_valid(pl.slot, 1);
        if (s < 0) { pl.slot = 0xFF; draw_empty(now); return; }
        int all = pl.all;
        anim_start((unsigned)s, now);
        pl.all = (uint8_t)all;
    }
    if ((int32_t)(now - pl.due) >= 0) {
        if (pl.all && pl.frame >= dir[pl.slot].frames && now - pl.since >= SWITCH_MS) {
            int s = next_valid(pl.slot, 1);
            if (s >= 0 && (unsigned)s != pl.slot) { anim_start((unsigned)s, now); pl.all = 1; }
            else pl.since = now;
        }
        if (decode_frame() != 0) { dir[pl.slot].len = 0; gfx_fill(0); return; }   /* damaged: leave it out */
        if (speed10 < 1u) speed10 = 10u;
        uint32_t d = (uint32_t)pl.delay * 10u / speed10;
        if (d < MIN_DELAY_MS) d = MIN_DELAY_MS;
        pl.due += d;
        if ((int32_t)(now - pl.due) > 1000) pl.due = now + d;    /* fell behind (e.g. a flash write): no catch-up burst */
    }
    const uint8_t *pal = slot_ptr(pl.slot) + HDR;
    unsigned ncol = dir[pl.slot].ncol;
    fb_t *f = gfx_fb();
    for (unsigned i = 0; i < NPX; i++) {
        unsigned v = pl.idx[i];
        uint8_t *px = (*f)[i / ANIM_W][i % ANIM_W];
        if (v < ncol) { px[0] = pal[3 * v]; px[1] = pal[3 * v + 1]; px[2] = pal[3 * v + 2]; }
        else px[0] = px[1] = px[2] = 0;
    }
}

void anim_draw_upload(void)
{
    uint32_t pct = up.len ? up.next * 100u / up.len : 0;          /* len <= 640 KiB: no overflow */
    char num[5];
    unsigned n = 0;
    if (pct >= 100u) num[n++] = '1';
    if (pct >= 10u) num[n++] = (char)('0' + (pct / 10u) % 10u);
    num[n++] = (char)('0' + pct % 10u);
    num[n++] = '%';
    num[n] = 0;
    gfx_fill(0);
    gfx_text((32 - gfx_text_width("SAVE")) / 2, 4, "SAVE", RGB(120, 170, 255));
    gfx_rect(2, 15, 28, 5, RGB(70, 80, 100), 0);
    if (pct) gfx_rect(3, 16, (int)(pct * 26u / 100u) ? (int)(pct * 26u / 100u) : 1, 3, RGB(90, 220, 140), 1);
    gfx_tiny_text((32 - gfx_tiny_width(num)) / 2, 23, num, RGB(200, 200, 210));
}

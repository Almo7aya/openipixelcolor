/* Animation store and player.
 *
 * File format "OPA1" (little-endian, made by webui/js/create/anim-codec.js; the same file is what is stored
 * in flash):
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
 * Where: the external flash when the panel has one (docs/HARDWARE.md). On the first start the whole chip is
 * erased, in the background (10-40 s); sector 0 then stays erased (the vendor firmware keeps its picture
 * list there: erased = no pictures), a marker in sector 1 says the chip is ours, and 127 slots of 32 KiB
 * follow. Animations that older versions stored in the internal flash are then copied to the same slots,
 * checked, and only then removed from there. Without the chip: 20 slots in the internal flash, as before.
 *
 * Upload: 0xF10C begin (slot, length, flags), then 0xF10D with 4 KiB pieces in order. Each piece is written
 * at once; after the last one the panel reads the file back, checks the CRC and only then writes the state
 * word. A half-sent file never becomes valid. Every read of stored data is bounds-checked, so a damaged file
 * can only show wrong pixels. The internal flash's cache can return bytes from before a write, so reading
 * back first pushes it out (st_forget). In RAM: each slot's length (the directory), and while one plays,
 * its palette and the frame being drawn; everything else is read from flash when needed.
 */
#include "anim.h"
#include "gfx.h"
#include "screens.h"
#include "app_port.h"

#define MAGIC        0x3141504Fu        /* "OPA1" */
#define STATE_DONE   0x454E4F44u        /* "DONE" */
#define XMAGIC       0x5358504Fu        /* "OPXS": the external flash is set up as our store */
#define XLAYOUT      ((ANIM_XBASE >> 12) | (ANIM_SLOTS_MAX << 8) | ((ANIM_SLOT_SIZE >> 12) << 16) | (1u << 24))
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
#define ERASE_LIMIT_MS   120000u        /* the whole chip: typically 10-40 s */
#define SWITCH_MS    12000u             /* "play all": next animation after this long (at the end of a round) */
#define MIN_DELAY_MS 20u

enum { R_OK = 0, R_MORE, R_DONE, R_ARG, R_BUSY, R_NOUP, R_OFFSET, R_LENGTH, R_HEADER, R_FLASH, R_VERIFY };

static struct { uint8_t ext, n, prep; uint32_t prep_t0, poll; } st;   /* prep: erasing the external flash */
static uint32_t dlen[ANIM_SLOTS_MAX];   /* length of the file starting in each slot, 0 = none */
static uint8_t  ready;

static struct {
    uint8_t  active, slot, flags;
    uint32_t len, next, deadline, crc;
} up;

static struct {
    uint8_t  slot, all, want, pending;  /* want: asked for while the store wasn't ready (pending) */
    uint16_t frame, delay, frames, ncol;
    uint32_t off, due, since, len;
    uint8_t  idx[NPX];
    uint8_t  pal[3 * 256];
} pl = { .slot = 0xFF };

static struct { uint8_t show; uint32_t at, len; } saved;   /* the "saved" screen after an upload */

/* source buffers for internal flash writes must be in RAM, never in flash */
static uint32_t ram_done = STATE_DONE;
static uint32_t ram_ff = 0xFFFFFFFFu;
static uint32_t ram_mark[2] = { XMAGIC, XLAYOUT };

/* ------------------------------------------------------------------------------------------------ */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static uint32_t crc_upd(uint32_t c, const uint8_t *p, uint32_t n)   /* zlib CRC-32, 16-entry table */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };
    while (n--) { c ^= *p++; c = (c >> 4) ^ T[c & 15u]; c = (c >> 4) ^ T[c & 15u]; }
    return c;
}

/* ---- the store: offsets from slot 0, in whichever flash holds it ---- */
static uint32_t soff(unsigned s) { return s * ANIM_SLOT_SIZE; }
static unsigned nslots(uint32_t len) { return (len + ANIM_SLOT_SIZE - 1u) / ANIM_SLOT_SIZE; }

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (st.ext) return app_xflash_read(ANIM_XBASE + off, dst, n);
    const uint8_t *p = app_flash_ptr(ANIM_BASE + off);
    uint8_t *d = dst;
    while (n--) *d++ = *p++;
    return 0;
}
static int st_write_sector(uint32_t off, const void *src, uint32_t n)
{
    return st.ext ? app_xflash_write_sector(ANIM_XBASE + off, src, n) : app_flash_write_sector(ANIM_BASE + off, src, n);
}
static int st_program(uint32_t off, const void *src, uint32_t n)
{
    return st.ext ? app_xflash_program(ANIM_XBASE + off, src, n) : app_flash_program(ANIM_BASE + off, src, n);
}
static void st_forget(void) { if (!st.ext) app_flash_forget(); }

static int st_crc(uint32_t off, uint32_t n, uint32_t *crc)          /* CRC-32 of n stored bytes */
{
    uint8_t b[64];
    uint32_t c = 0xFFFFFFFFu;
    while (n) {
        uint32_t k = n < sizeof b ? n : sizeof b;
        if (st_read(off, b, k) != 0) return -1;
        c = crc_upd(c, b, k);
        off += k; n -= k;
    }
    *crc = ~c;
    return 0;
}

/* header plausible for a file starting in slot s of a store of n slots; done = 1: complete (state DONE),
 * 0: as sent (state FF..) */
static int hdr_ok_n(const uint8_t *h, unsigned s, unsigned n, int done)
{
    uint32_t frames = rd16(h + H_FRAMES), ncol = rd16(h + H_NCOL), len = rd32(h + H_LEN);
    if (rd32(h) != MAGIC || h[4] != ANIM_W || h[5] != ANIM_H) return 0;
    if (frames == 0 || ncol == 0 || ncol > 256u) return 0;
    if (s >= n || len < HDR + 3u * ncol + 4u * frames || len > (n - s) * ANIM_SLOT_SIZE) return 0;
    return rd32(h + H_STATE) == (done ? STATE_DONE : 0xFFFFFFFFu);
}
static int hdr_ok(const uint8_t *h, unsigned s, int done) { return hdr_ok_n(h, s, st.n, done); }

static void scan(void)
{
    uint8_t h[HDR];
    for (unsigned i = 0; i < ANIM_SLOTS_MAX; i++) dlen[i] = 0;
    st_forget();
    for (unsigned i = 0; i < st.n; ) {
        if (st_read(soff(i), h, HDR) == 0 && hdr_ok(h, i, 1)) {
            uint32_t len = rd32(h + H_LEN), crc;
            /* the internal store is checked in full (a big firmware update can reach it); the external one,
               which nothing else writes, by its header (reading 4 MiB would take seconds) */
            if (st.ext || (st_crc(soff(i) + HDR, len - HDR, &crc) == 0 && crc == rd32(h + H_CRC))) {
                dlen[i] = len;
                i += nslots(len);
                continue;
            }
        }
        i++;
    }
}

/* ---- the external flash: set up on the first start, old animations moved over ---- */
static void use_internal(void) { st.ext = 0; st.n = ANIM_SLOTS_INT; st.prep = 0; }

/* animations in the internal flash (stored by older versions) -> the same slots in the external flash */
static void move_internal(void)
{
    for (unsigned i = 0; i < ANIM_SLOTS_INT; ) {
        const uint8_t *h = app_flash_ptr(ANIM_BASE + soff(i));
        if (!hdr_ok_n(h, i, ANIM_SLOTS_INT, 1)) { i++; continue; }
        uint32_t len = rd32(h + H_LEN), crc;
        if (~crc_upd(0xFFFFFFFFu, h + HDR, len - HDR) == rd32(h + H_CRC)) {
            int bad = 0;
            for (uint32_t off = 0; off < len && !bad; off += PIECE)
                bad = app_xflash_write_sector(ANIM_XBASE + soff(i) + off, h + off, len - off < PIECE ? len - off : PIECE) != 0;
            if (bad || st_crc(soff(i) + HDR, len - HDR, &crc) != 0 || crc != rd32(h + H_CRC))
                app_xflash_write_sector(ANIM_XBASE + soff(i), &ram_ff, 4);        /* not a good copy: none */
        }
        i += nslots(len);
    }
}

/* old copies in the internal flash whose twin is in the external flash: removed (the header sector) */
static void tidy_internal(void)
{
    uint8_t x[HDR];
    for (unsigned i = 0; i < ANIM_SLOTS_INT; i++) {
        const uint8_t *h = app_flash_ptr(ANIM_BASE + soff(i));
        if (!hdr_ok_n(h, i, ANIM_SLOTS_INT, 1) || dlen[i] != rd32(h + H_LEN)) continue;
        if (st_read(soff(i), x, HDR) != 0 || rd32(x + H_CRC) != rd32(h + H_CRC)) continue;
        app_flash_write_sector(ANIM_BASE + soff(i), &ram_ff, 4);
    }
}

/* the external flash is erased: move the old animations in, then mark it ours */
static void prep_done(void)
{
    st.prep = 0;
    move_internal();
    if (app_xflash_program(ANIM_XMARK, ram_mark, sizeof ram_mark) != 0) use_internal();
    scan();
    if (st.ext) tidy_internal();
    ready = 1;
}

void anim_init(void)
{
    up.active = 0;
    pl.slot = 0xFF; pl.pending = 0;
    ready = 0;
    st.ext = app_xflash_size() >= ANIM_XBASE + ANIM_SLOTS_MAX * ANIM_SLOT_SIZE;
    st.n = st.ext ? ANIM_SLOTS_MAX : ANIM_SLOTS_INT;
    st.prep = 0;
    for (unsigned i = 0; i < ANIM_SLOTS_MAX; i++) dlen[i] = 0;
    if (st.ext) {
        uint8_t m[8];
        if (app_xflash_read(ANIM_XMARK, m, sizeof m) != 0) use_internal();
        else if (rd32(m) != XMAGIC || rd32(m + 4) != XLAYOUT) {        /* not ours yet: erase it all, then move in */
            if (app_xflash_erase_all() != 0) use_internal();
            else if (app_xflash_busy() == 0) { prep_done(); return; }  /* done at once (PC simulator, web preview) */
            else { st.prep = 1; st.prep_t0 = 0; return; }           /* the chip: anim_tick() waits for it */
        }
    }
    scan();
    if (st.ext) tidy_internal();
    ready = 1;
}

int anim_valid(unsigned slot)
{
    if (st.prep) return slot == ANIM_ALL || slot < ANIM_SLOTS_MAX;  /* the store comes back once it's ready */
    if (slot == ANIM_ALL) {
        for (unsigned i = 0; i < st.n; i++) if (dlen[i]) return 1;
        return 0;
    }
    return slot < st.n && dlen[slot] != 0;
}

/* first valid slot after `after` (0xFF: from the start), wrapping; -1 = none */
static int next_valid(unsigned after, int wrap)
{
    unsigned start = after == 0xFFu ? 0u : after + 1u;
    for (unsigned i = start; i < st.n; i++) if (dlen[i]) return (int)i;
    if (wrap) for (unsigned i = 0; i < start && i < st.n; i++) if (dlen[i]) return (int)i;
    return -1;
}

/* everything overlapping slots [a, b) is about to be overwritten. A file starting before a keeps its header,
 * so it is erased (one sector) or it would come back after a power cycle with a damaged tail. */
static int forget_range(unsigned a, unsigned b)
{
    int rc = 0;
    for (unsigned j = 0; j < st.n; j++) {
        if (!dlen[j]) continue;
        unsigned e = j + nslots(dlen[j]);
        if (j >= b || e <= a) continue;
        dlen[j] = 0;
        if (j < a && st_write_sector(soff(j), &ram_ff, 4) != 0) rc = -1;
    }
    return rc;
}

/* ---- upload --------------------------------------------------------------------------------------- */
void anim_abort_upload(void) { up.active = 0; }
int  anim_uploading(void)    { return up.active; }

void anim_tick(uint32_t now)
{
    if (up.active && (int32_t)(now - up.deadline) >= 0) up.active = 0;
    if (!st.prep || now - st.poll < 100u) return;
    st.poll = now;
    if (!st.prep_t0) st.prep_t0 = now | 1u;
    int b = app_xflash_busy();
    if (b == 1 && now - st.prep_t0 < ERASE_LIMIT_MS) return;
    if (b != 0) { use_internal(); scan(); ready = 1; return; }      /* no answer, or never done: internal store */
    prep_done();
}

static int begin(unsigned slot, uint32_t len, unsigned flags, uint32_t now)
{
    if (!ready) return R_BUSY;
    if (slot >= st.n || len < HDR + 3u + 4u || len > (st.n - slot) * ANIM_SLOT_SIZE) return R_ARG;
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
        up.crc = rd32(d + H_CRC);
    }
    if (st_write_sector(soff(up.slot) + off, d, n) != 0) { up.active = 0; return R_FLASH; }
    up.next += n;
    up.deadline = now + UPLOAD_TIMEOUT_MS;
    if (up.next < up.len) return R_MORE;

    up.active = 0;                                          /* complete: check what is really in flash */
    st_forget();                                            /* not what the cache kept from before */
    uint32_t crc;
    if (st_crc(soff(up.slot) + HDR, up.len - HDR, &crc) != 0 || crc != up.crc) return R_VERIFY;
    if (st_program(soff(up.slot) + H_STATE, &ram_done, 4) != 0) return R_FLASH;
    dlen[up.slot] = up.len;
    if (up.flags & 1u) { saved.show = 1; saved.at = now; saved.len = up.len; }     /* "saved" before it plays */
    return R_DONE;
}

static int delete_slot(unsigned s)
{
    if (!dlen[s]) return 0;
    dlen[s] = 0;
    return st_write_sector(soff(s), &ram_ff, 4);
}

/* 0xF10E [page] reply: slots, slot_kb, valid(4) used(4) for slots 32 * page .. 32 * page + 31,
 * flags (bit 0: upload running, bit 1: external flash, bit 2: still preparing it, bits 4-7: the page, so a
 * late duplicate reply can't pass for another page's) */
static void reply_list(unsigned page)
{
    uint8_t x[11] = { st.n, ANIM_SLOT_SIZE / 1024u };
    uint32_t valid = 0, used = 0, first = page * 32u;
    for (unsigned i = 0; i < st.n; i++) {
        if (!dlen[i]) continue;
        if (i >= first && i < first + 32u) valid |= 1u << (i - first);
        for (unsigned k = i; k < i + nslots(dlen[i]) && k < st.n; k++)
            if (k >= first && k < first + 32u) used |= 1u << (k - first);
    }
    wr32(x + 2, valid);
    wr32(x + 6, used);
    x[10] = (uint8_t)((up.active ? 1u : 0u) | (st.ext ? 2u : 0u) | (st.prep ? 4u : 0u) | ((page & 15u) << 4));
    app_reply(ANIM_CMD_LIST, ready, x, sizeof x);
}

/* 0xF10F slot part -> part 0: slot 0 frames(2) len(3) crc(4); parts 1-3: slot part name[9] */
static void reply_info(unsigned s, unsigned part)
{
    uint8_t x[11] = { (uint8_t)s, (uint8_t)part }, h[HDR];
    int ok = ready && s < st.n && dlen[s] && part <= 3u && st_read(soff(s), h, HDR) == 0;
    if (ok && part == 0) {
        x[2] = h[H_FRAMES]; x[3] = h[H_FRAMES + 1];
        x[4] = (uint8_t)dlen[s]; x[5] = (uint8_t)(dlen[s] >> 8); x[6] = (uint8_t)(dlen[s] >> 16);
        wr32(x + 7, rd32(h + H_CRC));
    } else if (ok) {
        for (unsigned i = 0; i < 9; i++) { unsigned k = (part - 1u) * 9u + i; x[2 + i] = k < NAME_MAX ? h[H_NAME + k] : 0; }
    }
    app_reply(ANIM_CMD_INFO, ok ? 1u : 0u, x, sizeof x);
}

/* 0xF112 slot off(4) -> off_lo + 10 bytes of the stored file from off (fewer at the end). Replies are at most
 * 16 bytes, so this is slow (~10 bytes a round trip): the web UI reads the colours and the first frame of
 * animations it didn't send itself, for a preview. off_lo tells a late duplicate reply from the answer. */
static void reply_read(unsigned s, uint32_t off)
{
    uint8_t x[11] = { (uint8_t)off };
    int ok = ready && s < st.n && dlen[s] && off < dlen[s];
    if (ok) {
        if (off == 0) st_forget();                          /* the slot may have been rewritten since it was read */
        uint32_t n = dlen[s] - off < 10u ? dlen[s] - off : 10u;
        ok = st_read(soff(s) + off, x + 1, n) == 0;          /* a file's slots are consecutive */
    }
    app_reply(ANIM_CMD_READ, ok ? 1u : 0u, x, sizeof x);
}

int anim_cmd(uint16_t cmd, const uint8_t *p, unsigned len, uint32_t now)
{
    switch (cmd) {
    case ANIM_CMD_READ:                                     /* slot off(4) */
        reply_read(len >= 5 ? p[4] : 0xFFu, len >= 9 ? rd32(p + 5) : 0);
        return 2;
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
    case ANIM_CMD_LIST:                                     /* [page] */
        reply_list(len >= 5 ? p[4] : 0u);
        return 2;
    case ANIM_CMD_INFO:
        reply_info(len >= 5 ? p[4] : 0xFFu, len >= 6 ? p[5] : 0u);
        return 2;
    case ANIM_CMD_DELETE: {                                 /* slot, or ANIM_ALL */
        int ok = ready && len >= 5 && !up.active && (p[4] == ANIM_ALL || p[4] < st.n);
        for (unsigned s = 0; ok && s < st.n; s++)
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
    pl.off = HDR + 3u * pl.ncol;
    for (unsigned i = 0; i < NPX; i++) pl.idx[i] = 0;
}

void anim_start(unsigned slot, uint32_t now)
{
    pl.all = slot == ANIM_ALL;
    pl.slot = 0xFF;
    pl.due = now;
    pl.since = now;
    if (!ready) { pl.want = (uint8_t)slot; pl.pending = 1; return; }   /* started once the store is ready */
    int s = pl.all ? next_valid(0xFFu, 0) : (slot < st.n && dlen[slot] ? (int)slot : -1);
    if (s < 0) return;
    uint8_t h[HDR];
    st_forget();                                            /* the slot may have been rewritten since it was read */
    if (st_read(soff((unsigned)s), h, HDR) != 0 || !hdr_ok(h, (unsigned)s, 1) ||
        st_read(soff((unsigned)s) + HDR, pl.pal, 3u * rd16(h + H_NCOL)) != 0) { dlen[s] = 0; return; }
    pl.slot = (uint8_t)s;
    pl.frames = (uint16_t)rd16(h + H_FRAMES);
    pl.ncol = (uint16_t)rd16(h + H_NCOL);
    pl.len = dlen[s];
    pl_rewind();
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

/* the ops of a frame, read through a small window */
typedef struct { uint32_t pos, end; unsigned i, n; uint8_t b[64]; } rdr_t;

static int rget(rdr_t *r)
{
    if (r->i >= r->n) {
        if (r->pos >= r->end) return -1;
        uint32_t k = r->end - r->pos < sizeof r->b ? r->end - r->pos : sizeof r->b;
        if (st_read(soff(pl.slot) + r->pos, r->b, k) != 0) return -1;
        r->pos += k; r->n = (unsigned)k; r->i = 0;
    }
    return r->b[r->i++];
}
static uint32_t rleft(const rdr_t *r) { return (r->n - r->i) + (r->end - r->pos); }

/* decode the next frame into pl.idx; 0 = ok, -1 = damaged */
static int decode_frame(void)
{
    if (pl.frame >= pl.frames) pl_rewind();
    uint32_t off = pl.off;
    uint8_t fh[4];
    if (off + 4u > pl.len || st_read(soff(pl.slot) + off, fh, 4) != 0) return -1;
    uint32_t delay = rd16(fh), n = rd16(fh + 2), end = off + 4u + n;
    if (end > pl.len) return -1;
    rdr_t r = { .pos = off + 4u, .end = end };
    unsigned i = 0;
    while (rleft(&r)) {
        int op = rget(&r);
        if (op < 0) return -1;
        unsigned cnt = ((unsigned)op & 63u) + 1u;
        if (i + cnt > NPX) return -1;
        switch ((unsigned)op >> 6) {
        case 0: i += cnt; break;
        case 1: { int v = rget(&r); if (v < 0) return -1; while (cnt--) pl.idx[i++] = (uint8_t)v; break; }
        case 2: if (rleft(&r) < cnt) return -1; while (cnt--) pl.idx[i++] = (uint8_t)rget(&r); break;
        default: return -1;
        }
    }
    pl.off = end;
    pl.frame++;
    pl.delay = (uint16_t)delay;
    return 0;
}

static void draw_empty(uint32_t now, int preparing)
{
    rgb_t c = gfx_hsv((uint8_t)(now / 40u), 120, 90);
    const char *t = preparing ? "WAIT" : "NO ANIM";
    gfx_fill(0);
    gfx_rect(6, 5, 20, 14, c, 0);                           /* a film frame */
    for (int y = 7; y < 18; y += 3) { gfx_px(8, y, c); gfx_px(23, y, c); }
    if (preparing) gfx_rect(10, 11, (int)(now / 150u % 12u), 2, c, 1);   /* busy */
    gfx_tiny_text((32 - gfx_tiny_width(t)) / 2, 23, t, RGB(110, 110, 120));
}

void anim_draw(uint32_t now, unsigned speed10)
{
    if (!ready) { draw_empty(now, 1); return; }             /* the external flash is being prepared */
    if (pl.pending) { pl.pending = 0; anim_start(pl.want, now); }   /* what was asked for meanwhile */
    if (pl.slot == 0xFF || !dlen[pl.slot]) {                /* nothing chosen, or it was deleted / overwritten */
        int s = next_valid(pl.slot, 1);
        if (s < 0) { pl.slot = 0xFF; draw_empty(now, 0); return; }
        int all = pl.all;
        anim_start((unsigned)s, now);
        pl.all = (uint8_t)all;
        if (pl.slot == 0xFF) { draw_empty(now, 0); return; }
    }
    if ((int32_t)(now - pl.due) >= 0) {
        if (pl.all && pl.frame >= pl.frames && now - pl.since >= SWITCH_MS) {
            int s = next_valid(pl.slot, 1);
            if (s >= 0 && (unsigned)s != pl.slot) { anim_start((unsigned)s, now); pl.all = 1; if (pl.slot == 0xFF) return; }
            else pl.since = now;
        }
        if (decode_frame() != 0) { dlen[pl.slot] = 0; gfx_fill(0); return; }   /* damaged: leave it out */
        if (speed10 < 1u) speed10 = 10u;
        uint32_t d = (uint32_t)pl.delay * 10u / speed10;
        if (d < MIN_DELAY_MS) d = MIN_DELAY_MS;
        pl.due += d;
        if ((int32_t)(now - pl.due) > 1000) pl.due = now + d;    /* fell behind (e.g. a flash write): no catch-up burst */
    }
    fb_t *f = gfx_fb();
    for (unsigned i = 0; i < NPX; i++) {
        unsigned v = pl.idx[i];
        uint8_t *px = (*f)[i / ANIM_W][i % ANIM_W];
        if (v < pl.ncol) { px[0] = pl.pal[3 * v]; px[1] = pl.pal[3 * v + 1]; px[2] = pl.pal[3 * v + 2]; }
        else px[0] = px[1] = px[2] = 0;
    }
}

/* The saving screen (screens.c) while an upload runs; after one that plays when done, "saved" for a moment,
 * then the animation starts from its first frame. Returns 1 when it drew. */
int anim_draw_saving(uint32_t now, int playing)
{
    if (up.active) { screens_saving(now, up.next, up.len, -1); return 1; }
    if (!saved.show) return 0;
    uint32_t d = now - saved.at;
    if (!playing || d >= SCREENS_SAVED_MS) {
        saved.show = 0;
        if (playing) anim_start(pl.all ? ANIM_ALL : pl.slot, now);
        return 0;
    }
    screens_saving(now, saved.len, saved.len, (int)d);
    return 1;
}

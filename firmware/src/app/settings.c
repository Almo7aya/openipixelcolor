/* Settings, picture and text strip that survive a power cycle. Hardware independent (storage via
 * app_port.h), so the PC simulator runs this file unchanged against an emulated flash ("sim settings").
 *
 * Where: the top of the update staging area in the internal flash, below the update record (0x1FF000).
 * An update only reaches these sectors with an image larger than ~0xF3000 bytes (ours ~45 KiB, the vendor's
 * ~255 KiB); if one ever does, the records fail their checks and the panel starts with factory settings.
 *
 *   0x1FD000  settings log, 16 slots x 256 B. A save programs the next empty slot (a few ms, no erase).
 *             Only when all 16 are used is the sector erased (~0.5 s, the panel goes dark for that
 *             moment). At boot the valid record with the highest sequence number wins.
 *   0x1FC000  picture: 16-byte header + 32x32x3 pixels, erased and rewritten as a whole.
 *   0x1FB000  text picture strip (Arabic etc.): 16-byte header + up to 3075 bytes, the same way.
 *
 * Writes happen a few seconds after the last change (dragging a slider saves once), never during an
 * update and never in rescue mode (main.c). A record is used only if magic, length, layout version and
 * CRC-32 match, and modes_restore() clamps every field: damaged data can only give factory settings.
 * Nothing written is read back in the same boot (the flash cache may hold old bytes): RAM keeps the copy.
 */
#include "settings.h"
#include "modes.h"
#include "app_port.h"

#define SLOT_SIZE      256u
#define SLOTS          16u
#define SET_MAGIC      0x31475453u       /* "STG1" */
#define PIC_MAGIC      0x31434950u       /* "PIC1" */
#define TXT_MAGIC      0x31545854u       /* "TXT1" */
#define PIC_BYTES      (32u * 32u * 3u)
#define SAVE_DELAY_MS  3000u             /* settings: quiet time after the last change */
#define PIC_DELAY_MS   8000u             /* picture: longer, a save blanks the panel for ~0.5 s */
#define TXT_DELAY_MS   3000u
#define CHECK_MS       500u
#define RETRY_MS       30000u
#define MAX_FAILS      3u                /* then stop writing until the next boot */
#define BLOB_MAX       MODES_TEXTBITS_MAX

typedef struct {                          /* exactly one slot */
    uint32_t magic;
    uint32_t seq;
    uint16_t len;                         /* sizeof(modes_persist_t) of the layout that wrote it */
    uint16_t rsv;
    uint8_t  data[240];
    uint32_t crc;                         /* CRC-32 of the 252 bytes above */
} set_rec_t;

typedef struct {                          /* picture or text strip, one sector each */
    uint32_t magic;
    uint32_t crc;                         /* CRC-32 of data[0..len) */
    uint32_t len;
    uint32_t rsv;
    uint8_t  data[BLOB_MAX];
} blob_rec_t;

_Static_assert(sizeof(set_rec_t) == SLOT_SIZE, "settings record must fill one slot");
_Static_assert(sizeof(modes_persist_t) <= 240, "modes_persist_t does not fit a settings record");
_Static_assert(sizeof(blob_rec_t) <= 4096, "picture / text record must fit one sector");
_Static_assert(PIC_BYTES <= BLOB_MAX, "picture must fit the blob buffer");

enum { B_PICTURE, B_TEXT, NBLOB };
typedef struct { uint32_t sector, magic, delay, saved_crc, seen_crc, seen_at; uint8_t valid; } blob_t;
static blob_t blobs[NBLOB] = {
    { PICTURE_SECTOR,  PIC_MAGIC, PIC_DELAY_MS, 0, 0, 0, 0 },
    { TEXTBITS_SECTOR, TXT_MAGIC, TXT_DELAY_MS, 0, 0, 0, 0 },
};

static set_rec_t  rec;
static blob_rec_t brec;
static modes_persist_t saved, seen;
static uint32_t seq, seen_at, next_check, retry_at;
static uint8_t  next_slot, ready, fails;
static int8_t   last_rc;

static uint32_t crc32(const void *buf, uint32_t len)   /* zlib CRC-32, no table */
{
    const uint8_t *p = buf;
    uint32_t c = 0xFFFFFFFFu;
    while (len--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void copy(void *dst, const void *src, unsigned n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}

static int same(const void *a, const void *b, unsigned n)
{
    const uint8_t *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}

static int erased(const void *p, unsigned n)
{
    const uint8_t *b = p;
    while (n--) if (*b++ != 0xFFu) return 0;
    return 1;
}

/* what is in RAM now for a blob (NULL = nothing to keep), and whether it should be saved */
static const uint8_t *blob_data(int i, unsigned *len)
{
    if (i == B_PICTURE) { *len = PIC_BYTES; return modes_canvas(); }
    return modes_textbits(len);
}

static uint32_t blob_crc(int i)
{
    unsigned len;
    const uint8_t *d = blob_data(i, &len);
    return d ? crc32(d, len) : 0;
}

static int blob_wanted(int i, const modes_persist_t *cur)
{
    unsigned len;
    return i == B_PICTURE ? cur->mode == MODE_CANVAS : blob_data(i, &len) != 0;
}

/* a valid record in flash, or NULL */
static const blob_rec_t *blob_flash(int i)
{
    const blob_rec_t *r = app_flash_ptr(blobs[i].sector);
    if (r->magic != blobs[i].magic || r->len == 0 || r->len > BLOB_MAX) return 0;
    if (i == B_PICTURE && r->len != PIC_BYTES) return 0;
    if (crc32(r->data, r->len) != r->crc) return 0;
    return r;
}

void settings_load(uint32_t now, int apply)
{
    const set_rec_t *best = 0;
    ready = 0; next_slot = 0; seq = 0; fails = 0; last_rc = 0; retry_at = 0;

    for (unsigned i = 0; i < SLOTS; i++) {
        const set_rec_t *r = app_flash_ptr(SETTINGS_SECTOR + i * SLOT_SIZE);
        if (erased(r, SLOT_SIZE)) continue;
        next_slot = (uint8_t)(i + 1u);                    /* append after the last used slot */
        if (r->magic != SET_MAGIC || r->len < MODES_PERSIST_V1_LEN || r->len > sizeof(modes_persist_t)) continue;
        if (crc32(r, SLOT_SIZE - 4u) != r->crc) continue;
        if (r->data[0] < 1 || r->data[0] > MODES_PERSIST_VER) continue;   /* modes_persist_t.ver */
        if (!best || r->seq > best->seq) best = r;
    }
    if (best) seq = best->seq;                            /* the next record continues the sequence */
    modes_first_start(best == 0);                         /* never saved anything: show the welcome screen */

    for (int i = 0; i < NBLOB; i++) {                     /* picture and text strip first: the settings */
        const blob_rec_t *r = blob_flash(i);              /* below may refer to them */
        blobs[i].valid = (uint8_t)(r != 0);
        if (!r || !apply) continue;
        if (i == B_PICTURE) modes_restore_canvas(r->data);
        else blobs[i].valid = (uint8_t)modes_restore_textbits(r->data, r->len);
    }

    modes_persist_t p;
    modes_snapshot(&p);                                   /* fields an older layout lacks keep factory values */
    if (best) copy(&p, best->data, best->len);
    if (apply && best) {
        if (p.mode == MODE_CANVAS && !blobs[B_PICTURE].valid) p.mode = MODE_EFFECT;
        modes_restore(&p, now);
    }

    modes_snapshot(&saved);                               /* RAM now matches flash (or nothing saved yet) */
    if (best && !apply) saved.ver = 0xFFu;                /* not applied (crash boot): replace the record */
    seen = saved;
    seen_at = now;
    for (int i = 0; i < NBLOB; i++) { blobs[i].saved_crc = blobs[i].seen_crc = blob_crc(i); blobs[i].seen_at = now; }
    next_check = now + CHECK_MS;
    ready = 1;
}

static int write_settings(const modes_persist_t *p)
{
    uint8_t *b = (uint8_t *)&rec;
    for (unsigned i = 0; i < sizeof rec; i++) b[i] = 0;
    rec.magic = SET_MAGIC;
    rec.seq = ++seq;
    rec.len = sizeof *p;
    copy(rec.data, p, sizeof *p);
    rec.crc = crc32(&rec, SLOT_SIZE - 4u);

    int rc;
    if (next_slot >= SLOTS) {
        rc = app_flash_write_sector(SETTINGS_SECTOR, &rec, SLOT_SIZE);
        next_slot = 1;
    } else {
        rc = app_flash_program(SETTINGS_SECTOR + next_slot * SLOT_SIZE, &rec, SLOT_SIZE);
        next_slot++;
    }
    if (rc) next_slot = SLOTS;                            /* state unknown: erase on the next attempt */
    return rc;
}

static int write_blob(int i)
{
    unsigned len;
    const uint8_t *d = blob_data(i, &len);
    if (!d) return 0;
    brec.magic = blobs[i].magic;
    brec.len = len;
    brec.rsv = 0;
    copy(brec.data, d, len);
    brec.crc = crc32(brec.data, len);
    return app_flash_write_sector(blobs[i].sector, &brec, (16u + len + 3u) & ~3u);
}

static void note(int rc, uint32_t now)
{
    last_rc = (int8_t)rc;
    if (rc) { fails++; retry_at = now + RETRY_MS; if (!retry_at) retry_at = 1; }
    else    fails = 0;
}

void settings_tick(uint32_t now, int allowed)
{
    if (!ready || (int32_t)(now - next_check) < 0) return;
    next_check = now + CHECK_MS;

    modes_persist_t cur;
    modes_snapshot(&cur);
    if (!same(&cur, &seen, sizeof cur)) { seen = cur; seen_at = now; }
    uint32_t crc[NBLOB];
    for (int i = 0; i < NBLOB; i++) {
        crc[i] = blob_crc(i);
        if (crc[i] != blobs[i].seen_crc) { blobs[i].seen_crc = crc[i]; blobs[i].seen_at = now; }
    }

    if (!allowed || fails >= MAX_FAILS) return;
    if (retry_at) {
        if ((int32_t)(now - retry_at) < 0) return;
        retry_at = 0;
    }

    if (!same(&cur, &saved, sizeof cur)) {
        if (now - seen_at < SAVE_DELAY_MS) return;
        int rc = write_settings(&cur);
        note(rc, now);
        if (rc == 0) saved = cur;
        return;                                           /* at most one flash write per check */
    }
    for (int i = 0; i < NBLOB; i++) {
        if (crc[i] == blobs[i].saved_crc || !blob_wanted(i, &cur) || now - blobs[i].seen_at < blobs[i].delay) continue;
        int rc = write_blob(i);
        note(rc, now);
        if (rc == 0) { blobs[i].saved_crc = crc[i]; blobs[i].valid = 1; }
        return;
    }
}

/* 0xF109 reply: seq(4) next_slot saved flags last_rc
 * saved: bit0 picture, bit1 text strip
 * flags: bit0 loaded, bit1 settings waiting to be saved, bit2 picture or text waiting, bit3 writing stopped */
unsigned settings_info(uint8_t out[8])
{
    modes_persist_t cur;
    modes_snapshot(&cur);
    int blob_pending = 0;
    for (int i = 0; i < NBLOB && ready; i++)
        if (blob_crc(i) != blobs[i].saved_crc && blob_wanted(i, &cur)) blob_pending = 1;
    out[0] = (uint8_t)seq; out[1] = (uint8_t)(seq >> 8); out[2] = (uint8_t)(seq >> 16); out[3] = (uint8_t)(seq >> 24);
    out[4] = next_slot;
    out[5] = (uint8_t)(blobs[B_PICTURE].valid | (blobs[B_TEXT].valid << 1));
    out[6] = (uint8_t)((ready ? 1u : 0u) |
                       (ready && !same(&cur, &saved, sizeof cur) ? 2u : 0u) |
                       (blob_pending ? 4u : 0u) |
                       (fails >= MAX_FAILS ? 8u : 0u));
    out[7] = (uint8_t)last_rc;
    return 8;
}

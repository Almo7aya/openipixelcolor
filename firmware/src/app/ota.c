/* Firmware update receiver - compatible with the vendor's update protocol, with stricter checks.
 *
 * Wire format (both directions checked against the vendor firmware and pypixelcolor):
 *   0xC000: 0D 00 00 C0 | count u8 | crc32(encoded image) u32 | size u32
 *   0xC001: len u16 | 01 C0 | idx u8 | crc32(chunk) u32 | chunk_len u32 | chunk (encoded)
 * Each chunk is decoded ((b - 0x0A) ^ 0xCA, the format of the vendor's update files) and written to 0x108000 + idx * 0x3000.
 * After the last chunk, if crc32 over all encoded bytes matches, the 0x400-byte record is written to
 * 0x1FF000 in the bootloader's format and the chip resets; the bootloader installs the staged image.
 */
#include <stddef.h>
#include "ota.h"

static uint32_t crc_table[256];
static uint8_t  crc_ready;

static struct {
    uint8_t  active;
    uint8_t  count;
    uint8_t  next;
    uint32_t expect_crc;     /* crc32 of the whole encoded file, from 0xC000 */
    uint32_t size;
    uint32_t received;
    uint32_t crc_raw;        /* running crc32 of encoded bytes */
    uint32_t crc_dec;        /* running crc32 of decoded bytes */
} st;

static uint32_t dec_buf[OTA_CHUNK / 4 + 1];
static uint32_t record[0x400 / 4];

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        crc_table[i] = c;
    }
    crc_ready = 1;
}

/* standard reflected CRC-32 (zlib) */
uint32_t ota_crc32(uint32_t crc, const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    if (!crc_ready) crc_init();
    crc = ~crc;
    while (len--) crc = (crc >> 8) ^ crc_table[(crc ^ *p++) & 0xFFu];
    return ~crc;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int  ota_active(void) { return st.active; }
void ota_abort(void)  { st.active = 0; }

void ota_progress(uint32_t *done, uint32_t *total) { *done = st.received; *total = st.size; }

int ota_begin(uint8_t count, uint32_t crc_encoded, uint32_t size)
{
    st.active = 0;
    if (count == 0 || size == 0 || size > OTA_MAX_SIZE) return OTA_FAIL;
    if ((uint32_t)count != (size + OTA_CHUNK - 1u) / OTA_CHUNK) return OTA_FAIL;   /* stricter than the vendor firmware */
    st.count = count;
    st.next = 0;
    st.expect_crc = crc_encoded;
    st.size = size;
    st.received = 0;
    st.crc_raw = 0;
    st.crc_dec = 0;
    st.active = 1;
    return OTA_OK;
}

static int fail(void) { st.active = 0; return OTA_FAIL; }

int ota_chunk(const uint8_t *pkt, uint32_t pkt_len)
{
    if (!st.active || pkt_len < 13u) return fail();
    uint8_t  idx  = pkt[4];
    uint32_t ccrc = rd32(pkt + 5);
    uint32_t len  = rd32(pkt + 9);
    const uint8_t *data = pkt + 13;

    if (len == 0 || len > OTA_CHUNK || pkt_len != 13u + len) return fail();
    if (idx != st.next || idx >= st.count) return fail();
    if (st.received + len > st.size) return fail();
    if (idx + 1u < st.count && len != OTA_CHUNK) return fail();
    if (ota_crc32(0, data, len) != ccrc) return fail();

    uint8_t *d = (uint8_t *)dec_buf;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)((uint8_t)(data[i] - 0x0Au) ^ 0xCAu);
    for (uint32_t i = len; i < ((len + 3u) & ~3u); i++) d[i] = 0xFF;

    st.crc_raw = ota_crc32(st.crc_raw, data, len);
    st.crc_dec = ota_crc32(st.crc_dec, d, len);
    if (ota_port_flash_write(OTA_STAGING + (uint32_t)idx * OTA_CHUNK, d, len) != 0) return fail();
    st.received += len;
    st.next++;

    if (st.next < st.count) return OTA_OK;

    /* last chunk */
    if (st.received != st.size || st.crc_raw != st.expect_crc) return fail();

    /* safety net: read the staged image back through XIP and check it really is what we decoded.
       (in 99.01 a driver bug made writes silently fail; never write the record for a bad image) */
    if (ota_crc32(0, ota_port_flash_ptr(OTA_STAGING), st.size) != st.crc_dec) return fail();

    for (uint32_t i = 0; i < sizeof record / 4; i++) record[i] = 0;
    record[0] = OTA_STAGING;
    record[1] = st.size;
    record[2] = st.crc_dec;
    uint8_t *r = (uint8_t *)record;
    r[0x0C] = 0x00; r[0x0D] = 0x05; r[0x0E] = 0x00; r[0x0F] = 0x05;      /* what the bootloader expects: d0=0 d1=5 d2=0 d3=5 */
    r[0x10] = 0x00; r[0x11] = 0x01; r[0x12] = 0xC0; r[0x13] = 0x01;      /* d4=0, cmd 0xC001, d7=1 */
    st.active = 0;

    ota_port_final_ack();
    for (int attempt = 0; attempt < 2; attempt++) {
        if (ota_port_flash_write(OTA_RECORD, record, sizeof record) != 0) continue;
        const uint8_t *chk = ota_port_flash_ptr(OTA_RECORD);
        int same = 1;
        for (uint32_t i = 0; i < 0x14u; i++) if (chk[i] != r[i]) { same = 0; break; }
        if (!same) continue;
        ota_port_reset();
        return OTA_OK;   /* only reached in tests */
    }
    return OTA_FAIL;     /* record could not be verified: do not reset into an unknown state */
}

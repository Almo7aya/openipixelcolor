// The panel's Bluetooth protocol (docs/PROTOCOL.md): packets, versions, checksums, update-file encoding.

export const CMD = {
  TIME: 0x8001, CLEAR: 0x8003, BRIGHTNESS: 0x8004, VERSION: 0x8005, ORIENT: 0x8006, TIMER: 0x8009, SCORE: 0x800A,
  CANVAS: 0x0104, CLOCK: 0x0106, POWER: 0x0107, RHYTHM_ANIM: 0x0200, RHYTHM: 0x0201,
  OTA_BEGIN: 0xC000, OTA_CHUNK: 0xC001,
  DIAG: 0xF0F1, MODE: 0xF101, PICTURE: 0xF103, AUTOPLAY: 0xF104, PIXELS: 0xF105, STATUS: 0xF106, KEY: 0xF107,
  FACTORY: 0xF108, SAVED: 0xF109, TEXT: 0xF10B,
  ANIM_BEGIN: 0xF10C, ANIM_DATA: 0xF10D, ANIM_LIST: 0xF10E, ANIM_INFO: 0xF10F, ANIM_DELETE: 0xF110, ANIM_PLAY: 0xF111,
  ANIM_READ: 0xF112, PANEL_INFO: 0xF113,
};
export const MODE = { EFFECT: 0, CLOCK: 1, TEXT: 2, CANVAS: 3, RHYTHM: 4, SCORE: 5, TIMER: 6, TEST: 7, SPLASH: 8, ANIM: 9, WELCOME: 10 };

export function packet(cmd, payload = []) {
  const p = new Uint8Array(4 + payload.length);
  p[0] = p.length & 255; p[1] = p.length >> 8; p[2] = cmd & 255; p[3] = cmd >> 8;
  p.set(payload, 4);
  return p;
}
export const u32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
export const rd32 = (v, o) => (v[o] | (v[o + 1] << 8) | (v[o + 2] << 16) | (v[o + 3] << 24)) >>> 0;
export const sameBytes = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

const CRC_T = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[i] = c >>> 0; }
  return t;
})();
export function crc32(buf) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) c = CRC_T[(c ^ buf[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

/* 0x8005 reply -> version. openipixelcolor: one number (100, 101, ...) in the "major" byte (from 256 on:
   255 + minor); builds before that: 99.xx; the vendor firmware: 3.x. level orders openipixelcolor builds
   (99.xx -> xx, so 99.18 < 100) and is 0 for the vendor firmware. */
export function fwVersionOf(v) {
  const maj = v[4], min = v[5], two = `${maj}.${String(min).padStart(2, "0")}`;
  if (maj >= 100) { const n = maj === 255 ? 255 + min : maj; return { custom: true, text: String(n), level: n }; }
  if (maj === 99) return { custom: true, text: two, level: min };
  return { custom: false, text: two, level: 0 };
}
export const versionLevel = (s) => (/^99\.\d+$/.test(s) ? Number(s.slice(3)) : /^\d+$/.test(s) && Number(s) >= 100 ? Number(s) : 0);
export const newerVersion = (a, b) => versionLevel(a) > versionLevel(b);

/* 0xF0F1: mode crashes exc_type exc_address(4) uptime_s(4) - see firmware/src/main.c */
export function healthText(d) {
  const mode = d[5], exc = d[7], addr = rd32(d, 8), up = rd32(d, 12);
  const upText = up < 120 ? `${up} s` : up < 7200 ? `${Math.round(up / 60)} min` : `${Math.round(up / 3600)} h`;
  const last = exc ? `; an earlier crash was at 0x${addr.toString(16).padStart(8, "0")}` : "";
  if (mode === 1) return "Rescue mode (button held while switching on): only firmware install works";
  if (mode === 2) return `Rescue mode: it kept crashing at start-up${last}. Install a working firmware`;
  return `Running for ${upText}${last}`;
}
/* 0xF109: seq(4) next_slot saved flags last_rc - see firmware/src/app/settings.c */
export function savedText(s) {
  const f = s[11];
  if (!(f & 1)) return "–";
  if (f & 8) return `Can't save on the panel (error ${(s[12] << 24) >> 24})`;
  if (f & 2) return "Saving in a few seconds";
  if (f & 4) return "Saving the picture or text";
  return "Everything is saved on the panel";
}

/* update files are byte-substituted (tools/fwcodec.py); a raw build is encoded here. The decoded image
   must carry the bootloader's header magic "FHD\x03" at 0xD4. */
export const fwEncode = (raw) => raw.map((b) => ((b ^ 0x4A) + 0x8A) & 255);
export const fwDecode = (enc) => enc.map((b) => ((b - 0x8A) & 255) ^ 0x4A);
export const hasImageHeader = (raw) => raw.length > 0x100 && raw[0] === 0x48 && raw[0xD4] === 0x46 && raw[0xD5] === 0x48 && raw[0xD6] === 0x44 && raw[0xD7] === 0x03;
export const OTA_CHUNK = 0x3000;

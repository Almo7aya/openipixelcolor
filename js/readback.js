// Previews of animations this browser didn't send (another phone, a script): the panel reads its stored file
// back, 10 bytes a request (0xF112, firmware 101 and later). Small files are read whole, so they move and the
// panel preview can play them; for bigger ones only the colours and the first frame (decoded like the
// player). Runs in the background, one request at a time between everything else.
import { CMD, u32 } from "./protocol.js?v=62301d0";
import { panel, send } from "./ble.js?v=62301d0";
import { putAnim } from "./idb.js?v=62301d0";
import OPA from "./create/anim-codec.js?v=62301d0";

export const READBACK_LEVEL = 101;
const WHOLE_MAX = 2048;                                  // read files up to this size completely (~200 requests)

async function readAt(slot, off) {
  const r = await send(CMD.ANIM_READ, [slot, ...u32(off)], 4000, (v) => v.length >= 16 && v[5] === (off & 255));
  if (r[4] !== 1) throw new Error("The panel refused to read the animation back.");
  return r.subarray(6, 16);
}
async function readRange(slot, from, to) {
  const out = new Uint8Array(to - from);
  for (let o = from; o < to; o += 10) {
    if (!panel.chWrite) throw new Error("Disconnected.");
    const d = await readAt(slot, o);
    out.set(d.subarray(0, Math.min(10, to - o)), o - from);
  }
  return out;
}

/* the first frame from the header, the palette and the first frame's drawing ops (see anim-codec.js) */
function firstFrame(ncol, pal, ops, npx) {
  const idx = new Uint8Array(npx), rgb = new Uint8Array(npx * 3);
  let p = 0, i = 0;
  while (p < ops.length && i < npx) {
    const op = ops[p++];
    let cnt = Math.min((op & 63) + 1, npx - i);
    if (op >> 6 === 0) i += cnt;
    else if (op >> 6 === 1) { const v = ops[p++]; while (cnt--) idx[i++] = v; }
    else if (op >> 6 === 2) { while (cnt-- && p < ops.length) idx[i++] = ops[p++]; }
    else break;
  }
  for (let k = 0; k < npx; k++) {
    const v = idx[k];
    if (v < ncol) { rgb[k * 3] = pal[v * 3]; rgb[k * 3 + 1] = pal[v * 3 + 1]; rgb[k * 3 + 2] = pal[v * 3 + 2]; }
  }
  return rgb;
}

/* -> { rgb, w, h } (the first frame), and the whole file stored in this browser when it was small enough */
export async function readPreview(item) {
  if (item.size <= WHOLE_MAX) {
    const bytes = await readRange(item.slot, 0, item.size);
    bytes.set([0xFF, 0xFF, 0xFF, 0xFF], 28);              // the state word as sent, not the panel's "DONE"
    if (OPA.crc32(bytes, OPA.HDR) !== item.crc) throw new Error("The read-back copy didn't check out.");
    const frames = OPA.decode(bytes);
    await putAnim(item.crc, bytes);
    return { rgb: frames[0].rgb, w: frames.w, h: frames.h, whole: true };
  }
  const head = await readRange(item.slot, 0, 10);
  if (head[0] !== 0x4F || head[1] !== 0x50 || head[2] !== 0x41 || head[3] !== 0x31) throw new Error("Not an animation file.");
  const ncol = head[8] | (head[9] << 8);
  if (!ncol || ncol > 256 || !head[4] || !head[5]) throw new Error("Damaged animation.");
  const palAndHead = await readRange(item.slot, OPA.HDR, OPA.HDR + 3 * ncol + 4);
  const n = palAndHead[3 * ncol + 2] | (palAndHead[3 * ncol + 3] << 8);
  const start = OPA.HDR + 3 * ncol + 4;
  const ops = await readRange(item.slot, start, Math.min(item.size, start + n));
  return { rgb: firstFrame(ncol, palAndHead.subarray(0, 3 * ncol), ops, head[4] * head[5]), w: head[4], h: head[5], whole: false };
}

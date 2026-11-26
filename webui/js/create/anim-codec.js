// openipixelcolor animation files ("OPA1", any matrix size: w and h in the header): colour reduction, encoder, and a decoder that mirrors the
// firmware's player (firmware/src/app/anim.c) step for step, so the preview shows exactly what the panel
// shows. Also used by the simulator tests (tools/sim/anim_vectors.js).
const W = 32, H = 32, NPX = W * H, HDR = 64;
const SLOT_SIZE = 0x8000, SLOTS = 20, PIECE = 4096;
const MAGIC = 0x3141504F, DONE = 0x454E4F44;
const MAX_BYTES = SLOT_SIZE * SLOTS, MAX_FRAMES = 65535, NAME_MAX = 27;
const MIN_DELAY = 20, MAX_DELAY = 65535;

const CRC_T = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[i] = c >>> 0; }
  return t;
})();
function crc32(buf, start = 0, end = buf.length) {
  let c = 0xFFFFFFFF;
  for (let i = start; i < end; i++) c = CRC_T[(c ^ buf[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

/* ---- colour reduction: exact palette when there are few colours, else median cut ---------------- */
function histogram(frames) {
  const counts = new Map();
  for (const f of frames) {
    const p = f.rgb, n = p.length / 3;
    for (let i = 0; i < n; i++) {
      const k = (p[i * 3] << 16) | (p[i * 3 + 1] << 8) | p[i * 3 + 2];
      counts.set(k, (counts.get(k) || 0) + 1);
    }
  }
  return counts;
}

function medianCut(keys, weights, k) {
  const ch = (c, a) => (c >> (16 - 8 * a)) & 255;
  const box = (ids) => {
    let lo = [255, 255, 255], hi = [0, 0, 0], w = 0;
    for (const i of ids) {
      const c = keys[i];
      for (let a = 0; a < 3; a++) { const v = ch(c, a); if (v < lo[a]) lo[a] = v; if (v > hi[a]) hi[a] = v; }
      w += weights[i];
    }
    const span = [hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]];
    const axis = span[1] >= span[0] && span[1] >= span[2] ? 1 : span[0] >= span[2] ? 0 : 2;
    return { ids, w, axis, span: span[axis] };
  };
  const boxes = [box([...keys.keys()])];
  while (boxes.length < k) {
    let best = -1, score = 0;
    for (let b = 0; b < boxes.length; b++) {
      const s = boxes[b].ids.length > 1 ? boxes[b].span * Math.sqrt(boxes[b].w) : 0;
      if (s > score) { score = s; best = b; }
    }
    if (best < 0) break;
    const { ids, axis, w } = boxes[best];
    ids.sort((a, b) => ch(keys[a], axis) - ch(keys[b], axis));
    let acc = 0, cut = 1;
    for (let i = 0; i < ids.length - 1; i++) { acc += weights[ids[i]]; if (acc >= w / 2) { cut = i + 1; break; } cut = i + 1; }
    boxes.splice(best, 1, box(ids.slice(0, cut)), box(ids.slice(cut)));
  }
  return boxes.map(({ ids, w }) => {
    let r = 0, g = 0, b = 0;
    for (const i of ids) { const c = keys[i], q = weights[i]; r += (c >> 16) * q; g += ((c >> 8) & 255) * q; b += (c & 255) * q; }
    return [Math.round(r / w), Math.round(g / w), Math.round(b / w)];
  });
}

/* frames: [{ rgb: Uint8Array(w*h*3) }] -> { palette: [[r,g,b]], indexed: [Uint8Array(w*h)] }
   The most used colour gets index 0 (the grid starts as colour 0, so a plain background costs nothing). */
function quantize(frames, maxColors = 256) {
  maxColors = Math.max(2, Math.min(256, maxColors | 0));
  const counts = histogram(frames);
  const keys = Uint32Array.from(counts.keys()), weights = Uint32Array.from(counts.values());
  let palette, lookup = new Map();
  if (keys.length <= maxColors) {
    palette = [...keys].map((c) => [c >> 16, (c >> 8) & 255, c & 255]);
    keys.forEach((c, i) => lookup.set(c, i));
  } else {
    palette = medianCut(keys, weights, maxColors);
    for (const c of keys) {
      const r = c >> 16, g = (c >> 8) & 255, b = c & 255;
      let best = 0, bd = Infinity;
      for (let i = 0; i < palette.length; i++) {
        const p = palette[i], dr = p[0] - r, dg = p[1] - g, db = p[2] - b;
        const d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
        if (d < bd) { bd = d; best = i; }
      }
      lookup.set(c, best);
    }
  }
  const use = new Float64Array(palette.length);
  keys.forEach((c, i) => { use[lookup.get(c)] += weights[i]; });
  const order = [...palette.keys()].filter((i) => use[i] > 0).sort((a, b) => use[b] - use[a]);
  const remap = new Int32Array(palette.length);
  order.forEach((old, i) => { remap[old] = i; });
  palette = order.map((i) => palette[i]);
  const indexed = frames.map((f) => {
    const p = f.rgb, n = p.length / 3, out = new Uint8Array(n);
    for (let i = 0; i < n; i++) out[i] = remap[lookup.get((p[i * 3] << 16) | (p[i * 3 + 1] << 8) | p[i * 3 + 2])];
    return out;
  });
  return { palette, indexed };
}

/* ---- drawing ops of one frame against the previous one (see anim.c) ----------------------------- */
function encodeFrame(prev, cur, out) {
  let end = cur.length;
  while (end > 0 && cur[end - 1] === prev[end - 1]) end--;   // unchanged tail: nothing to send
  let i = 0;
  while (i < end) {
    if (cur[i] === prev[i]) {
      let j = i + 1;
      while (j < end && j - i < 64 && cur[j] === prev[j]) j++;
      out.push(j - i - 1);
      i = j;
      continue;
    }
    let r = i + 1;
    while (r < end && r - i < 64 && cur[r] === cur[i]) r++;
    if (r - i >= 2) { out.push(0x40 | (r - i - 1), cur[i]); i = r; continue; }
    let l = i + 1;
    while (l < end && l - i < 64) {
      if (cur[l] === prev[l] && l + 1 < end && cur[l + 1] === prev[l + 1]) break;   // a skip is cheaper
      if (l + 2 < end && cur[l] === cur[l + 1] && cur[l] === cur[l + 2]) break;     // so is a run
      l++;
    }
    out.push(0x80 | (l - i - 1));
    for (let k = i; k < l; k++) out.push(cur[k]);
    i = l;
  }
}

function nameBytes(name) {
  const enc = new TextEncoder().encode(String(name || "").trim());
  let n = Math.min(enc.length, NAME_MAX);
  while (n > 0 && n < enc.length && (enc[n] & 0xC0) === 0x80) n--;   // don't cut a letter in half
  return enc.subarray(0, n);
}

/* frames: [{ rgb: Uint8Array(w*h*3), delay: ms }], opts { w, h (default 32 x 32), name, maxColors }
   -> { bytes, colors, frames, size } (throws if too big) */
function encode(frames, opts = {}) {
  if (!frames.length) throw new Error("No frames.");
  if (frames.length > MAX_FRAMES) throw new Error("Too many frames.");
  const w = opts.w || W, h = opts.h || H;
  if (w > 255 || h > 255 || frames.some((f) => f.rgb.length !== w * h * 3)) throw new Error("The frames don't match the panel's size.");
  const { palette, indexed } = quantize(frames, opts.maxColors || 256);
  const ops = [], delays = [];
  let prev = new Uint8Array(w * h), merged = 0;
  for (let f = 0; f < indexed.length; f++) {
    const d = Math.max(MIN_DELAY, Math.min(MAX_DELAY, Math.round(frames[f].delay || 100)));
    if (f > 0 && indexed[f].every((v, i) => v === prev[i]) && delays[delays.length - 1] + d <= MAX_DELAY) {
      delays[delays.length - 1] += d;                          // same picture again: just show it longer
      merged++;
      continue;
    }
    const o = [];
    encodeFrame(prev, indexed[f], o);
    ops.push(o); delays.push(d);
    prev = indexed[f];
  }
  const size = HDR + palette.length * 3 + ops.reduce((a, o) => a + 4 + o.length, 0);
  const out = new Uint8Array(size), dv = new DataView(out.buffer);
  dv.setUint32(0, MAGIC, true);
  out[4] = w; out[5] = h;
  dv.setUint16(6, ops.length, true);
  dv.setUint16(8, palette.length, true);
  dv.setUint32(12, size, true);
  dv.setUint32(28, 0xFFFFFFFF, true);
  out.set(nameBytes(opts.name), 32);
  let o = HDR;
  for (const c of palette) { out[o++] = c[0]; out[o++] = c[1]; out[o++] = c[2]; }
  ops.forEach((list, f) => {
    dv.setUint16(o, delays[f], true); dv.setUint16(o + 2, list.length, true); o += 4;
    out.set(list, o); o += list.length;
  });
  dv.setUint32(16, crc32(out, HDR, size), true);
  return { bytes: out, colors: palette.length, frames: ops.length, merged, size, fits: size <= MAX_BYTES };
}

/* the firmware's player, step for step -> [{ rgb, delay }] with .w and .h (throws on a damaged file) */
function decode(b) {
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (b.length < HDR || dv.getUint32(0, true) !== MAGIC || !b[4] || !b[5]) throw new Error("Not an openipixelcolor animation.");
  const NPX = b[4] * b[5];
  const frames = dv.getUint16(6, true), ncol = dv.getUint16(8, true), len = dv.getUint32(12, true);
  if (!frames || !ncol || ncol > 256 || len > b.length || len < HDR + 3 * ncol + 4 * frames) throw new Error("Damaged animation.");
  const pal = b.subarray(HDR, HDR + 3 * ncol), idx = new Uint8Array(NPX), out = [];
  let off = HDR + 3 * ncol;
  for (let f = 0; f < frames; f++) {
    if (off + 4 > len) throw new Error("Damaged animation.");
    const delay = dv.getUint16(off, true), n = dv.getUint16(off + 2, true), end = off + 4 + n;
    if (end > len) throw new Error("Damaged animation.");
    let p = off + 4, i = 0;
    while (p < end) {
      const op = b[p++]; let cnt = (op & 63) + 1;
      if (i + cnt > NPX) throw new Error("Damaged animation.");
      switch (op >> 6) {
      case 0: i += cnt; break;
      case 1: { if (p >= end) throw new Error("Damaged animation."); const v = b[p++]; while (cnt--) idx[i++] = v; break; }
      case 2: if (end - p < cnt) throw new Error("Damaged animation."); while (cnt--) idx[i++] = b[p++]; break;
      default: throw new Error("Damaged animation.");
      }
    }
    off = end;
    const rgb = new Uint8Array(NPX * 3);
    for (let k = 0; k < NPX; k++) {
      const v = idx[k];
      if (v < ncol) { rgb[k * 3] = pal[v * 3]; rgb[k * 3 + 1] = pal[v * 3 + 1]; rgb[k * 3 + 2] = pal[v * 3 + 2]; }
    }
    out.push({ rgb, delay });
  }
  out.w = b[4]; out.h = b[5];
  return out;
}

function info(b) {
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const z = b.subarray(32, 64), n = z.indexOf(0);
  return { w: b[4], h: b[5], frames: dv.getUint16(6, true), colors: dv.getUint16(8, true), size: dv.getUint32(12, true),
           crc: dv.getUint32(16, true), name: new TextDecoder().decode(z.subarray(0, n < 0 ? NAME_MAX : Math.min(n, NAME_MAX))) };
}

const OPA = { W, H, NPX, HDR, SLOT_SIZE, SLOTS, PIECE, MAX_BYTES, MIN_DELAY, NAME_MAX, MAGIC, DONE,
                crc32, quantize, encode, decode, info, encodeFrame, nameBytes };
export default OPA;

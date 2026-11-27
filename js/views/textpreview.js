// Live preview of the text playlist on the panel preview: the firmware's timeline and effects
// (firmware/src/app/textfx.c) ported line by line, so what it shows is what the panel shows.
import { pix, setPix, drawMirror, claimMirror, mirrorNote } from "../mirror.js?v=ed0e7b5";
import { glyph, glyphSpan } from "../led.js?v=ed0e7b5";
import { is32, sizeText } from "../size.js?v=ed0e7b5";

const p2 = (n) => String(n).padStart(2, "0");
export function expandTokens(t) {
  const d = new Date();
  return t.replace(/\{time\}/g, `${p2(d.getHours())}:${p2(d.getMinutes())}`)
          .replace(/\{secs\}/g, `${p2(d.getHours())}:${p2(d.getMinutes())}:${p2(d.getSeconds())}`)
          .replace(/\{date\}/g, `${p2(d.getDate())}.${p2(d.getMonth() + 1)}`)
          .replace(/\{day\}/g, ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"][d.getDay()])
          .replace(/\{year\}/g, String(d.getFullYear()));
}
export function panelColumns(text, scale) {          // same as the firmware: 5x7 font, trimmed, 1 px gap
  const cols = [];
  for (const ch of text) {
    if (ch === " ") { for (let k = 0; k < 3 * scale; k++) cols.push(0); continue; }
    const g = glyph(ch), [a, b] = glyphSpan(g);
    for (let i = a; i <= b; i++) {
      let v = g[i] & 0x7f;
      if (scale === 2) { let dd = 0; for (let r = 0; r < 7; r++) if ((v >> r) & 1) dd |= 3 << (2 * r); v = dd; }
      for (let k = 0; k < scale; k++) cols.push(v);
    }
    for (let k = 0; k < scale; k++) cols.push(0);
  }
  while (cols.length > 1 && !cols[cols.length - 1]) cols.pop();
  return cols.length ? cols : [0];
}
const SIN8 = Array.from({ length: 256 }, (_, i) => Math.round(128 + 127 * Math.sin(2 * Math.PI * i / 256)));
const sin8 = (i) => SIN8[i & 255];
const idiv = (a, b) => Math.trunc(a / b);
const clampi = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
function hsh(a, b) {
  let h = (Math.imul(a >>> 0, 2654435761) ^ Math.imul((b + 0x9E3779B9) >>> 0, 2246822519)) >>> 0;
  h ^= h >>> 15; h = Math.imul(h, 2246822519) >>> 0; h ^= h >>> 13;
  return h >>> 0;
}
function hsvc(h, s, v) {                              // gfx_hsv
  h &= 255;
  const region = Math.floor(h / 43), rem = (h - region * 43) * 6;
  const p = (v * (255 - s)) >> 8, q = (v * (255 - ((s * rem) >> 8))) >> 8, t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  return [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][Math.min(region, 5)];
}
const scaleC = (c, v) => c.map((x) => idiv(x * v, 255));
const mixC = (a, b, f, n) => (n <= 0 ? a : a.map((x, i) => idiv(x * (n - clampi(f, 0, n)) + b[i] * clampi(f, 0, n), n)));
function pset(x, y, c) { if (x >= 0 && x < 32 && y >= 0 && y < 32) setPix(x, y, c[0], c[1], c[2]); }
function padd(x, y, c) {
  if (x < 0 || x > 31 || y < 0 || y > 31) return;
  const i = (y * 32 + x) * 3;
  for (let k = 0; k < 3; k++) pix[i + k] = Math.min(255, pix[i + k] + c[k]);
}

const PV = { list: [], cur: 0, phase: 0, t0: 0, dur: 0, rep: 0, fin: 0, fout: 0, w: 1, h: 7, x0: 0, y0: 0, win: 1,
             marquee: false, np: 1, cols: [0], running: false, raf: 0, last: 0, fnext: 0 };
const speedOf = (st) => (st[10] < 1 ? 5 : st[10] > 10 ? 10 : st[10]);
const ppsOf = (st) => 6 + speedOf(st) * 4;
const fxMs = (fx, st) => (fx === 0 ? 0 : 1700 - speedOf(st) * 130);
function pvLoad(i) {
  const c = PV.list[i];
  if (c.kind === 1) { PV.cols = panelColumns(expandTokens(c.raw), c.scale ? 2 : 1); PV.h = c.scale ? 14 : 7; }
  else { const s = c.strip; PV.cols = Array.from({ length: s.w }, (_, x) => s.bits[x * s.bpc] | (s.bpc > 1 ? s.bits[x * s.bpc + 1] << 8 : 0) | (s.bpc > 2 ? s.bits[x * s.bpc + 2] << 16 : 0)); PV.h = s.h; }
  PV.w = PV.cols.length;
}
function pvGeometry() {
  const st = PV.list[PV.cur].style;
  PV.win = Math.min(PV.w, 32);
  const al = st[12] & 3;
  PV.y0 = Math.max(0, al === 1 ? 1 : al === 2 ? 31 - PV.h : idiv(32 - PV.h, 2));
  PV.x0 = PV.w < 32 ? idiv(32 - PV.w, 2) : 0;
  PV.marquee = PV.w > 32 && (PV.fin === 1 || PV.fin === 2);
  let np = 0, gap = 1;
  for (let c = 0; c < PV.w; c++) { if (PV.cols[c]) { if (gap) np++; gap = 0; } else gap = 1; }
  PV.np = np || 1;
}
function pvHoldMs(st) {
  const pause = st[11] ? st[11] * 250 : 2000, pps = ppsOf(st);
  if (PV.marquee) return idiv((PV.w + 32) * 1000, pps);
  if (PV.w > 32) return 700 + idiv((PV.w - 32) * 1000, pps) + pause;
  return pause;
}
const pick = (fx) => (fx >= 23 ? 1 + Math.floor(Math.random() * 22) : fx);
function pvPhase(ph, now) {
  const st = PV.list[PV.cur].style;
  PV.phase = ph; PV.t0 = now;
  PV.dur = ph === 0 ? fxMs(PV.fin, st) : ph === 2 ? fxMs(PV.fout, st) : ph === 1 ? pvHoldMs(st) : 250;
}
function pvStartMsg(i, now, first) {
  const st = PV.list[i].style;
  PV.cur = i;
  if (first) PV.rep = 0;
  PV.fin = pick(st[0]); PV.fout = pick(st[2]);
  pvLoad(i); PV.fnext = now + 500;
  pvGeometry();
  pvPhase(PV.marquee ? 1 : 0, now);
}
function pvAdvance(now) {
  for (let g = 0; g < 8 && now - PV.t0 >= PV.dur; g++) {
    const end = PV.t0 + PV.dur;
    if (PV.phase === 0) pvPhase(1, end);
    else if (PV.phase === 1) pvPhase(PV.marquee ? 3 : 2, end);
    else if (PV.phase === 2) pvPhase(3, end);
    else {
      const rep = PV.list[PV.cur].style[15] || 1;
      if (++PV.rep < rep) pvStartMsg(PV.cur, end, false);
      else pvStartMsg((PV.cur + 1) % PV.list.length, end, true);
    }
  }
}
function applyFx(fx, out, p, k, c, r, piece, rtl, s) {
  const W = PV.win, H = PV.h, q = out ? 1000 - p : p, hh = hsh(c, r);
  switch (fx) {
  case 1: s.x += out ? idiv(-p * (PV.x0 + W), 1000) : idiv((1000 - p) * (32 - PV.x0), 1000); return true;
  case 2: s.x += out ? idiv(p * (32 - PV.x0), 1000) : idiv(-(1000 - p) * (PV.x0 + W), 1000); return true;
  case 3: s.y += out ? idiv(-p * (PV.y0 + H), 1000) : idiv((1000 - p) * (32 - PV.y0), 1000); return true;
  case 4: s.y += out ? idiv(p * (32 - PV.y0), 1000) : idiv(-(1000 - p) * (PV.y0 + H), 1000); return true;
  case 5: case 6: case 20: { const kk = rtl ? W - 1 - k : k; return kk * 1000 < q * W; }
  case 7: return Math.abs(2 * k - (W - 1)) * 1000 < q * W;
  case 8: { const d = Math.min(k, W - 1 - k); return 2 * d * 1000 < q * W; }
  case 9: return (k % 4) * 1000 < q * 4;
  case 10: return (hh % 1000) < q;
  case 11:
    if (k & 1) s.y += idiv((out ? p : 1000 - p) * (32 - PV.y0), 1000); else s.y -= idiv((out ? p : 1000 - p) * (PV.y0 + H), 1000);
    return true;
  case 12: {
    const lp = clampi(idiv((idiv(p * (W + 6), 1000) - k) * 1000, 6), 0, 1000);
    if (!out) s.x += idiv((1000 - lp) * (32 - s.x), 1000); else s.x -= idiv(lp * (s.x + 1), 1000);
    return true; }
  case 13: {
    const lp = clampi(idiv(p * (PV.np + 1) - piece * 1000, 2), 0, 1000);
    if (!out) { const e = lp < 700 ? idiv(lp * lp, 490) : 1000 - idiv((lp - 700) * (1000 - lp), 190); s.y -= idiv((1000 - e) * (PV.y0 + H + 2), 1000); }
    else s.y += idiv(idiv(lp * lp, 1000) * (33 - PV.y0), 1000);
    return true; }
  case 14: return (H - 1 - r) * 1000 < q * H;
  case 15: return r * 1000 < q * H;
  case 16: {
    if (q < 40) return false;
    const cx = PV.x0 + idiv(W, 2), cy = PV.y0 + idiv(H, 2);
    s.x = cx + idiv((s.x - cx) * q, 1000); s.y = cy + idiv((s.y - cy) * q, 1000);
    return true; }
  case 17: s.a = idiv(s.a * q, 1000); return true;
  case 18: case 22: {
    const lp = clampi(idiv(p * 3, 2) - (hh % 500), 0, 1000);
    if (!out) { if (!lp) return false; s.y -= idiv((1000 - lp) * (s.y + 1), 1000); if (fx === 22 && lp < 1000) s.green = true; return true; }
    if (fx === 22 && lp) s.green = true;
    s.y += idiv(idiv(lp * lp, 1000) * (33 - s.y), 1000);
    return s.y < 32; }
  case 19: {
    const ox = (hh % 48) - 8, oy = ((hh >>> 8) % 48) - 8;
    if (!out) { if (!p) return false; const e = 1000 - idiv((1000 - p) * (1000 - p), 1000); s.x = ox + idiv((s.x - ox) * e, 1000); s.y = oy + idiv((s.y - oy) * e, 1000); }
    else { s.x += idiv(((s.x - 15) * 3 + ox - 16) * p, 1000); s.y += idiv(((s.y - 15) * 3 + oy - 16) * p, 1000); s.a = idiv(s.a * (1000 - p), 1000); }
    return true; }
  case 21: { if (q < 60) return false; const cy = PV.y0 + idiv(H, 2); s.y = cy + idiv((s.y - cy) * q, 1000); return true; }
  default: return true;
  }
}
function applyHold(hold, holding, now, c, r, piece, s) {
  switch (hold) {
  case 1: if (holding && (Math.floor(now / 400) & 1)) s.a = 0; break;
  case 2: if (holding) s.a = idiv(s.a * (60 + idiv(sin8(Math.floor(now / 6)) * 195, 255)), 255); break;
  case 3: s.y += idiv((sin8(c * 14 + Math.floor(now / 4)) - 128) * 2, 127); break;
  case 4: s.y -= idiv((sin8(Math.floor(now / 4) & 127) - 128) * 3, 127); break;
  case 5: { const h = hsh(Math.floor(now / 60), 7); s.x += (h & 1) - ((h >>> 1) & 1); s.y += ((h >>> 2) & 1) - ((h >>> 3) & 1); break; }
  case 6: { const pos = (Math.floor(now / 25) % 80) - 20, b = s.x + s.y - pos; if (b >= 0 && b < 3) s.white = 200; break; }
  case 7: if (hsh(c * 31 + r, Math.floor(now / 90)) % 17 === 0) s.white = 255; break;
  case 8: if (holding && hsh(Math.floor(now / 70), 3) % 9 === 0) s.a = idiv(s.a, 4); break;
  case 9: s.y += idiv((sin8(piece * 50 + Math.floor(now / 3)) - 128) * 2, 127); break;
  case 10: if (now % 1700 < 160) s.x += (hsh(r, Math.floor(now / 40)) % 5) - 2; break;
  }
}
function fireC(heat) {
  heat = clampi(heat, 0, 255);
  if (heat < 85) return [heat * 3, 0, 0];
  if (heat < 170) return [255, (heat - 85) * 3, 0];
  return [255, 255, (heat - 170) * 3];
}
function textColorC(st, c, r, x, y, piece, now) {
  const c1 = [st[4], st[5], st[6]], c2 = [st[7], st[8], st[9]];
  switch (st[3]) {
  case 1: return hsvc(c * 4 - Math.floor(now / 12), 230, 255);
  case 2: return mixC(c1, c2, c, PV.w - 1);
  case 3: return mixC(c1, c2, r, PV.h - 1);
  case 4: return hsvc(idiv(r * 255, PV.h + 1) + Math.floor(now / 10), 230, 255);
  case 5: return fireC(80 + idiv(r * 175, PV.h > 1 ? PV.h - 1 : 1) - (hsh(c * 64 + r, Math.floor(now / 70)) % 110));
  case 6: return scaleC(c1, hsh(Math.floor(now / 90), 5) % 13 === 0 ? 140 : 255);
  case 7: return (Math.floor((x + y + Math.floor(now / 70)) / 3) & 1) ? c1 : c2;
  case 8: return hsvc(hsh(piece, 99) & 255, 200, 255);
  default: return c1;
  }
}
function pvBackground(st, now) {
  const dim = st[14] || 90;
  if (st[13] === 1) { const c = scaleC([st[7], st[8], st[9]], Math.min(255, dim)); for (let i = 0; i < 1024; i++) pix.set(c, i * 3); return; }
  pix.fill(0);
  if (st[13] >= 2) {                                  // the real animation plays on the panel: a soft stand-in here
    for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) {
      const v = sin8(x * 9 + Math.floor(now / 20)) + sin8(y * 11 - Math.floor(now / 27));
      pix.set(scaleC(hsvc((v >> 2) + Math.floor(now / 60), 200, 255), idiv(dim, 3)), (y * 32 + x) * 3);
    }
  }
}
function pvRender(now) {
  if (!PV.list.length) { pix.fill(0); drawMirror(); return; }
  pvAdvance(now);
  const cm = PV.list[PV.cur], st = cm.style;
  if (cm.kind === 1 && now >= PV.fnext) { PV.fnext = now + 500; const ow = PV.w; pvLoad(PV.cur); if (PV.w !== ow) pvGeometry(); }
  pvBackground(st, now);
  const decor = st[12] >> 4, rtl = (st[12] >> 2) & 1, hold = st[1];
  if (decor & 8) for (let i = 0; i < 32; i++) {
    if (PV.y0 - 3 >= 0) pset(i, PV.y0 - 3, hsvc(i * 8 + Math.floor(now / 10), 255, 60));
    if (PV.y0 + PV.h + 2 < 32) pset(i, PV.y0 + PV.h + 2, hsvc(i * 8 - Math.floor(now / 10), 255, 60));
  }
  if (PV.phase === 3) { drawMirror(); return; }
  const t = now - PV.t0, p = PV.dur ? clampi(Math.floor(t * 1000 / PV.dur), 0, 1000) : 1000, pps = ppsOf(st);
  let x0 = PV.x0, view = 0, cLo, cHi;
  if (PV.marquee) {
    const travel = Math.floor(t * pps / 1000);
    x0 = PV.fin === 1 ? 32 - travel : -PV.w + travel;
    cLo = clampi(-x0 - 3, 0, PV.w); cHi = clampi(35 - x0, 0, PV.w);
  } else {
    if (PV.w > 32) {
      let v = PV.phase === 1 ? (t > 700 ? Math.floor((t - 700) * pps / 1000) : 0) : PV.phase === 2 ? PV.w - 32 : 0;
      v = clampi(v, 0, PV.w - 32);
      view = rtl ? (PV.w - 32) - v : v;
    }
    cLo = view; cHi = view + PV.win;
  }
  const fx = PV.marquee ? 0 : PV.phase === 0 ? PV.fin : PV.phase === 2 ? PV.fout : 0, out = PV.phase === 2, holding = PV.phase === 1;
  let piece0 = 0, gap = 1;
  for (let c = 0; c < cLo; c++) { if (PV.cols[c]) { if (gap) piece0++; gap = 0; } else gap = 1; }
  const gap0 = gap;
  for (let pass = 0; pass < 2; pass++) {
    if (pass === 0 && !(decor & 7)) continue;
    let piece = piece0; gap = gap0;
    for (let c = cLo; c < cHi; c++) {
      const bits = PV.cols[c] || 0;
      if (!bits) { gap = 1; continue; }
      if (gap) { piece++; gap = 0; }
      const k = c - view;
      for (let r = 0; r < PV.h; r++) {
        if (!((bits >> r) & 1)) continue;
        const s = { x: x0 + (c - view), y: PV.y0 + r, a: 255, green: false, white: 0 };
        if (fx && !applyFx(fx, out, p, k, c, r, piece - 1, rtl, s)) continue;
        applyHold(hold, holding, now, c, r, piece - 1, s);
        if (s.a <= 0) continue;
        let col = s.green ? [40, 255, 90] : textColorC(st, c, r, s.x, s.y, piece - 1, now);
        if (s.white) col = mixC(col, [255, 255, 255], s.white, 255);
        if (pass === 0) {
          if (decor & 4) for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) padd(s.x + dx, s.y + dy, scaleC(col, 45));
          if (decor & 2) for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) pset(s.x + dx, s.y + dy, [0, 0, 0]);
          if (decor & 1) pset(s.x + 1, s.y + 1, [0, 0, 0]);
        } else pset(s.x, s.y, s.a >= 255 ? col : scaleC(col, s.a));
      }
    }
  }
  if ((fx === 5 || fx === 6 || fx === 20) && p < 1000) {
    const q = out ? 1000 - p : p, kk = idiv(q * PV.win, 1000), ex = x0 + (rtl ? PV.win - 1 - kk : kk);
    if (fx === 5) { if (Math.floor(now / 150) & 1) for (let y = 0; y < PV.h; y++) pset(ex, PV.y0 + y, textColorC(st, kk + view, 0, ex, PV.y0, 0, now)); }
    else if (fx === 6) { for (let y = -1; y <= PV.h; y++) { pset(ex, PV.y0 + y, [255, 255, 255]); pset(ex + 1, PV.y0 + y, [255, 255, 255]); } }
    else {
      const ty = PV.y0 + (hsh(Math.floor(now / 40), 11) % PV.h), sx = rtl ? 0 : 31, n = Math.max(Math.abs(ex - sx), ty) || 1;
      for (let i = 0; i <= n; i++) pset(sx + Math.round((ex - sx) * i / n), Math.round(ty * i / n), [255, 40, 40]);
      pset(ex, ty, [255, 255, 255]);
    }
  }
  drawMirror();
}
function pvTick(ts) {
  if (!PV.running) return;
  PV.raf = requestAnimationFrame(pvTick);
  if (ts - PV.last < 24) return;
  PV.last = ts;
  pvRender(ts);
}
/* list: compiled messages (text.js); starts at message `first` */
export function startTextPreview(list, first) {
  claimMirror("text", stopTextPreview);
  PV.list = list;
  if (!list.length) { stopTextPreview(); pix.fill(0); drawMirror(); return; }
  if (!is32()) {                                      /* this is the firmware's text engine for 32 x 32 */
    stopTextPreview(); pix.fill(0); drawMirror();
    mirrorNote(`Text effects are drawn for 32 × 32 panels for now; this one is ${sizeText()}.`);
    return;
  }
  pvStartMsg(Math.min(first, list.length - 1), performance.now(), true);
  mirrorNote("Text preview, as it plays on the panel." + (list.some((c) => c.style[13] >= 2) ? " Background animations show on the panel only." : ""));
  if (!PV.running) { PV.running = true; PV.raf = requestAnimationFrame(pvTick); }
}
export function stopTextPreview() { PV.running = false; cancelAnimationFrame(PV.raf); }
// Generated animations: an emoji or a few letters with a motion, or a formula for the colour of every LED.
import { rgbOf } from "./import.js?v=62301d0";

/* x, y: the LED (from 0), w, h: the panel's size, cx, cy: its middle, t: seconds, i: the LED's number */
export const FORMULAS = [
  ["Plasma", "hsv(sin(x/5 + t) + sin(y/4 - t*1.3) + sin((x + y)/6 + t*0.7), 1, 1)"],
  ["Rings", "hsv(hypot(x - cx, y - cy)/12 - t/2, 1, 1)"],
  ["Spiral", "hsv(atan2(y - cy, x - cx)/(2*PI) + hypot(x - cx, y - cy)/24 - t/2, 1, 1)"],
  ["Pulse", "hsv(0.95, 0.7, max(0, 1 - abs(hypot(x - cx, y - cy) - t*16 % 24)/3))"],
  ["Rain", "((y - t*(14 + x*7 % 5*5) + x*13) % 16 + 16) % 16 < 4 ? [40, 200, 255] : [0, 0, 18]"],
  ["Checkers", "((x >> 2) + (y >> 2) + floor(t*4)) % 2 ? hsv(t/4, 1, 1) : [0, 0, 0]"],
  ["Sunset sea", "y < 19 + sin(x/3 + t*2) ? hsv(0.03 + y/110, 0.9, 1 - y/36) : hsv(0.58, 0.8, 0.3 + 0.2*sin(x/2 - t*4 + y))"],
];
export const MOTIONS = [["beat", "Heartbeat"], ["bounce", "Bounce"], ["spin", "Spin"], ["wobble", "Wobble"], ["float", "Float"], ["zoom", "Zoom in and out"], ["flash", "Rainbow flash"]];
const MATH = ["sin", "cos", "tan", "abs", "sqrt", "floor", "ceil", "round", "min", "max", "atan2", "hypot", "pow", "exp", "log", "sign", "random", "PI"];

function hsv01(h, s = 1, v = 1) {
  h = ((h % 1) + 1) % 1; s = Math.max(0, Math.min(1, s)); v = Math.max(0, Math.min(1, v));
  const i = Math.floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  const [r, g, b] = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6];
  return [r * 255, g * 255, b * 255];
}

export function formulaFrames(src, secs, fps, w = 32, h = 32) {
  let fn;
  try { fn = new Function(...MATH, "hsv", "x", "y", "t", "i", "w", "h", "cx", "cy", `"use strict"; return (${src}\n);`); }
  catch (e) { throw new Error("The formula has a mistake: " + e.message); }
  const args = MATH.map((k) => Math[k]), n = Math.round(secs * fps), out = [];
  for (let f = 0; f < n; f++) {
    const rgb = new Uint8Array(w * h * 3), t = f / fps;
    for (let i = 0; i < w * h; i++) {
      let c;
      try { c = fn(...args, hsv01, i % w, Math.floor(i / w), t, i, w, h, (w - 1) / 2, (h - 1) / 2); } catch (e) { throw new Error("The formula failed: " + e.message); }
      if (typeof c === "number") c = [c * 255, c * 255, c * 255];
      if (!Array.isArray(c)) throw new Error("The formula must give [r, g, b] or hsv(…).");
      for (let k = 0; k < 3; k++) rgb[i * 3 + k] = Math.max(0, Math.min(255, Math.round(Number(c[k]) || 0)));
    }
    out.push({ rgb, delay: 1000 / fps });
  }
  return out;
}

const bump = (p, c, w) => Math.exp(-(((p - c) * w) ** 2));
export function emojiFrames({ text, motion, ink, bg, secs, fps, w = 32, h = 32 }) {
  text = text.trim() || "❤️";
  const n = Math.round(secs * fps), out = [];
  const BW = w * 4, BH = h * 4, box = Math.min(BW, BH) * 0.875;      /* drawn 4x, the emoji in a square in the middle */
  const big = document.createElement("canvas"); big.width = BW; big.height = BH;
  const bctx = big.getContext("2d"), c = document.createElement("canvas"); c.width = w; c.height = h;
  const ctx = c.getContext("2d", { willReadFrequently: true });
  const family = '"Segoe UI Emoji", "Apple Color Emoji", "Noto Color Emoji", "Segoe UI", system-ui, sans-serif';
  const u = box / 112;                                 // the motions were laid out in a 112 px box
  let size = Math.round(104 * u);
  for (; size > 6; size -= 2) {                        // largest size that fits the box
    bctx.font = `bold ${size}px ${family}`;
    const m = bctx.measureText(text);
    if (m.width <= box && m.actualBoundingBoxAscent + m.actualBoundingBoxDescent <= box) break;
  }
  const m = bctx.measureText(text), yMid = (m.actualBoundingBoxAscent - m.actualBoundingBoxDescent) / 2;
  for (let f = 0; f < n; f++) {
    const p = f / n, tau = Math.PI * 2;
    bctx.setTransform(1, 0, 0, 1, 0, 0);
    bctx.filter = "none";
    bctx.fillStyle = bg; bctx.fillRect(0, 0, BW, BH);
    bctx.translate(BW / 2, BH / 2);
    let sx = 1, sy = 1;
    switch (motion) {
    case "beat": sx = sy = 0.82 + 0.2 * (bump(p, 0.12, 14) + 0.75 * bump(p, 0.32, 14)); break;
    case "bounce": { const hh = Math.abs(Math.sin(p * tau)); bctx.translate(0, (22 - hh * 40) * u); const sq = Math.max(0, 1 - hh * 5) * 0.22; sx = 0.8 + sq; sy = 0.8 - sq; break; }
    case "spin": bctx.rotate(p * tau); sx = sy = 0.85; break;
    case "wobble": bctx.rotate(Math.sin(p * tau * 2) * 0.35); sx = sy = 0.85; break;
    case "float": bctx.translate(Math.sin(p * tau) * 12 * u, Math.sin(p * tau * 2) * 8 * u); sx = sy = 0.8; break;
    case "zoom": sx = sy = 0.35 + 0.6 * (0.5 - 0.5 * Math.cos(p * tau)); break;
    case "flash": bctx.filter = `hue-rotate(${Math.round(p * 360)}deg) saturate(1.6)`; sx = sy = 0.9; break;
    }
    bctx.scale(sx, sy);
    bctx.font = `bold ${size}px ${family}`;
    bctx.textAlign = "center"; bctx.textBaseline = "alphabetic";
    bctx.fillStyle = motion === "flash" ? "#ff3c3c" : ink;
    bctx.fillText(text, 0, yMid);
    ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = "high";
    ctx.drawImage(big, 0, 0, w, h);
    out.push({ rgb: rgbOf(ctx.getImageData(0, 0, w, h).data), delay: 1000 / fps });
  }
  return out;
}

// A w x h drawing surface (the panel's size): pen, eraser, fill, colour picker, strokes joined so fast moves
// leave no gaps. Used by the animation frame editor and by Draw. The frame is a Uint8Array of w*h*3 owned
// by the caller; resize() changes the grid (the caller hands in a frame of the new size).
import { hexRgb, rgbHex } from "../core.js?v=62301d0";

export class PixelCanvas {
  constructor(canvas, { onPaint = () => {}, onPick = () => {}, onion = () => null, w = 32, h = 32 } = {}) {
    this.cv = canvas; this.ctx = canvas.getContext("2d");
    this.onPaint = onPaint; this.onPick = onPick; this.onion = onion;
    this.tool = "pen"; this.colour = "#ff3355";
    this.last = null; this.painting = false;
    this.resize(w, h);
    canvas.addEventListener("pointerdown", (e) => { this.painting = true; this.last = null; canvas.setPointerCapture(e.pointerId); this.stroke(e); });
    canvas.addEventListener("pointermove", (e) => {
      if (!this.painting || this.tool === "fill" || this.tool === "pick") return;
      for (const ev of e.getCoalescedEvents ? e.getCoalescedEvents() : [e]) this.stroke(ev);
    });
    for (const t of ["pointerup", "pointercancel"]) canvas.addEventListener(t, () => { this.painting = false; this.last = null; });
  }
  resize(w, h) {
    this.w = w; this.h = h;
    const cell = Math.max(4, Math.floor(640 / Math.max(w, h)));
    this.cv.width = w * cell; this.cv.height = h * cell;
    this.cv.style.aspectRatio = `${w} / ${h}`;
    this.frame = new Uint8Array(w * h * 3);
  }
  setFrame(f) { this.frame = f; this.draw(); }
  cell(e) {
    const r = this.cv.getBoundingClientRect();
    return [Math.floor((e.clientX - r.left) / r.width * this.w), Math.floor((e.clientY - r.top) / r.height * this.h)];
  }
  set(x, y, c, changed) {
    if (x < 0 || x >= this.w || y < 0 || y >= this.h) return;
    const i = (y * this.w + x) * 3;
    if (this.frame[i] === c[0] && this.frame[i + 1] === c[1] && this.frame[i + 2] === c[2]) return;
    this.frame.set(c, i);
    changed.push([x, y, c]);
  }
  fill(x, y, c, changed) {
    const f = this.frame, w = this.w, i0 = (y * w + x) * 3, from = [f[i0], f[i0 + 1], f[i0 + 2]];
    if (from.every((v, k) => v === c[k])) return;
    const st = [[x, y]];
    while (st.length) {
      const [cx, cy] = st.pop();
      if (cx < 0 || cx >= w || cy < 0 || cy >= this.h) continue;
      const i = (cy * w + cx) * 3;
      if (f[i] !== from[0] || f[i + 1] !== from[1] || f[i + 2] !== from[2]) continue;
      this.set(cx, cy, c, changed);
      st.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
    }
  }
  stroke(e) {
    const [x, y] = this.cell(e);
    if (x < 0 || x >= this.w || y < 0 || y >= this.h) return;
    const changed = [];
    if (this.tool === "pick") {
      const i = (y * this.w + x) * 3;
      this.onPick(rgbHex([this.frame[i], this.frame[i + 1], this.frame[i + 2]]));
      return;
    }
    const c = this.tool === "erase" ? [0, 0, 0] : hexRgb(this.colour);
    if (this.tool === "fill") this.fill(x, y, c, changed);
    else {
      let [x0, y0] = this.last || [x, y];
      const dx = Math.abs(x - x0), dy = -Math.abs(y - y0), sx = x0 < x ? 1 : -1, sy = y0 < y ? 1 : -1;
      let err = dx + dy;
      for (;;) {                                       // Bresenham from the previous point
        this.set(x0, y0, c, changed);
        if (x0 === x && y0 === y) break;
        const e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
      this.last = [x, y];
    }
    if (changed.length) { this.draw(); this.onPaint(changed); }
  }
  draw() {
    const s = this.cv.width / this.w, f = this.frame, prev = this.onion(), w = this.w;
    this.ctx.fillStyle = "#07080a";
    this.ctx.fillRect(0, 0, this.cv.width, this.cv.height);
    for (let y = 0; y < this.h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3;
      let r = f[i], g = f[i + 1], b = f[i + 2];
      if (r + g + b === 0 && prev && prev[i] + prev[i + 1] + prev[i + 2] > 0) { r = prev[i] >> 2; g = prev[i + 1] >> 2; b = prev[i + 2] >> 2; }
      this.ctx.fillStyle = r + g + b > 0 ? `rgb(${r},${g},${b})` : "#15171b";
      this.ctx.fillRect(x * s + 1, y * s + 1, s - 2, s - 2);
    }
  }
}

export const PALETTE = ["#ffffff", "#ff3355", "#ff8a00", "#ffd400", "#3ddc84", "#00c2ff", "#3d5afe", "#b44dff", "#ff4fd8", "#7a4a2a", "#000000"];

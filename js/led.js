// The panel's 5x7 font (same table as the firmware) and LED-dot drawing for the wordmark.
import { FONT5X7 } from "./font5x7.js?v=b9006af";

export function glyph(ch) {
  let c = ch.charCodeAt(0);
  if (c < 32 || c > 126) c = 63;
  return FONT5X7[c - 32];
}
export function glyphSpan(g) {
  let first = 0, last = 4;
  while (first < 4 && g[first] === 0) first++;
  while (last > first && g[last] === 0) last--;
  return [first, last];
}
/* text -> columns (bit 0 = top row), trimmed glyphs, one empty column between letters, 3 for a space */
export function textColumns(text) {
  const cols = [];
  for (const ch of text) {
    if (ch === " ") { cols.push(0, 0, 0); continue; }
    const g = glyph(ch), [a, b] = glyphSpan(g);
    for (let i = a; i <= b; i++) cols.push(g[i]);
    cols.push(0);
  }
  if (cols.length) cols.pop();
  return cols;
}

/* the name in LED dots, each letter in its own colour (hue per letter), on a canvas of height h px */
export function drawDotText(canvas, text, h, colourOf = () => "#edebe6") {
  const cols = textColumns(text), scale = h / 7.5, dpr = window.devicePixelRatio || 1;
  const w = Math.ceil(cols.length * scale);
  canvas.style.width = w + "px"; canvas.style.height = h + "px";
  canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);
  let letter = 0, gap = false;
  for (let x = 0; x < cols.length; x++) {
    if (!cols[x]) { if (!gap) letter++; gap = true; continue; }
    gap = false;
    ctx.fillStyle = colourOf(letter);
    for (let y = 0; y < 7; y++) if ((cols[x] >> y) & 1) {
      ctx.beginPath();
      ctx.arc(x * scale + scale / 2, y * scale + scale / 2, scale * 0.42, 0, Math.PI * 2);
      ctx.fill();
    }
  }
}
export const hueColour = (i, n = 15) => `hsl(${Math.round((i * 360) / n)}, 80%, 66%)`;

// The panel preview in the device column: a w x h RGB buffer (the connected panel's size, size.js) drawn as
// round LEDs. One part of the page at a time "owns" it: normally "panel" (virtual.js: what the panel shows
// right now), or a view showing a preview of something not sent yet (text, animation, picture, drawing).
// Taking it stops the previous owner.
import { $, on, emit } from "./core.js?v=8f6fef3";
import { size } from "./size.js?v=8f6fef3";

export let pix = new Uint8Array(32 * 32 * 3);          /* a live binding: replaced when the size changes */
let canvas = null, ctx = null, owner = null, stopOwner = null, w = 32, h = 32;

function fit() {
  w = size.w; h = size.h;
  pix = new Uint8Array(w * h * 3);
  const s = Math.max(4, Math.floor(512 / Math.max(w, h)));     /* LED pitch in canvas pixels */
  canvas.width = w * s;
  canvas.height = h * s;
  canvas.style.aspectRatio = `${w} / ${h}`;
  drawMirror();
}
export function initMirror() {
  canvas = $("#mirror");
  ctx = canvas.getContext("2d");
  $("#mirrorTag").addEventListener("click", () => { if (owner !== "panel") emit("show-live"); });
  fit();
  on("size", ({ changed }) => { if (changed) fit(); });
}
export function setPix(x, y, r, g, b) { if (x < 0 || y < 0 || x >= w || y >= h) return; const i = (y * w + x) * 3; pix[i] = r; pix[i + 1] = g; pix[i + 2] = b; }
export function drawMirror() {
  if (!ctx) return;
  const s = canvas.width / w;
  ctx.fillStyle = "#07080a";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 3, r = pix[i], g = pix[i + 1], b = pix[i + 2], lit = r + g + b > 24;
      ctx.fillStyle = lit ? `rgb(${r},${g},${b})` : "#17191d";
      ctx.beginPath();
      ctx.arc(x * s + s / 2, y * s + s / 2, s * (lit ? 0.43 : 0.36), 0, Math.PI * 2);
      ctx.fill();
    }
}
/* the label in the preview's corner: live (on the panel), idle (nothing connected) or preview (not sent) */
const TAGS = {
  live: ["On the panel", "What the panel shows right now"],
  idle: ["Not connected", "Connect a panel to see what it shows"],
  preview: ["Preview", "Not on the panel yet. Click to see what the panel shows."],
};
export function mirrorTag(kind) {
  const tag = $("#mirrorTag");
  if (!tag || tag.dataset.kind === kind) return;
  tag.dataset.kind = kind;
  [tag.textContent, tag.title] = TAGS[kind];
  tag.disabled = kind !== "preview";
}
/* take the preview; stop() is called when something else takes it */
export function claimMirror(name, stop) {
  if (owner !== name && stopOwner) stopOwner();
  owner = name;
  stopOwner = stop || null;
  if (name !== "panel") mirrorTag("preview");
}
export const mirrorOwner = () => owner;
export function mirrorNote(text) { const n = $("#mirrorNote"); if (n) n.textContent = text; }
export const mirrorCanvas = () => canvas;

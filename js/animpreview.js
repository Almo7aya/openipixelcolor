// Moving previews of the animations stored on the panel, decoded from this browser's copy (idb.js) exactly
// as the panel plays them. Animations sent from another device have no copy here: their saved picture, if
// any, stands in. One animation frame loop drives every preview on the page.
import { reducedMotion } from "./core.js?v=57d8ff9";
import { getAnim } from "./idb.js?v=57d8ff9";
import OPA from "./create/anim-codec.js?v=57d8ff9";

const decoded = new Map();                              // crc -> Promise<frames | null>
const playing = new Set();                              // { cv, frames, i, due }
let raf = 0;

export function framesOf(crc) {
  if (!decoded.has(crc)) {
    const p = getAnim(crc).then((b) => (b ? OPA.decode(b) : null)).catch(() => null);
    decoded.set(crc, p);
    p.then((f) => { if (!f) decoded.delete(crc); });     // it may be saved here later: look again next time
  }
  return decoded.get(crc);
}
function paint(cv, rgb) {
  const ctx = cv.getContext("2d"), im = ctx.createImageData(cv.width, cv.height);
  for (let i = 0; i < cv.width * cv.height; i++) { im.data[i * 4] = rgb[i * 3]; im.data[i * 4 + 1] = rgb[i * 3 + 1]; im.data[i * 4 + 2] = rgb[i * 3 + 2]; im.data[i * 4 + 3] = 255; }
  ctx.putImageData(im, 0, 0);
}
function tick(t) {
  raf = 0;
  for (const p of playing) {
    if (!p.cv.isConnected) { playing.delete(p); continue; }
    if (t < p.due || !p.cv.offsetParent) continue;      // not due, or not on screen
    p.i = (p.i + 1) % p.frames.length;
    paint(p.cv, p.frames[p.i].rgb);
    p.due = t + Math.max(OPA.MIN_DELAY, p.frames[p.i].delay);
  }
  if (playing.size) raf = requestAnimationFrame(tick);
}

/* a canvas for the animation with this checksum, at its own size: moving when this browser has the file, else
   its picture; resolves to "moving", "still" or "" (nothing to show) */
export async function previewInto(cv, crc, thumb) {
  const frames = await framesOf(crc);
  if (frames && frames.length) {
    cv.width = frames.w; cv.height = frames.h;
    cv.style.aspectRatio = `${frames.w} / ${frames.h}`;
    paint(cv, frames[0].rgb);
    if (frames.length > 1 && !reducedMotion()) {
      playing.add({ cv, frames, i: 0, due: performance.now() + frames[0].delay });
      if (!raf) raf = requestAnimationFrame(tick);
    }
    return "moving";
  }
  if (thumb) {
    try {
      const img = new Image(); img.src = thumb; await img.decode();
      cv.width = img.naturalWidth; cv.height = img.naturalHeight;
      cv.getContext("2d").drawImage(img, 0, 0);
      return "still";
    }
    catch (e) { /* unreadable picture */ }
  }
  return "";
}

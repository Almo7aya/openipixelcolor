// Draw: a canvas the panel's size. With live painting on, the first stroke puts the whole drawing on the
// panel (0xF103) and every stroke after that sends only the changed LEDs (0xF105, up to 47 per packet).
// The drawing is kept in this browser, one per panel size.
import { $, el, on, store, segmented } from "../core.js?v=ed0e7b5";
import { CMD, MODE } from "../protocol.js?v=ed0e7b5";
import { panel, send, act } from "../ble.js?v=ed0e7b5";
import { pix, drawMirror, claimMirror, mirrorNote } from "../mirror.js?v=ed0e7b5";
import { size } from "../size.js?v=ed0e7b5";
import { toast, toastError } from "../toast.js?v=ed0e7b5";
import { PixelCanvas, PALETTE } from "./pixelcanvas.js?v=ed0e7b5";

let frame = new Uint8Array(32 * 32 * 3), w = 32, h = 32;
const undo = [];
let pc = null, onPanel = false, dirty = new Map(), flushTimer = 0, saveTimer = 0;

const toB64 = (u8) => { let s = ""; for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode(...u8.subarray(i, i + 4096)); return btoa(s); };
const key = () => `draw.${w}x${h}`;
function keep() { clearTimeout(saveTimer); saveTimer = setTimeout(() => store.set(key(), toB64(frame)), 400); }
function load() {                                       /* the drawing for the panel's size */
  w = size.w; h = size.h;
  frame = new Uint8Array(w * h * 3);
  const saved = store.get(key(), w === 32 && h === 32 ? store.get("draw", null) : null);
  if (saved) try { const b = Uint8Array.from(atob(saved), (c) => c.charCodeAt(0)); if (b.length === frame.length) frame.set(b); } catch (e) { /* empty */ }
  undo.length = 0;
  if (pc) { pc.resize(w, h); pc.setFrame(frame); $("#dUndo").disabled = true; }
}
function mirror() {
  claimMirror("draw");
  if (pix.length === frame.length) pix.set(frame);
  drawMirror();
  mirrorNote($("#dLive").checked && panel.chWrite ? "Your drawing. Strokes appear on the panel as you draw." : "Your drawing.");
}

async function sendWhole() { await send(CMD.PICTURE, [...frame], 8000); onPanel = true; dirty.clear(); }
async function flush() {
  const items = [...dirty.entries()];
  dirty.clear();
  try {
    if (!onPanel) { await sendWhole(); return; }
    for (let k = 0; k < items.length; k += 47) {
      const payload = [];
      for (const [idx, rgb] of items.slice(k, k + 47)) payload.push(idx % w, Math.floor(idx / w), ...rgb);
      await send(CMD.PIXELS, payload);
    }
  } catch (e) { toastError(e.message); }
}
function painted(changed) {
  for (const [x, y, c] of changed) dirty.set(y * w + x, c);
  mirror(); keep();
  if ($("#dLive").checked && panel.chWrite) { clearTimeout(flushTimer); flushTimer = setTimeout(flush, 90); }
}
function replaced() {                                 // undo, clear: the whole drawing changed
  pc.draw(); mirror(); keep();
  if ($("#dLive").checked && panel.chWrite) { onPanel = false; clearTimeout(flushTimer); flushTimer = setTimeout(flush, 90); }
}
function doUndo() {
  if (!undo.length) return;
  frame.set(undo.pop());
  $("#dUndo").disabled = !undo.length;
  replaced();
}

export function initDraw() {
  const cv = $("#dEd");
  cv.addEventListener("pointerdown", () => {           // runs before the stroke: what to go back to
    undo.push(frame.slice());
    if (undo.length > 40) undo.shift();
    $("#dUndo").disabled = false;
  }, { capture: true });
  pc = new PixelCanvas(cv, {
    w, h, onPaint: painted,
    onPick: (hex) => { $("#dPen").value = hex; pc.colour = hex; pc.tool = "pen"; tools.set("pen"); },
  });
  load();
  const tools = segmented($("#dTool"), (t) => { pc.tool = t; });
  $("#dPen").addEventListener("input", (e) => { pc.colour = e.target.value; });
  PALETTE.forEach((c) => $("#dPal").append(el("button", {
    style: `background:${c}`, title: c, "aria-label": `Colour ${c}`,
    onclick: () => { $("#dPen").value = c; pc.colour = c; pc.tool = "pen"; tools.set("pen"); },
  })));
  $("#dUndo").addEventListener("click", doUndo);
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "z" && !$("#create-draw").hidden && !$("#view-create").hidden && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) { e.preventDefault(); doUndo(); }
  });
  $("#dClear").addEventListener("click", () => {
    undo.push(frame.slice()); $("#dUndo").disabled = false;
    frame.fill(0); pc.draw(); mirror(); keep();
    if (panel.chWrite) act(async () => { await send(CMD.CANVAS, [1]); onPanel = true; dirty.clear(); });
  });
  $("#dSend").addEventListener("click", () => { mirror(); act(async () => { await sendWhole(); toast("Drawing sent. The panel keeps it after switching off.", { kind: "ok" }); }); });
  $("#dLive").addEventListener("change", (e) => { store.set("draw.live", e.target.checked); mirror(); });
  $("#dLive").checked = store.get("draw.live", true);
  on("status", (r) => { if (r[5] !== MODE.CANVAS) onPanel = false; });
  on("disconnected", () => { onPanel = false; });
  on("size", ({ changed }) => { if (changed) { load(); onPanel = false; } });
  on("view", ({ dest, sub }) => { if (dest === "create" && sub === "draw") mirror(); else onPanel = false; });
}

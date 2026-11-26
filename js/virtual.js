// What the panel shows, in the panel preview. panel.wasm is the firmware's own display code
// (firmware/src/app, built by tools/wasm/build.py): it gets every packet this page sends to the panel, and
// each status poll (every 4 s) puts it back in step if the panel changed by itself (its button, shuffle,
// another phone). Effects, clocks, text, pictures and animations sent from this browser come out the same
// as on the panel; random effects (stars, fireworks) differ in the details.
import { on, store } from "./core.js?v=7d6fc0c";
import { CMD, MODE, packet, u32, rd32 } from "./protocol.js?v=7d6fc0c";
import { panel, onPacket } from "./ble.js?v=7d6fc0c";
import { pix, drawMirror, claimMirror, mirrorOwner, mirrorNote, mirrorTag } from "./mirror.js?v=7d6fc0c";
import { getAnim } from "./idb.js?v=7d6fc0c";
import { is32, sizeText } from "./size.js?v=7d6fc0c";
import OPA from "./create/anim-codec.js?v=7d6fc0c";

let vp = null, synced = false, nowCaption = "", missing = null;
const now = () => Math.floor(performance.now()) >>> 0;
const mem = () => new Uint8Array(vp.memory.buffer);

/* ---- talking to the virtual panel ---- */
function feed(bytes) {
  if (!vp || bytes.length > 8192) return null;
  const c = bytes[2] | (bytes[3] << 8);
  if (c === CMD.OTA_BEGIN || c === CMD.OTA_CHUNK) return null;
  mem().set(bytes, vp.vp_in());
  const r = vp.vp_cmd(bytes.length, now());
  const at = vp.vp_reply();
  return { r, reply: mem().slice(at, at + 16) };
}
const cmd = (c, payload = []) => feed(packet(c, payload));

/* what the page sent last, to show it again after the page is reopened */
let lastText = store.get("live.text", []);
let lastCanvas = null;
try { const s = store.get("live.canvas", null); if (s) lastCanvas = Uint8Array.from(atob(s), (ch) => ch.charCodeAt(0)); } catch (e) { lastCanvas = null; }
let keepTimer = 0;
function keepCanvas() {
  clearTimeout(keepTimer);
  keepTimer = setTimeout(() => {
    let s = "";
    for (let i = 0; i < lastCanvas.length; i += 4096) s += String.fromCharCode(...lastCanvas.subarray(i, i + 4096));
    store.set("live.canvas", btoa(s));
  }, 1000);
}
function remember(bytes) {
  const c = bytes[2] | (bytes[3] << 8);
  if (c === CMD.TEXT) {
    if (bytes[4] === 0) lastText = [];
    lastText.push([...bytes]);
    if (bytes[4] === bytes[5] - 1) store.set("live.text", lastText);
  } else if (c === CMD.PICTURE && bytes.length === 4 + 3072) {
    lastCanvas = bytes.slice(4); keepCanvas();
  } else if (c === CMD.PIXELS) {
    lastCanvas ||= new Uint8Array(3072);
    for (let i = 4; i + 5 <= bytes.length; i += 5) if (bytes[i] < 32 && bytes[i + 1] < 32) lastCanvas.set(bytes.subarray(i + 2, i + 5), (bytes[i + 1] * 32 + bytes[i]) * 3);
    keepCanvas();
  } else if (c === CMD.CANVAS && bytes[4]) {
    lastCanvas = new Uint8Array(3072); keepCanvas();
  }
}

/* an animation the panel has, put into the virtual panel's store at the same slot (from this browser's copy) */
async function loadAnimation(slot, crc) {
  const have = cmd(CMD.ANIM_INFO, [slot, 0]);
  if (have && have.reply[4] === 1 && rd32(have.reply, 12) === crc) return true;
  const bytes = await getAnim(crc);
  if (!bytes || !vp) return false;
  const begun = cmd(CMD.ANIM_BEGIN, [slot, ...u32(bytes.length), 0]);
  if (!begun || begun.reply[4] !== 1) return false;
  let res = null;
  for (let off = 0; off < bytes.length; off += OPA.PIECE) {
    const piece = bytes.subarray(off, off + OPA.PIECE), payload = new Uint8Array(4 + piece.length);
    payload.set(u32(off)); payload.set(piece, 4);
    res = feed(packet(CMD.ANIM_DATA, payload));
  }
  return !!res && res.reply[4] === 2;                    // 2 = stored
}

/* ---- keeping it in step with the panel's status (0xF106 reply, see protocol.js) ---- */
function sync(r) {
  if (!vp || !panel.custom || panel.rescue) return;
  const v = cmd(CMD.STATUS).reply;
  if (r[8] !== v[8]) cmd(CMD.POWER, [r[8]]);
  if (r[7] !== v[7]) cmd(CMD.BRIGHTNESS, [r[7]]);
  if (r[9] !== v[9] || r[10] !== v[10] || Math.abs(r[11] - v[11]) > 2) cmd(CMD.TIME, [r[9], r[10], r[11], 0]);
  const mode = r[5], arg = r[6], flags = r[12];
  missing = null;
  switch (mode) {
  case MODE.EFFECT:
    if (v[5] !== mode || v[6] !== arg) {
      cmd(CMD.MODE, [MODE.EFFECT, arg]);
      if (flags & 2) cmd(CMD.AUTOPLAY, [1, r[15], 0]);
    } else if (((v[12] ^ flags) & 2) || ((flags & 2) && v[15] !== r[15])) cmd(CMD.AUTOPLAY, [flags & 2 ? 1 : 0, r[15], 0]);
    break;
  case MODE.CLOCK:
    if (v[5] !== mode || v[14] !== r[14] || ((v[12] ^ flags) & 12)) {
      const d = new Date();
      cmd(CMD.CLOCK, [r[14], flags & 4 ? 1 : 0, flags & 8 ? 1 : 0, d.getFullYear() % 100, d.getMonth() + 1, d.getDate(), d.getDay() || 7]);
    }
    break;
  case MODE.ANIM: {
    const all = !!(flags & 16);
    if (arg === 0xFF) { if (v[5] !== mode) cmd(CMD.MODE, [MODE.ANIM, 0xFF]); break; }
    if (v[5] === mode && (all ? v[12] & 16 : v[6] === arg)) break;
    const res = cmd(CMD.ANIM_PLAY, [all ? 0xFF : arg, 0]);
    if (!res || res.r !== 1) missing = arg;              // not in this browser: the list's picture stands in
    break; }
  case MODE.TEXT:
    if (v[5] !== mode) { if (lastText.length) lastText.forEach((p) => feed(Uint8Array.from(p))); else cmd(CMD.MODE, [mode, 0]); }
    break;
  case MODE.CANVAS:
    if (v[5] !== mode) { if (lastCanvas) cmd(CMD.PICTURE, [...lastCanvas]); else cmd(CMD.CANVAS, [1]); }
    break;
  default:
    if (v[5] !== mode) cmd(CMD.MODE, [mode, arg]);
  }
  synced = true;
}

/* ---- drawing it ---- */
let live = false, raf = 0, last = 0, still = null, welcome = false;
const thumbs = new Map();                                // slot -> RGB of the library picture

function caption() {
  if (!live) return;
  mirrorTag(panel.chWrite ? "live" : "idle");
  if (!panel.chWrite) mirrorNote(panel.state === "connecting" ? "Connecting…" : "The welcome screen a new panel shows.");
  else if (!is32()) mirrorNote(`A ${sizeText()} panel. The live preview is there for 32 × 32 panels for now.`);
  else if (!panel.custom) mirrorNote("The preview needs openipixelcolor on the panel.");
  else if (panel.rescue) mirrorNote("The panel is in rescue mode.");
  else mirrorNote(nowCaption + (missing != null ? " (made on another device: its first frame)" : ""));
}
function frame(t) {
  if (!live) return;
  raf = requestAnimationFrame(frame);
  if (t - last < 33) return;
  last = t;
  /* nothing connected: the first-start screen, as a new panel shows it (the firmware's own welcome) */
  const idle = vp && !panel.chWrite && panel.state !== "connecting" && is32();
  if (idle && !welcome) { vp.vp_init(now()); cmd(CMD.MODE, [MODE.WELCOME, 0]); welcome = true; }
  if (!idle) welcome = false;
  const on = idle || (vp && panel.chWrite && panel.custom && !panel.rescue && synced && is32());   /* panel.wasm is the 32 x 32 build */
  if (!on) { if (still !== "dark") { pix.fill(0); drawMirror(); still = "dark"; } return; }
  still = null;
  if (missing != null && thumbs.get(missing)) { pix.set(thumbs.get(missing)); drawMirror(); return; }
  const f = vp.vp_render(now()), src = new Uint8Array(vp.memory.buffer, f, 3072);
  const k = 0.3 + 0.7 * Math.min(100, vp.vp_bright()) / 100;     // dimmer when the panel is dimmer
  for (let i = 0; i < 3072; i++) pix[i] = src[i] * k;
  drawMirror();
}
function stopLive() { live = false; cancelAnimationFrame(raf); }
export function showLive() {
  if (live && mirrorOwner() === "panel") return;
  claimMirror("panel", stopLive);
  live = true; still = null;
  caption();
  raf = requestAnimationFrame(frame);
}

export async function initVirtual() {
  try {
    const src = await fetch("panel.wasm?v=7d6fc0c");
    const { instance } = await WebAssembly.instantiate(await src.arrayBuffer(), {});
    vp = instance.exports;
    vp.vp_init(now());
  } catch (e) { vp = null; }                             // no preview of the panel; everything else works
  onPacket((bytes) => { remember(bytes); feed(bytes); });
  on("connecting", () => { synced = false; missing = null; welcome = false; if (vp) vp.vp_init(now()); caption(); });
  on("state", caption);
  on("size", () => { still = null; caption(); });
  on("status", (r) => { sync(r); caption(); });
  on("now", (text) => { nowCaption = text; caption(); });
  on("library", async (items) => {
    for (const it of items) {
      if (await loadAnimation(it.slot, it.crc)) thumbs.delete(it.slot);
      else if (it.thumb) thumbs.set(it.slot, await rgbOfImage(it.thumb).catch(() => null));
    }
    if (panel.status) sync(panel.status);
  });
  on("show-live", showLive);
}

async function rgbOfImage(url) {
  const img = new Image();
  img.src = url;
  await img.decode();
  const c = document.createElement("canvas");
  c.width = c.height = 32;
  const x = c.getContext("2d", { willReadFrequently: true });
  x.drawImage(img, 0, 0, 32, 32);
  const d = x.getImageData(0, 0, 32, 32).data, o = new Uint8Array(3072);
  for (let i = 0; i < 1024; i++) { o[i * 3] = d[i * 4]; o[i * 3 + 1] = d[i * 4 + 1]; o[i * 3 + 2] = d[i * 4 + 2]; }
  return o;
}

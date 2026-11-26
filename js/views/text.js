// Text: a playlist of up to 4 messages (cmd 0xF10B, firmware/src/app/textfx.h). One card per message; the
// open card holds the editor. Any script works: the browser draws the text and sends it as a picture strip.
import { $, $$, el, on, emit, store, fillSelect, hexRgb } from "../core.js?v=06a6a24";
import { CMD } from "../protocol.js?v=06a6a24";
import { panel, send, act } from "../ble.js?v=06a6a24";
import { toast, toastError } from "../toast.js?v=06a6a24";
import { EFFECTS, fxIndex } from "../catalog.js?v=06a6a24";
import { startTextPreview } from "./textpreview.js?v=06a6a24";

const FX_NAMES = ["None (just appears)", "Scroll left", "Scroll right", "Scroll up", "Scroll down", "Typewriter", "Wipe",
  "Opening", "Closing", "Blinds", "Dissolve", "Mesh", "Slice", "Drop", "Grow up", "Grow down", "Zoom", "Fade",
  "Rain / melt", "Explode", "Laser", "Flip", "Matrix", "Random"];
const HOLD_NAMES = ["Still", "Blink", "Breathe", "Wave", "Bounce", "Shake", "Shine", "Sparkle", "Neon flicker", "Jelly", "Glitch"];
const CMODE_NAMES = ["One colour", "Rainbow", "Gradient left to right", "Gradient top to bottom", "Rainbow top to bottom",
  "Fire", "Neon", "Candy stripes", "A colour per word"];
const FONTS = [
  { id: "panel", name: "Panel font (live {time})" },
  { id: "sans",  name: "Bold",       css: '"Segoe UI", Tahoma, "Noto Sans Arabic", Arial, sans-serif', w: "bold" },
  { id: "black", name: "Heavy",      css: 'Impact, "Arial Black", "Segoe UI Black", sans-serif',     w: "normal" },
  { id: "serif", name: "Serif",      css: 'Georgia, "Times New Roman", serif',                        w: "bold" },
  { id: "mono",  name: "Typewriter", css: 'Consolas, "Courier New", monospace',                       w: "bold" },
  { id: "round", name: "Playful",    css: '"Comic Sans MS", "Segoe Print", cursive',                  w: "bold" },
];
const SIZES = { panel: [[7, "Normal (7 px)"], [14, "Big (14 px)"]], other: [[11, "Small (11 px)"], [16, "Normal (16 px)"], [24, "Big (24 px)"]] };
const POOL = 3584, MAX = 4;
const RTL = /[֐-ࣿיִ-﷿ﹰ-﻿]/;
const FIELDS = [["#tIn", "fin"], ["#tHold", "hold"], ["#tOut", "fout"], ["#tAlign", "align"], ["#tColor", "cmode"],
                ["#tBg", "bg"], ["#tBgDim", "bgdim"], ["#tSpeed", "speed"], ["#tRepeat", "repeat"]];
const DECOR = ["shadow", "outline", "glow", "rails"];

const newMsg = (text) => ({ text, font: "sans", size: 16, fin: 1, hold: 0, fout: 0, align: 0, cmode: 1, c1: "#ffc850", c2: "#ff3cac",
  bg: 0, bgdim: 90, speed: 5, stay: 8, repeat: 1, shadow: false, outline: false, glow: false, rails: true });
let msgs = store.get("text", null);
msgs = Array.isArray(msgs) && msgs.length ? msgs.slice(0, MAX).map((m) => Object.assign(newMsg(""), m)) : [newMsg("Hello!")];
let cur = 0;
const save = () => store.set("text", msgs);

/* draw text with this computer's fonts (the browser joins Arabic and orders right to left) into a strip
   h px tall, then turn it into dots: column after column, bit 0 = top row */
function renderStrip(msg, h, family, weight) {
  const rtl = RTL.test(msg), c = document.createElement("canvas"), ctx = c.getContext("2d", { willReadFrequently: true });
  let size = h + 2, m;
  for (; size > 6; size--) {
    ctx.font = `${weight} ${size}px ${family}`;
    m = ctx.measureText(msg);
    if (m.actualBoundingBoxAscent + m.actualBoundingBoxDescent <= h) break;
  }
  const w = Math.max(1, Math.min(1024, Math.ceil(m.actualBoundingBoxLeft + m.actualBoundingBoxRight) + 2));
  c.width = w; c.height = h;
  ctx.font = `${weight} ${size}px ${family}`;
  ctx.direction = rtl ? "rtl" : "ltr";
  ctx.textAlign = "left";
  ctx.fillStyle = "#fff";
  ctx.fillText(msg, 1 + m.actualBoundingBoxLeft, Math.round((h - (m.actualBoundingBoxAscent + m.actualBoundingBoxDescent)) / 2 + m.actualBoundingBoxAscent));
  const d = ctx.getImageData(0, 0, w, h).data, bpc = Math.ceil(h / 8);
  let first = w, last = -1;
  const lit = (x, y) => d[(y * w + x) * 4] > 110;
  for (let x = 0; x < w; x++) for (let y = 0; y < h; y++) if (lit(x, y)) { first = Math.min(first, x); last = Math.max(last, x); }
  if (last < 0) return null;
  const cols = last - first + 1, bits = new Uint8Array(cols * bpc);
  for (let x = 0; x < cols; x++) for (let y = 0; y < h; y++) if (lit(first + x, y)) bits[x * bpc + (y >> 3)] |= 1 << (y & 7);
  return { h, w: cols, bpc, rtl, bits };
}

/* message -> { kind, style[16], ... } as the panel takes it */
export function compileMsg(m) {
  const rtl = m.font !== "panel" && RTL.test(m.text), style = new Uint8Array(16);
  style[0] = m.fin; style[1] = m.hold; style[2] = m.fout; style[3] = m.cmode;
  style.set(hexRgb(m.c1), 4); style.set(hexRgb(m.c2), 7);
  style[10] = m.speed; style[11] = m.stay;
  style[12] = m.align | (rtl ? 4 : 0) | (((m.shadow ? 1 : 0) | (m.outline ? 2 : 0) | (m.glow ? 4 : 0) | (m.rails ? 8 : 0)) << 4);
  style[13] = m.bg; style[14] = m.bgdim; style[15] = m.repeat;
  if (m.font === "panel") {
    const text = [...m.text].map((c) => c.charCodeAt(0)).filter((k) => k >= 32 && k <= 126).slice(0, 120);
    return text.length ? { kind: 1, style, scale: m.size >= 14 ? 1 : 0, text, bytes: text.length, rtl: false, raw: m.text } : null;
  }
  if (!m.text.trim()) return null;
  const f = FONTS.find((x) => x.id === m.font) || FONTS[1], s = renderStrip(m.text, m.size, f.css, f.w);
  return s ? { kind: 0, style, strip: s, bytes: s.bits.length, rtl } : null;
}

function sizeOptions(font, keep) {
  const list = font === "panel" ? SIZES.panel : SIZES.other;
  fillSelect($("#tSize"), list);
  const v = list.some(([s]) => s === keep) ? keep : list[font === "panel" ? 0 : 1][0];
  $("#tSize").value = v;
  return v;
}
function toEditor() {
  const m = msgs[cur];
  $("#tMsg").value = m.text;
  $("#tFont").value = m.font;
  m.size = sizeOptions(m.font, m.size);
  for (const [id, k] of FIELDS) $(id).value = m[k];
  $("#tC1").value = m.c1; $("#tC2").value = m.c2;
  $("#tStay").value = Math.max(2, Math.round(m.stay / 2));
  $("#tStayOut").value = (m.stay / 4).toFixed(1) + " s";
  for (const k of DECOR) $("#t_" + k).checked = !!m[k];
}
function fromEditor() {
  const m = msgs[cur], wasRtl = RTL.test(m.text);
  m.text = $("#tMsg").value;
  if (m.font !== $("#tFont").value) { m.font = $("#tFont").value; m.size = sizeOptions(m.font, m.size); }
  else m.size = Number($("#tSize").value);
  for (const [id, k] of FIELDS) m[k] = Number($(id).value);
  m.c1 = $("#tC1").value; m.c2 = $("#tC2").value;
  m.stay = Number($("#tStay").value) * 2;
  $("#tStayOut").value = (m.stay / 4).toFixed(1) + " s";
  for (const k of DECOR) m[k] = $("#t_" + k).checked;
  const isRtl = RTL.test(m.text);                     // Arabic reads right to left: the marquee goes the other way
  if (isRtl !== wasRtl) { if (isRtl && m.fin === 1) m.fin = 2; else if (!isRtl && m.fin === 2) m.fin = 1; $("#tIn").value = m.fin; }
  $("#tFontNote").hidden = !(m.font === "panel" && /[^\x20-\x7E]/.test(m.text));
}
function memory() {
  const used = msgs.reduce((a, m) => { const c = compileMsg(m); return a + (c ? c.bytes : 0); }, 0);
  $("#tMem").textContent = used > POOL ? "Too long for the panel: make the messages shorter or smaller." : "";
  return used;
}

/* the cards; the editor element moves into the open one */
function renderCards() {
  const box = $("#msgs"), editor = $("#msgEditor");
  box.replaceChildren();
  msgs.forEach((m, i) => {
    const open = i === cur;
    const card = el("article", { class: "msg", "aria-expanded": String(open) });
    const openIt = () => { if (cur !== i) { cur = i; renderCards(); toEditor(); preview(); } };
    const head = el("div", { class: "msg-head" },
      el("button", { class: "open", "aria-expanded": String(open), onclick: openIt },
        el("span", { class: "n" }, String(i + 1)), el("span", { class: "t", dir: "auto" }, m.text)),
      el("button", { class: "btn ghost small", title: "Move up", "aria-label": `Move message ${i + 1} up`, disabled: i === 0, onclick: () => move(i, -1) }, "↑"),
      el("button", { class: "btn ghost small", title: "Move down", "aria-label": `Move message ${i + 1} down`, disabled: i === msgs.length - 1, onclick: () => move(i, 1) }, "↓"),
      el("button", { class: "btn ghost small", "aria-label": `Remove message ${i + 1}`, disabled: msgs.length === 1, onclick: () => remove(i) }, "Remove"));
    card.append(head);
    if (open) { const body = el("div", { class: "msg-body" }); body.append(editor); editor.hidden = false; card.append(body); }
    box.append(card);
  });
  $("#tAdd").disabled = msgs.length >= MAX;
}
function move(i, d) {
  const j = i + d;
  [msgs[i], msgs[j]] = [msgs[j], msgs[i]];
  if (cur === i) cur = j; else if (cur === j) cur = i;
  save(); renderCards(); toEditor(); preview();
}
function remove(i) {
  msgs.splice(i, 1);
  cur = Math.min(cur, msgs.length - 1);
  save(); renderCards(); toEditor(); preview();
}
function preview() { startTextPreview(msgs.map(compileMsg).filter(Boolean), cur); }
function edited() { fromEditor(); save(); $$("#msgs .msg")[cur].querySelector(".t").textContent = msgs[cur].text; memory(); preview(); }

const PRESETS = [
  ["Marquee", (m) => ({ fin: RTL.test(m.text) ? 2 : 1, hold: 0, fout: 0, cmode: 1, bg: 0, rails: true, glow: false, outline: false, shadow: false })],
  ["Typewriter", (m) => ({ font: RTL.test(m.text) ? m.font : "mono", fin: 5, hold: 0, fout: 5, cmode: 0, c1: "#7cff8a", bg: 0, rails: false, glow: false })],
  ["Neon sign", () => ({ fin: 17, hold: 8, fout: 17, cmode: 6, c1: "#ff3cac", glow: true, outline: false, bg: 0, rails: false })],
  ["Breaking news", () => ({ fin: 6, hold: 6, fout: 6, cmode: 0, c1: "#ffffff", c2: "#c81e2d", bg: 1, bgdim: 150, outline: true, rails: false, glow: false })],
  ["Matrix", () => ({ fin: 22, hold: 0, fout: 22, cmode: 0, c1: "#3cff5a", bg: 2 + fxIndex("Matrix rain"), bgdim: 60, outline: true, rails: false, glow: false })],
  ["Love", () => ({ fin: 13, hold: 9, fout: 19, cmode: 2, c1: "#ff5aa0", c2: "#b45aff", bg: 2 + fxIndex("Sparkles"), bgdim: 70, glow: true, outline: false, rails: false })],
  ["Party", () => ({ fin: 19, hold: 4, fout: 19, cmode: 1, bg: 2 + fxIndex("Fireworks"), bgdim: 90, outline: true, glow: false, rails: false })],
  ["Fire", () => ({ fin: 18, hold: 8, fout: 18, cmode: 5, bg: 0, shadow: true, outline: false, glow: false, rails: false })],
  ["Surprise me", () => ({ fin: 1 + Math.floor(Math.random() * 22), hold: Math.floor(Math.random() * 11), fout: 1 + Math.floor(Math.random() * 22),
    cmode: Math.floor(Math.random() * 9), c1: "#" + Math.floor(Math.random() * 0xffffff).toString(16).padStart(6, "0"),
    c2: "#" + Math.floor(Math.random() * 0xffffff).toString(16).padStart(6, "0"),
    bg: Math.random() < 0.5 ? 0 : 2 + Math.floor(Math.random() * EFFECTS.length), bgdim: 80, outline: true, glow: Math.random() < 0.5, rails: false })],
];

export function initText() {
  fillSelect($("#tFont"), FONTS.map((f) => [f.id, f.name]));
  fillSelect($("#tIn"), FX_NAMES.map((n, i) => [i, n]));
  fillSelect($("#tOut"), FX_NAMES.map((n, i) => [i, i === 0 ? "None (just goes)" : n]));
  fillSelect($("#tHold"), HOLD_NAMES.map((n, i) => [i, n]));
  fillSelect($("#tColor"), CMODE_NAMES.map((n, i) => [i, n]));
  fillSelect($("#tBg"), [[0, "Black"], [1, "Colour 2"], ...EFFECTS.map((n, i) => [2 + i, "Animation: " + n])]);
  for (const id of ["#tMsg", "#tC1", "#tC2", "#tBgDim", "#tSpeed", "#tStay"]) $(id).addEventListener("input", edited);
  for (const id of ["#tFont", "#tSize", "#tIn", "#tHold", "#tOut", "#tAlign", "#tColor", "#tBg", "#tRepeat", ...DECOR.map((k) => "#t_" + k)]) $(id).addEventListener("change", edited);
  for (const [name, fn] of PRESETS)
    $("#tPresets").append(el("button", { class: "chip", onclick: () => { Object.assign(msgs[cur], fn(msgs[cur])); save(); toEditor(); edited(); } }, name));
  $("#tAdd").addEventListener("click", () => {
    if (msgs.length >= MAX) return;
    msgs.push(Object.assign(newMsg("Message " + (msgs.length + 1)), { cmode: msgs[cur].cmode, font: msgs[cur].font, size: msgs[cur].size }));
    cur = msgs.length - 1;
    save(); renderCards(); toEditor(); preview();
    $("#tMsg").focus();
  });
  $("#btnText").addEventListener("click", () => {
    fromEditor();
    const list = msgs.map(compileMsg);
    if (list.some((c) => !c)) { toastError(`Message ${list.findIndex((c) => !c) + 1} has no letters to show.`); return; }
    if (memory() > POOL) { toastError("The messages don't fit in the panel's memory. Make them shorter or smaller."); return; }
    if (panel.chWrite && panel.level < 17) { toastError("Text effects need newer firmware on the panel. Install it under Device."); return; }
    act(async () => {
      for (let i = 0; i < list.length; i++) {
        const c = list[i], head = [i, list.length, c.kind, ...c.style];
        const body = c.kind === 1 ? [c.scale, ...c.text] : [c.strip.h, c.strip.w & 255, c.strip.w >> 8, ...c.strip.bits];
        const r = await send(CMD.TEXT, [...head, ...body], 8000);
        if (r[4] !== 1) throw new Error(`The panel refused message ${i + 1}.`);
      }
      toast(list.length > 1 ? `Sent ${list.length} messages.` : "Message sent.", { kind: "ok" });
      emit("show-live");
    });
  });
  renderCards(); toEditor(); memory();
  on("view", ({ dest }) => { if (dest === "text") preview(); });
}

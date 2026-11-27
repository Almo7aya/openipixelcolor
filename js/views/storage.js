// Device > Storage. The animation space as its slots of 32 KB (an animation always takes whole slots; 20 in
// the panel's own flash, 127 on the extra memory chip with openipixelcolor 104 and later), each animation
// with a moving preview, playing and deleting; then the memory, area by area (fixed by the firmware:
// firmware/src/drv/iflash.c, app/anim.h, app/settings.c).
import { $, el, on, kb, holdToConfirm } from "../core.js?v=123ecfd";
import { CMD } from "../protocol.js?v=123ecfd";
import { panel, send, act } from "../ble.js?v=123ecfd";
import { libraryState, libraryPreparing, refreshLibrary, deleteAnimation, ready } from "../library.js?v=123ecfd";
import { previewInto } from "../animpreview.js?v=123ecfd";
import { READBACK_LEVEL } from "../readback.js?v=123ecfd";
import { latest } from "../firmware.js?v=123ecfd";

const K = 1024;
/* the panel's own flash, in address order; `used` is filled in where it is known */
const AREAS = [
  { id: "boot",  name: "Bootloader",                 size: 64 * K,  note: "From the factory, never changed" },
  { id: "fw",    name: "Firmware",                   size: 992 * K, note: "Set aside for the running firmware" },
  { id: "ota",   name: "Room for updates",           size: 288 * K, note: "A new firmware waits here while it installs" },
  { id: "anim",  name: "Animations",                 size: 640 * K, note: "What you save from Create" },
  { id: "free",  name: "Not used",                   size: 48 * K,  note: "" },
  { id: "saved", name: "Settings, picture and text", size: 12 * K,  note: "Kept when the panel is switched off" },
  { id: "rec",   name: "Update record",              size: 4 * K,   note: "Tells the bootloader to install an update" },
];
/* with the extra chip, the animations live there (app/anim.h) */
const MOVED = { id: "free", name: "Not used", size: 640 * K, note: "Animations were kept here before the extra chip was used" };
const xareas = (animBytes) => [
  { id: "free", name: "Kept empty", size: 4 * K, note: "Where the original firmware lists its pictures: empty, so it has none" },
  { id: "rec",  name: "Marker",     size: 4 * K, note: "Says the chip holds openipixelcolor's animations" },
  { id: "free", name: "Not used",   size: 24 * K, note: "" },
  { id: "anim", name: "Animations", size: animBytes, note: "What you save from Create" },
];

/* ---- the animation space ---- */
function linkHover(slot, onOff) {
  for (const n of document.querySelectorAll(`#stSlots [data-slot="${slot}"], #stList [data-slot="${slot}"]`)) n.classList.toggle("hl", onOff);
}
const columns = (n) => {
  const narrow = matchMedia("(max-width: 640px)").matches;
  return n > 20 ? (narrow ? 16 : 32) : (narrow ? 10 : 20);
};
function renderAnimations() {
  const st = libraryState(), strip = $("#stSlots"), list = $("#stList");
  const usable = !!(ready() && st);
  $("#stClean").disabled = !usable || !st.items.length;
  $("#stRefresh").disabled = !ready();
  strip.replaceChildren(); list.replaceChildren();
  const nSlots = st ? st.slots : 20, slotBytes = (st ? st.slotKb : 32) * K, total = nSlots * slotBytes, cols = columns(nSlots);
  strip.style.setProperty("--cols", cols);
  strip.classList.toggle("many", nSlots > 20);

  // the strip: one block per animation over the slots it takes (in two pieces where it goes past the end of
  // a row), free slots as empty cells
  const starts = new Map(usable ? st.items.map((it) => [it.slot, it]) : []);
  let usedBytes = 0;
  for (let s = 0; s < nSlots;) {
    const it = starts.get(s);
    if (!it) { strip.append(el("span", { class: "free", title: `Slot ${s + 1}: free` })); s++; continue; }
    const n = Math.max(1, Math.min(nSlots - s, Math.ceil(it.size / slotBytes)));
    usedBytes += n * slotBytes;
    const name = it.name || `Slot ${it.slot + 1}`;
    for (let at = s, left = n, first = true; left > 0; first = false) {
      const k = Math.min(left, cols - (at % cols));
      const run = el("button", { class: first ? "run" : "run cont", style: `grid-column: span ${k}`, "data-slot": it.slot,
        title: `${name}: ${kb(it.size)}, ${n} ${n === 1 ? "slot" : "slots"}. Click to play it.`,
        onclick: () => play(it), onmouseenter: () => linkHover(it.slot, true), onmouseleave: () => linkHover(it.slot, false) });
      if (first) {
        const cv = el("canvas", { width: 32, height: 32 });
        run.append(el("span", { class: "screen" }, cv));
        if (k > (nSlots > 20 ? 3 : 1)) run.append(el("span", { class: "label" }, name));
        previewInto(cv, it.crc, it.thumb);
      }
      strip.append(run);
      at += k; left -= k;
    }
    s += n;
  }

  // the summary
  if (!panel.chWrite) { $("#stFree").textContent = ""; $("#stFreeOf").textContent = "Connect the panel to see what's stored on it."; return; }
  if (!ready()) { $("#stFree").textContent = ""; $("#stFreeOf").textContent = "Storing animations needs newer firmware on the panel. Install it above."; return; }
  if (libraryPreparing()) { $("#stFree").textContent = "…"; $("#stFreeOf").textContent = "Getting the extra memory chip ready (the first start after the update). Up to a minute."; return; }
  if (!st) { $("#stFree").textContent = "…"; $("#stFreeOf").textContent = "Reading what's stored…"; return; }
  $("#stFree").textContent = `${kb(total - usedBytes)} free`;
  $("#stFreeOf").textContent = st.items.length
    ? `of ${kb(total)} for animations. ${st.items.length} stored, using ${kb(usedBytes)}.`
    : `of ${kb(total)} for animations. Nothing stored yet: make one under Create.`;

  // the list
  for (const it of st.items) {
    const n = Math.ceil(it.size / slotBytes), cv = el("canvas", { width: 32, height: 32 });
    const note = el("span", { class: "meta" }, `${it.frames} ${it.frames === 1 ? "frame" : "frames"}, ${kb(it.size)}, takes ${n} ${n === 1 ? "slot" : "slots"} (${kb(n * slotBytes)})`);
    let armed = 0;
    const del = el("button", { class: "btn small ghost" }, "Delete");
    del.addEventListener("click", () => {
      if (!armed) { del.textContent = "Sure?"; del.classList.add("danger"); armed = setTimeout(() => { armed = 0; del.textContent = "Delete"; del.classList.remove("danger"); }, 4000); return; }
      clearTimeout(armed);
      act(() => deleteAnimation(it.slot, it.name));
    });
    const row = el("li", { "data-slot": it.slot, onmouseenter: () => linkHover(it.slot, true), onmouseleave: () => linkHover(it.slot, false) },
      el("span", { class: "screen" }, cv),
      el("span", { class: "grow" }, el("span", { class: "name" }, it.name || `Slot ${it.slot + 1}`), note),
      el("button", { class: "btn small", onclick: () => play(it) }, "Play"), del);
    previewInto(cv, it.crc, it.thumb).then((kind) => {
      if (kind) return;
      cv.parentNode.classList.add("none");
      note.textContent += panel.level >= READBACK_LEVEL ? ". Reading a preview from the panel…" : ". Sent from another device: firmware 101 or later can show a preview";
    });
    list.append(row);
  }
}
function play(it) {
  act(async () => {
    const r = await send(CMD.ANIM_PLAY, [it.slot, Number($("#libSpeed").value)]);
    if (r[4] !== 1) throw new Error("The panel couldn't play it. Refresh and try again.");
  });
}

/* ---- the memory ---- */
function renderBar(bar, legend, areas, known, label, st) {
  bar.replaceChildren(); legend.replaceChildren();
  for (const a of areas) {
    const used = known[a.id] ?? null;
    const part = el("span", { class: `part m-${a.id}${used != null ? " split" : ""}`, style: `flex:${a.size} 1 0px`,
      title: `${a.name}: ${kb(a.size)}${used != null ? `, ${kb(used)} used` : ""}` });
    if (used != null) part.append(el("span", { class: "used", style: `width:${Math.max(used ? 1.5 : 0, (used / a.size) * 100)}%` }));
    part.append(el("span", { class: "amount" }, kb(a.size)));
    bar.append(part);
    let detail = a.note;
    if (a.id === "fw" && used != null) detail = `This version uses ${kb(used)}; the rest is room for bigger firmware`;
    if (a.id === "anim" && used != null) detail = `${kb(used)} used by ${st.items.length} ${st.items.length === 1 ? "animation" : "animations"}`;
    legend.append(el("li", {}, el("span", { class: `key m-${a.id}` }), el("span", { class: "nm" }, a.name),
      el("span", { class: "sz" }, kb(a.size)), el("span", { class: "dt" }, detail)));
  }
  bar.setAttribute("aria-label", `${label}: ` + areas.map((a) => `${a.name} ${kb(a.size)}`).join(", "));
  requestAnimationFrame(() => {                       // an amount only where it fits inside its part
    for (const p of bar.children) { const t = p.querySelector(".amount"); t.hidden = false; t.hidden = t.scrollWidth + 14 > p.clientWidth; }
  });
}
function renderMemory() {
  const st = libraryState(), ext = !!(st && st.ext && ready());
  const animUsed = st && ready() ? st.items.reduce((a, it) => a + Math.ceil(it.size / (st.slotKb * K)) * st.slotKb * K, 0) : null;
  const fw = latest && panel.custom && panel.version === latest.version ? latest.size : null;
  $("#stMemTitle").textContent = ext ? "The panel's own memory: 2 MB" : "The whole memory: 2 MB";
  renderBar($("#stMem"), $("#stMemList"), ext ? AREAS.map((a) => (a.id === "anim" ? MOVED : a)) : AREAS,
    { anim: ext ? null : animUsed, fw }, "The panel's 2 MB of flash", st);
  $("#stXBox").hidden = !ext;
  if (ext) renderBar($("#stXMem"), $("#stXMemList"), xareas(st.slots * st.slotKb * K), { anim: animUsed }, "The extra 4 MB memory chip", st);
}

export function initStorage() {
  holdToConfirm($("#stClean"), () => act(() => deleteAnimation(0xFF)));
  $("#stRefresh").addEventListener("click", refreshLibrary);
  const both = () => { renderAnimations(); renderMemory(); };
  for (const ev of ["library", "connected", "disconnected", "release"]) on(ev, both);
  window.addEventListener("resize", both);
  both();
}

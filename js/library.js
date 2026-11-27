// Animations stored on the panel (cmds 0xF10C-0xF111, docs/PROTOCOL.md): list, names, play, delete, speed,
// and the upload used by Create. Thumbnails and names of animations sent from this browser are remembered
// by checksum, so the list can show them.
import { $, el, on, emit, store, kb } from "./core.js?v=f294bea";
import { CMD, MODE, u32, rd32 } from "./protocol.js?v=f294bea";
import { panel, send, act, refreshSoon } from "./ble.js?v=f294bea";
import { toast, toastError } from "./toast.js?v=f294bea";
import { setAnimNamer } from "./device.js?v=f294bea";
import { putAnim, getAnim } from "./idb.js?v=f294bea";
import { previewInto } from "./animpreview.js?v=f294bea";
import { readPreview, READBACK_LEVEL } from "./readback.js?v=f294bea";
import { thumbOf } from "./create/import.js?v=f294bea";
import OPA from "./create/anim-codec.js?v=f294bea";

const FW_MIN = 18, ALL = 0xFF, KEEP = 0xFE;
const PREPARING = "The panel is getting its animation storage ready (the first start after the update erases its extra memory chip). This takes up to a minute.";
const REASONS = {
  3: "The panel refused the request.", 4: "The panel isn't ready for animations (is it in rescue mode, or still getting its storage ready?).",
  5: "The upload stopped: the panel waited too long for data.", 6: "A piece arrived out of order.",
  7: "A piece had the wrong size.", 8: "The panel refused the file's header.",
  9: "The panel couldn't write its flash memory.", 10: "The copy in the panel's memory didn't check out.",
};
const cache = store.get("animlib", {});            // crc -> { name, thumb, t }
const lib = { items: [], store: null, showing: -1, busy: false, preparing: false };
export const ready = () => panel.chWrite && panel.custom && panel.level >= FW_MIN;

/* the slots, 32 to a page of the reply (openipixelcolor 104 with the extra memory chip has 127; older ones
   20 and no pages). Flags: bit 1 the extra chip, bit 2 still preparing it, bits 4-7 the page. */
async function list() {
  const page = (p) => send(CMD.ANIM_LIST, [p], 4000, (v) => (v[15] >> 4) === p);
  const r = await page(0);
  if (r[4] !== 1) { const e = new Error(r[15] & 4 ? PREPARING : REASONS[4]); e.preparing = !!(r[15] & 4); throw e; }
  const l = { slots: r[5], slotKb: r[6], valid: [], used: [], ext: !!(r[15] & 2) };
  const take = (v, first) => {
    for (let i = 0; i < 32 && first + i < l.slots; i++) {
      l.valid[first + i] = ((rd32(v, 7) >>> i) & 1) === 1;
      l.used[first + i] = ((rd32(v, 11) >>> i) & 1) === 1;
    }
  };
  take(r, 0);
  for (let p = 1; p * 32 < l.slots; p++) take(await page(p), p * 32);
  return l;
}
async function info(slot) {
  const r = await send(CMD.ANIM_INFO, [slot, 0], 4000, (v) => v[5] === slot && v[6] === 0);
  return r[4] !== 1 ? null : { slot, frames: r[7] | (r[8] << 8), size: r[9] | (r[10] << 8) | (r[11] << 16), crc: rd32(r, 12) };
}
async function readName(slot) {
  const bytes = [];
  for (let part = 1; part <= 3; part++) {
    const r = await send(CMD.ANIM_INFO, [slot, part], 4000, (v) => v[5] === slot && v[6] === part);
    if (r[4] !== 1) break;
    bytes.push(...r.slice(7, 16));
  }
  const z = bytes.indexOf(0);
  return new TextDecoder().decode(new Uint8Array(z < 0 ? bytes : bytes.slice(0, z)));
}
export function remember(crc, name, thumb) {
  cache[crc] = { name, thumb, t: Date.now() };
  const keys = Object.keys(cache);
  if (keys.length > 60) keys.sort((a, b) => cache[a].t - cache[b].t).slice(0, keys.length - 60).forEach((k) => delete cache[k]);
  store.set("animlib", cache);
}

export async function refreshLibrary() {
  if (!ready()) { render(); return; }
  if (lib.busy) return;
  lib.busy = true;
  try {
    const l = await list(), items = [];
    lib.preparing = false;
    for (let s = 0; s < l.slots; s++) {
      if (!l.valid[s]) continue;
      const it = await info(s);
      if (!it) continue;
      const c = cache[it.crc];
      it.name = c ? c.name : await readName(s);
      it.thumb = c ? c.thumb : null;
      items.push(it);
    }
    lib.store = l; lib.items = items;
    emit("library", items);
  } catch (e) {
    if (e.preparing) { lib.preparing = true; setTimeout(() => { if (ready()) refreshLibrary(); }, 5000); }   /* look again soon */
    else toastError(e.message);
  }
  finally { lib.busy = false; render(); }
  if (!lib.preparing) fetchMissingPreviews();
}
export const libraryPreparing = () => lib.preparing;

/* animations without a picture here (sent from elsewhere): read one back from the panel, one at a time */
let reading = false;
async function fetchMissingPreviews() {
  if (reading || !panel.custom || panel.level < READBACK_LEVEL) return;
  reading = true;
  try {
    for (const it of [...lib.items]) {
      if (!panel.chWrite || panel.busy) break;
      if (it.thumb || (await getAnim(it.crc))) continue;
      try {
        const r = await readPreview(it);                // ~25 ms a request: 2-5 s each
        it.thumb = thumbOf(r.rgb, r.w, r.h);
        remember(it.crc, it.name, it.thumb);
        render();
        emit("library", lib.items);
      } catch (e) { /* no preview for this one */ }
    }
  } finally { reading = false; }
}
/* for the storage overview: { slots, slotKb, ext, items: [{ slot, frames, size, crc, name, thumb }] } or null */
export const libraryState = () => (lib.store ? { slots: lib.store.slots, slotKb: lib.store.slotKb, ext: lib.store.ext, items: lib.items } : null);

export async function deleteAnimation(slot, name) {
  const r = await send(CMD.ANIM_DELETE, [slot], 8000);
  if (r[4] !== 1) throw new Error("The panel couldn't delete it.");
  toast(slot === ALL ? "Deleted every animation on the panel." : `Deleted "${name || "the animation"}".`, { kind: "ok" });
  await refreshLibrary();
}

function render() {
  const box = $("#libTiles"), empty = $("#libEmpty");
  box.replaceChildren();
  $("#libActions").hidden = !ready() || !lib.items.length;
  if (!panel.chWrite) { empty.textContent = "Connect the panel to see the animations stored on it."; empty.hidden = false; $("#libSpace").textContent = ""; return; }
  if (!ready()) { empty.textContent = "Animations need newer firmware on the panel. Install it under Device."; empty.hidden = false; return; }
  if (lib.preparing) { empty.textContent = PREPARING; empty.hidden = false; $("#libSpace").textContent = ""; return; }
  empty.textContent = "No animations on the panel yet. Make one under Create.";
  empty.hidden = lib.items.length > 0;
  for (const it of lib.items) {
    const del = el("button", { class: "del", title: "Delete from the panel" }, "Delete");
    const cv = el("canvas", { width: 32, height: 32 }), scr = el("span", { class: "screen" }, cv);
    previewInto(cv, it.crc, it.thumb).then((kind) => { if (!kind) scr.append(el("span", { class: "placeholder" }, "made on another device")); });
    const tile = el("button", { class: "tile", "aria-pressed": String(it.slot === lib.showing), title: "Play on the panel" },
      scr,
      el("span", { class: "name" }, it.name || `Slot ${it.slot + 1}`),
      el("span", { class: "meta" }, `${it.frames} ${it.frames === 1 ? "frame" : "frames"}, ${kb(it.size)}`), del);
    tile.dataset.slot = it.slot;
    tile.addEventListener("click", (e) => {
      if (e.target === del) return;
      act(async () => {
        const r = await send(CMD.ANIM_PLAY, [it.slot, Number($("#libSpeed").value)]);
        if (r[4] !== 1) throw new Error("The panel couldn't play it. Refresh and try again.");
      });
    });
    let armed = 0;
    del.addEventListener("click", () => {
      if (!armed) { del.textContent = "Sure?"; del.classList.add("armed"); armed = setTimeout(() => { armed = 0; del.textContent = "Delete"; del.classList.remove("armed"); }, 4000); return; }
      clearTimeout(armed);
      act(() => deleteAnimation(it.slot, it.name));
    });
    box.append(tile);
  }
  const l = lib.store;
  if (l) { const n = l.used.filter(Boolean).length; $("#libSpace").textContent = n ? `${Math.round(n * 100 / l.slots)}% of the space used` : ""; }
}

/* ---- upload (Create) ---- */
async function uploadOnce(bytes, slot, onProgress) {
  const b = await send(CMD.ANIM_BEGIN, [slot, ...u32(bytes.length), 1], 15000);
  if (b[4] !== 1) throw new Error(REASONS[b[5]] || "The panel refused the upload.");
  for (let off = 0; off < bytes.length; off += OPA.PIECE) {
    const piece = bytes.subarray(off, off + OPA.PIECE), payload = new Uint8Array(4 + piece.length);
    payload.set(u32(off)); payload.set(piece, 4);
    const r = await send(CMD.ANIM_DATA, payload, 20000, (v) => v.length >= 10 && rd32(v, 5) === off);
    if (r[4] === 0) { const e = new Error(REASONS[r[9]] || "The panel stopped the upload."); e.reason = r[9]; throw e; }
    onProgress((off + piece.length) / bytes.length);
  }
}
/* stores the file in the first free run of slots and plays it; returns true when stored */
export async function uploadAnimation(bytes, name, thumb, onProgress) {
  const l = await list();
  const need = Math.ceil(bytes.length / (l.slotKb * 1024));
  let slot = -1;
  for (let s = 0; s + need <= l.slots && slot < 0; s++) {
    let free = true;
    for (let k = s; k < s + need; k++) if (l.used[k]) free = false;
    if (free) slot = s;
  }
  if (slot < 0) throw new Error("Not enough free space on the panel for this one. Delete an animation, or use fewer frames or colours.");
  try { await uploadOnce(bytes, slot, onProgress); }
  catch (e) { if (e.reason !== 10) throw e; await uploadOnce(bytes, slot, onProgress); }
  const crc = OPA.info(bytes).crc;
  remember(crc, name, thumb);
  await putAnim(crc, bytes);                          // so the panel preview can play it after a reload
  const d = new Date();                               // flash writes pause the panel's clock a little
  await send(CMD.TIME, [d.getHours(), d.getMinutes(), d.getSeconds(), 0]).catch(() => {});
  refreshSoon();
  await refreshLibrary();
  return true;
}

export function initLibrary() {
  const speed = $("#libSpeed"), out = $("#libSpeedOut");
  const speedText = () => { out.value = `${(Number(speed.value) / 10).toFixed(1).replace(/\.0$/, "")}×`; };
  let timer = 0;
  speed.addEventListener("input", () => {
    speedText();
    clearTimeout(timer);
    timer = setTimeout(() => { if (ready()) act(() => send(CMD.ANIM_PLAY, [KEEP, Number(speed.value)])); }, 200);
  });
  speedText();
  $("#libPlayAll").addEventListener("click", () => act(async () => {
    const r = await send(CMD.ANIM_PLAY, [ALL, Number(speed.value)]);
    if (r[4] !== 1) throw new Error("There are no animations on the panel yet.");
  }));
  $("#libRefresh").addEventListener("click", refreshLibrary);
  setAnimNamer((slot) => { const it = lib.items.find((x) => x.slot === slot); return it ? it.name || `Slot ${slot + 1}` : `Slot ${slot + 1}`; });
  on("connected", (d) => { lib.items = []; lib.store = null; if (d.custom && !d.rescue && d.level >= FW_MIN && !panel.busy) refreshLibrary(); else render(); });
  on("disconnected", () => { lib.items = []; lib.store = null; render(); emit("library", []); });
  on("status", (r) => {
    lib.showing = r[5] === MODE.ANIM ? r[6] : -1;
    $("#libTiles").querySelectorAll(".tile").forEach((t) => t.setAttribute("aria-pressed", String(Number(t.dataset.slot) === lib.showing)));
  });
  render();
}
export const goToLibrary = () => emit("navigate", "play/panel");

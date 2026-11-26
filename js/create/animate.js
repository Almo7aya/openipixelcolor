// The animation maker: frames from files, the frame editor or a generator -> colours reduced and packed
// (anim-codec.js) -> a preview decoded exactly like the panel plays it -> saved on the panel (library.js).
import { $, $$, el, on, emit, store, segmented, fillSelect, kb } from "../core.js?v=7f0cc15";
import { pix, drawMirror, claimMirror, mirrorNote } from "../mirror.js?v=7f0cc15";
import { panel } from "../ble.js?v=7f0cc15";
import { size } from "../size.js?v=7f0cc15";
import { toast, toastError } from "../toast.js?v=7f0cc15";
import { uploadAnimation, ready as libReady, goToLibrary, libraryState } from "../library.js?v=7f0cc15";
import OPA from "./anim-codec.js?v=7f0cc15";
import { readFiles, fitFrames, thumbOf, MAX_SOURCE_FRAMES } from "./import.js?v=7f0cc15";
import { PixelCanvas, PALETTE } from "./pixelcanvas.js?v=7f0cc15";
import { FORMULAS, MOTIONS, formulaFrames, emojiFrames } from "./generators.js?v=7f0cc15";
import { SCENE_LIST, BACKDROPS, sceneFrames, sceneColor } from "./scenes3d.js?v=7f0cc15";
import { ILLUSIONS, illusionFrames, illusionNote } from "./illusions.js?v=7f0cc15";
import { PIC_EFFECTS, pictureFrames } from "./picfx.js?v=7f0cc15";

const A = { src: "file", fileSrc: null, lastFiles: null, enc: null, encFrames: null, nameAuto: true, uploading: false, pic: null };

/* 3D scenes and illusions take a moment to draw: drawn in the background (with progress), kept for as long
   as nothing that changes the picture changes */
const R3 = { key: "", frames: null, drawing: "" };
const slowKind = () => ["3d", "illusion", "picture"].includes($("#gKind").value);
const keyGen = () => {
  const k = $("#gKind").value, t = [$("#gSecs").value, $("#gFps").value, size.w, size.h];
  return JSON.stringify(k === "3d" ? ["3d", $("#g3Scene").value, $("#g3Color").value, $("#g3Bg").value, ...t]
    : k === "picture" ? ["picture", A.pic ? A.pic.id : "", $("#gPicFx").value, ...t]
    : ["illusion", $("#gIllusion").value, ...t]);
};
function drawGen(key) {
  if (R3.drawing === key) return;
  const kind = $("#gKind").value;
  if (kind === "picture" && !A.pic) { facts("Choose a picture or a GIF."); return; }
  R3.drawing = key;
  const what = kind === "3d" ? "3D scene" : kind === "picture" ? "picture" : "illusion";
  const common = { secs: Number($("#gSecs").value), fps: Number($("#gFps").value), w: size.w, h: size.h,
    onFrame: (i, n) => { if (R3.drawing === key) facts(`Drawing the ${what}… ${Math.round(i * 100 / n)}%`); } };
  (kind === "3d" ? sceneFrames({ ...common, scene: $("#g3Scene").value, color: $("#g3Color").value, bg: $("#g3Bg").value, ss: 3 })
    : kind === "picture" ? pictureFrames({ ...common, tex: A.pic, kind: $("#gPicFx").value })
    : illusionFrames({ ...common, kind: $("#gIllusion").value }))
    .then((frames) => { if (R3.drawing !== key) return; Object.assign(R3, { key, frames, drawing: "" }); encodeNow(true); })
    .catch((e) => { if (R3.drawing === key) { R3.drawing = ""; facts(e.message, true); } });
}

/* ---- preview on the panel preview, with the file's own frame times ---- */
const pv = { frames: null, i: 0, timer: 0, on: false };
function previewStop() { pv.on = false; clearTimeout(pv.timer); }
function previewPlay(frames) {
  if (!frames || !frames.length) return;
  previewStop();
  claimMirror("anim", previewStop);
  Object.assign(pv, { frames, i: 0, on: true });
  mirrorNote("Animation preview, exactly as the panel will play it.");
  const step = () => {
    if (!pv.on) return;
    const f = pv.frames[pv.i];
    if (pix.length === f.rgb.length) pix.set(f.rgb);
    drawMirror();
    pv.i = (pv.i + 1) % pv.frames.length;
    pv.timer = setTimeout(step, Math.max(OPA.MIN_DELAY, f.delay));
  };
  step();
}

/* ---- the frame editor (frames the panel's size; kept in this browser, one set per size) ---- */
const ed = { frames: [], delays: [], cur: 0, w: 32, h: 32 };
const edKey = () => (ed.w === 32 && ed.h === 32 ? "anim.editor" : `anim.editor.${ed.w}x${ed.h}`);
let pc = null;
const toB64 = (u8) => { let s = ""; for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode(...u8.subarray(i, i + 4096)); return btoa(s); };
const fromB64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
let saveTimer = 0;
function edChanged(structure) {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => store.set(edKey(), { frames: ed.frames.map(toB64), delays: ed.delays }), 400);
  if (structure) edStrip(); else edThumb(ed.cur);
  pc.setFrame(ed.frames[ed.cur]);
  $("#aDur").value = ed.delays[ed.cur];
  $("#aDurOut").value = `${ed.delays[ed.cur]} ms`;
  if (A.src === "draw") schedule(false);
}
function edThumb(i) {
  const b = $("#aStrip").children[i];
  if (!b) return;
  const ctx = b.querySelector("canvas").getContext("2d"), im = ctx.createImageData(ed.w, ed.h), f = ed.frames[i];
  for (let k = 0; k < ed.w * ed.h; k++) { im.data[k * 4] = f[k * 3]; im.data[k * 4 + 1] = f[k * 3 + 1]; im.data[k * 4 + 2] = f[k * 3 + 2]; im.data[k * 4 + 3] = 255; }
  ctx.putImageData(im, 0, 0);
}
function edStrip() {
  const box = $("#aStrip");
  box.replaceChildren(...ed.frames.map((_, i) => {
    const b = el("button", { role: "option", "aria-selected": String(i === ed.cur), "aria-label": `Frame ${i + 1}` },
      el("canvas", { width: ed.w, height: ed.h, style: `aspect-ratio: ${ed.w} / ${ed.h}; width: auto` }), el("span", {}, String(i + 1)));
    b.addEventListener("click", () => { ed.cur = i; edChanged(true); });
    return b;
  }));
  ed.frames.forEach((_, i) => edThumb(i));
  const lit = box.children[ed.cur];
  if (lit) lit.scrollIntoView({ block: "nearest", inline: "nearest" });
}
const blank = () => new Uint8Array(ed.w * ed.h * 3);
function edLoad() {                                    /* the frames for the panel's size */
  ed.w = size.w; ed.h = size.h; ed.frames = []; ed.cur = 0;
  const s = store.get(edKey(), null);
  if (s && Array.isArray(s.frames) && s.frames.length) {
    ed.frames = s.frames.map(fromB64).filter((f) => f.length === ed.w * ed.h * 3);
    ed.delays = ed.frames.map((_, i) => Math.max(OPA.MIN_DELAY, Number(s.delays && s.delays[i]) || 150));
  }
  if (!ed.frames.length) { ed.frames = [blank()]; ed.delays = [150]; }
  if (pc) pc.resize(ed.w, ed.h);
}
function initEditor() {
  edLoad();
  pc = new PixelCanvas($("#aEd"), {
    w: ed.w, h: ed.h,
    onPaint: () => edChanged(false),
    onPick: (hex) => { $("#aPen").value = hex; pc.colour = hex; tools.set("pen"); pc.tool = "pen"; },
    onion: () => ($("#aOnion").checked && ed.cur > 0 ? ed.frames[ed.cur - 1] : null),
  });
  const tools = segmented($("#aTool"), (t) => { pc.tool = t; });
  $("#aPen").addEventListener("input", (e) => { pc.colour = e.target.value; });
  PALETTE.forEach((c) => $("#aPal").append(el("button", { style: `background:${c}`, "aria-label": `Colour ${c}`, onclick: () => { $("#aPen").value = c; pc.colour = c; pc.tool = "pen"; tools.set("pen"); } })));
  $("#aOnion").addEventListener("change", () => pc.draw());
  $("#aDur").addEventListener("input", (e) => { ed.delays[ed.cur] = Number(e.target.value); edChanged(false); });
  $("#aDurAll").addEventListener("click", () => { ed.delays = ed.delays.map(() => ed.delays[ed.cur]); edChanged(false); toast(`Every frame now stays ${ed.delays[ed.cur]} ms.`); });
  $("#aFrAdd").addEventListener("click", () => { ed.frames.splice(ed.cur + 1, 0, blank()); ed.delays.splice(ed.cur + 1, 0, ed.delays[ed.cur]); ed.cur++; edChanged(true); });
  $("#aFrDup").addEventListener("click", () => { ed.frames.splice(ed.cur + 1, 0, ed.frames[ed.cur].slice()); ed.delays.splice(ed.cur + 1, 0, ed.delays[ed.cur]); ed.cur++; edChanged(true); });
  $("#aFrDel").addEventListener("click", () => {
    if (ed.frames.length === 1) { ed.frames[0].fill(0); edChanged(true); return; }
    ed.frames.splice(ed.cur, 1); ed.delays.splice(ed.cur, 1); ed.cur = Math.min(ed.cur, ed.frames.length - 1); edChanged(true);
  });
  const moveFrame = (d) => {
    const j = ed.cur + d;
    if (j < 0 || j >= ed.frames.length) return;
    [ed.frames[ed.cur], ed.frames[j]] = [ed.frames[j], ed.frames[ed.cur]];
    [ed.delays[ed.cur], ed.delays[j]] = [ed.delays[j], ed.delays[ed.cur]];
    ed.cur = j; edChanged(true);
  };
  $("#aFrLeft").addEventListener("click", () => moveFrame(-1));
  $("#aFrRight").addEventListener("click", () => moveFrame(1));
  $$("[data-shift]").forEach((b) => b.addEventListener("click", () => {     // move the picture, wrapping around
    const [dx, dy] = b.dataset.shift.split(",").map(Number), f = ed.frames[ed.cur], o = f.slice();
    const W = ed.w, H = ed.h;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const s2 = (((y - dy + H) % H) * W + ((x - dx + W) % W)) * 3;
      f.set(o.subarray(s2, s2 + 3), (y * W + x) * 3);
    }
    edChanged(false);
  }));
  $("#aFlip").addEventListener("click", () => {
    const f = ed.frames[ed.cur], o = f.slice();
    const W = ed.w;
    for (let y = 0; y < ed.h; y++) for (let x = 0; x < W; x++) f.set(o.subarray((y * W + W - 1 - x) * 3, (y * W + W - 1 - x) * 3 + 3), (y * W + x) * 3);
    edChanged(false);
  });
  $("#aClr").addEventListener("click", () => { ed.frames[ed.cur].fill(0); edChanged(false); });
}

/* ---- the sources ---- */
function note(msg, err) { const n = $("#aFileNote"); n.textContent = msg; n.classList.toggle("err", !!err); }
async function importFiles(list) {
  note("Reading…");
  try {
    const r = await readFiles(list, Number($("#aFps").value), (p) => note(`Reading the video… ${Math.round(p * 100)}%`));
    A.fileSrc = r; A.lastFiles = r.files; A.nameAuto = true;
    note(`${r.frames.length} ${r.frames.length === 1 ? "frame" : "frames"} from ${r.files.length > 1 ? r.files.length + " pictures" : r.files[0].name}` +
         (r.frames.length >= MAX_SOURCE_FRAMES ? ` (the first ${MAX_SOURCE_FRAMES} only)` : "") + ".");
    $("#aToEditor").disabled = false;
    encodeNow(true);
  } catch (e) { note(e.message || "That file couldn't be read.", true); }
}
const fileFrames = () => (A.fileSrc ? fitFrames(A.fileSrc.frames, { step: Number($("#aStep").value), fit: $("#aFit").value, smooth: $("#aSmooth").value === "1", bg: $("#aBg").value, w: size.w, h: size.h }) : null);
function genFrames() {
  const secs = Number($("#gSecs").value), fps = Number($("#gFps").value), kind = $("#gKind").value;
  if (slowKind()) return R3.key === keyGen() ? R3.frames : null;       /* encodeNow() starts the drawing */
  return kind === "emoji"
    ? emojiFrames({ text: $("#gText").value, motion: $("#gMotion").value, ink: $("#gInk").value, bg: $("#gBg").value, secs, fps, w: size.w, h: size.h })
    : formulaFrames($("#gFormula").value, secs, fps, size.w, size.h);
}
function currentFrames() {
  if (A.src === "file") return fileFrames();
  if (A.src === "draw") return ed.frames.map((rgb, i) => ({ rgb, delay: ed.delays[i] }));
  return genFrames();
}
function defaultName() {
  if (A.src === "file") return A.fileSrc ? A.fileSrc.name : "";
  if (A.src === "draw") return "Drawing";
  const kind = $("#gKind").value;
  return kind === "3d" ? $("#g3Scene").selectedOptions[0].textContent
       : kind === "illusion" ? $("#gIllusion").selectedOptions[0].textContent
       : kind === "picture" ? `${A.pic ? A.pic.name : "Picture"} ${$("#gPicFx").selectedOptions[0].textContent.toLowerCase()}`
       : kind === "emoji" ? `${$("#gText").value.trim()} ${$("#gMotion").selectedOptions[0].textContent.toLowerCase()}`
       : $("#gPreset").selectedOptions[0].textContent;
}

/* ---- encode, facts, save ---- */
let encTimer = 0;
const schedule = (play) => { clearTimeout(encTimer); encTimer = setTimeout(() => encodeNow(play), 220); };
function facts(msg, err) { const f = $("#aFacts"); f.textContent = msg; f.classList.toggle("err", !!err); }
function buttons() { $("#aSave").disabled = !A.enc || !A.enc.fits || A.uploading; $("#aPreview").disabled = !A.encFrames; }
/* what the panel can hold: 4 MB with the extra memory chip (openipixelcolor 104), else 640 KB */
const capacity = () => { const st = libraryState(); return st ? st.slots * st.slotKb * 1024 : OPA.MAX_BYTES; };
function encodeNow(play) {
  if (A.nameAuto) $("#aName").value = defaultName().slice(0, 40);
  if (A.src === "gen" && slowKind() && R3.key !== keyGen()) {         /* not drawn yet: draw, then come back */
    A.enc = A.encFrames = null; buttons();
    drawGen(keyGen());
    return;
  }
  let frames;
  try { frames = currentFrames(); } catch (e) { A.enc = A.encFrames = null; facts(e.message, true); buttons(); return; }
  if (!frames || !frames.length) { A.enc = A.encFrames = null; facts("Nothing made yet."); buttons(); return; }
  const tempo = Number($("#aTempo").value);
  try {
    A.enc = OPA.encode(frames.map((f) => ({ rgb: f.rgb, delay: f.delay / tempo })), { name: $("#aName").value, maxColors: Number($("#aColors").value), w: size.w, h: size.h, maxBytes: capacity() });
    A.encFrames = OPA.decode(A.enc.bytes);
  } catch (e) { A.enc = A.encFrames = null; facts(e.message, true); buttons(); return; }
  const secs = A.encFrames.reduce((a, f) => a + f.delay, 0) / 1000;
  facts(A.enc.fits ? `${A.enc.frames} ${A.enc.frames === 1 ? "frame" : "frames"}, ${secs.toFixed(1)} s, ${kb(A.enc.size)}`
                   : "Too big for the panel. Use fewer frames (Frames: every 2nd) or fewer colours.", !A.enc.fits);
  buttons();
  if (play) previewPlay(A.encFrames);
}
async function save() {
  if (!panel.chWrite) { toastError("Connect the panel first."); return; }
  if (!libReady()) { toastError("Animations need newer firmware on the panel. Install it under Device."); return; }
  if (!A.enc || !A.enc.fits || A.uploading) return;
  const enc = A.enc, name = $("#aName").value.trim() || defaultName(), prog = $("#aProg");
  A.uploading = true; buttons(); prog.hidden = false; prog.value = 0;
  const t0 = performance.now();
  try {
    await uploadAnimation(enc.bytes, name, thumbOf(A.encFrames[0].rgb, A.encFrames.w, A.encFrames.h), (p) => { prog.value = p; facts(`Saving on the panel… ${Math.round(p * 100)}%`); });
    facts(`Saved in ${((performance.now() - t0) / 1000).toFixed(1)} s. It plays on the panel now.`);
    toast(`"${name}" is saved on the panel and playing.`, { kind: "ok", action: { label: "See all on the panel", fn: goToLibrary } });
    emit("show-live");
  } catch (e) { facts(e.message, true); toastError(e.message); }
  finally { A.uploading = false; buttons(); prog.hidden = true; }
}

function setSource(src) {
  A.src = src;
  $("#aSrcFile").hidden = src !== "file"; $("#aSrcDraw").hidden = src !== "draw"; $("#aSrcGen").hidden = src !== "gen";
  if (src === "draw") edChanged(true);
  A.nameAuto = true;
  schedule(src !== "draw");
}

export function initAnimate() {
  initEditor();
  const srcSeg = segmented($("#aSrcSeg"), setSource);
  // files
  const drop = $("#aDrop");
  $("#aFile").addEventListener("change", (e) => { if (e.target.files.length) importFiles(e.target.files); e.target.value = ""; });
  drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
  drop.addEventListener("dragleave", () => drop.classList.remove("over"));
  drop.addEventListener("drop", (e) => { e.preventDefault(); drop.classList.remove("over"); importFiles(e.dataTransfer.files); });
  for (const id of ["#aFit", "#aSmooth", "#aStep", "#aBg"]) $(id).addEventListener("change", () => schedule(true));
  $("#aFps").addEventListener("change", () => { if (A.lastFiles && (A.lastFiles.length > 1 || A.lastFiles[0].type.startsWith("video/"))) importFiles(A.lastFiles); });
  $("#aToEditor").addEventListener("click", () => {
    const fr = fileFrames();
    if (!fr || !fr.length) return;
    const n = Math.min(fr.length, 300);
    ed.frames = fr.slice(0, n).map((f) => f.rgb);
    ed.delays = fr.slice(0, n).map((f) => Math.max(OPA.MIN_DELAY, Math.round(f.delay)));
    ed.cur = 0;
    srcSeg.set("draw"); setSource("draw");
    toast(n < fr.length ? `The first ${n} frames are in the editor.` : "The frames are in the editor.");
  });
  // generators
  fillSelect($("#gPreset"), FORMULAS.map(([n], i) => [i, n]));
  fillSelect($("#gMotion"), MOTIONS);
  $("#gFormula").value = FORMULAS[0][1];
  fillSelect($("#g3Scene"), SCENE_LIST);
  fillSelect($("#g3Bg"), BACKDROPS);
  $("#g3Color").value = sceneColor($("#g3Scene").value);
  $("#g3Scene").addEventListener("change", (e) => { $("#g3Color").value = sceneColor(e.target.value); A.nameAuto = true; schedule(true); });
  for (const id of ["#g3Color", "#g3Bg"]) $(id).addEventListener("change", () => schedule(true));
  fillSelect($("#gPicFx"), PIC_EFFECTS);
  $("#gPicFx").addEventListener("change", () => { A.nameAuto = true; schedule(true); });
  $("#gPic").addEventListener("change", async (e) => {      /* the picture as a texture: 3 x the panel's size */
    const f = e.target.files[0];
    if (!f) return;
    try {
      const src = await readFiles([f]), w = size.w * 3, h = size.h * 3;
      A.pic = { frames: fitFrames(src.frames, { fit: "cover", smooth: true, w, h }), w, h, name: src.name, id: `${f.name}:${f.size}:${f.lastModified}` };
      A.nameAuto = true; schedule(true);
    } catch (err) { facts(err.message || "That picture couldn't be read.", true); }
  });
  fillSelect($("#gIllusion"), ILLUSIONS.map(([k, n]) => [k, n]));
  $("#gIllusionNote").textContent = illusionNote($("#gIllusion").value);
  $("#gIllusion").addEventListener("change", (e) => { $("#gIllusionNote").textContent = illusionNote(e.target.value); A.nameAuto = true; schedule(true); });
  const genKind = () => { const k = $("#gKind").value; $$("[data-gen]").forEach((e) => { e.hidden = e.dataset.gen !== k; }); };
  $("#gKind").addEventListener("change", () => { genKind(); A.nameAuto = true; schedule(true); });
  $("#gPreset").addEventListener("change", (e) => { $("#gFormula").value = FORMULAS[Number(e.target.value)][1]; A.nameAuto = true; schedule(true); });
  for (const id of ["#gText", "#gMotion", "#gBg", "#gInk", "#gSecs", "#gFps"]) $(id).addEventListener(id === "#gText" ? "input" : "change", () => { A.nameAuto = true; schedule(true); });
  $("#gFormula").addEventListener("input", () => schedule(true));
  genKind();
  // result
  $("#aColors").addEventListener("change", () => schedule(true));
  $("#aTempo").addEventListener("change", () => schedule(true));
  $("#aName").addEventListener("input", () => { A.nameAuto = !$("#aName").value; schedule(false); });
  $("#aPreview").addEventListener("click", () => previewPlay(A.encFrames));
  $("#aSave").addEventListener("click", save);
  setSource("file");
  on("view", ({ dest, sub }) => { if (dest === "create" && sub === "animation" && A.encFrames) previewPlay(A.encFrames); });
  on("size", ({ changed }) => {                       /* another panel size: its own editor frames, everything made again */
    if (!changed) return;
    edLoad();
    if (A.src === "draw") edChanged(true); else pc.setFrame(ed.frames[0]);
    schedule(false);
  });
}

// Play, one page of sections: effects and clock faces as animated preview tiles (drawn by the firmware's
// own code in the simulator, tools/sim/make_previews.py) with shuffle and clock options, the animations
// stored on the panel (library.js), and the tools.
import { $, $$, el, on, store, reducedMotion } from "./core.js?v=5e9bd5e";
import { CMD, MODE } from "./protocol.js?v=5e9bd5e";
import { panel, send, act } from "./ble.js?v=5e9bd5e";
import { EFFECTS, CLOCKS, TESTS, previewOf } from "./catalog.js?v=5e9bd5e";
import { toastError } from "./toast.js?v=5e9bd5e";
import { go } from "./nav.js?v=5e9bd5e";

const pic = (name) => name + (reducedMotion() ? ".png" : ".gif");

/* what came with which version (an older panel would show another one, so it gets a notice instead):
   effects from 20 and clock faces from 10 with openipixelcolor 103, effects from 47 with 104 */
const SINCE = { effect: [[47, 104], [20, 103]], clock: [[10, 103]] };
function tooOld(kind, i) {
  const need = (SINCE[kind].find(([first]) => i >= first) || [0, 0])[1];
  if (!panel.custom || !need || panel.level >= need) return false;
  toastError(`This one needs openipixelcolor ${need} or newer on the panel.`, { label: "Update", fn: () => go("device") });
  return true;
}
function tile(name, img, onclick) {
  const b = el("button", { class: "tile", "aria-pressed": "false" },
    el("span", { class: "screen" }, el("img", { src: pic(img), alt: "", loading: "lazy", width: 32, height: 32 })),
    el("span", { class: "name" }, name));
  b.addEventListener("click", onclick);
  return b;
}

function initEffects() {
  const grid = $("#effectTiles");
  /* first: the screen a new panel greets you with (not an effect in the firmware: a mode of its own) */
  const welcome = tile("Welcome", previewOf("welcome", 0), () => act(() => send(CMD.MODE, [MODE.WELCOME, 0])));
  welcome.dataset.mode = MODE.WELCOME;
  grid.append(welcome);
  EFFECTS.forEach((name, i) => {
    const t = tile(name, previewOf("effect", i), () => tooOld("effect", i) || act(async () => {
      await send(CMD.MODE, [MODE.EFFECT, i]);
      $("#shuffle").checked = false;
    }));
    t.dataset.mode = MODE.EFFECT; t.dataset.arg = i;
    grid.append(t);
  });
  const sendShuffle = () => {
    const secs = Number($("#shuffleSecs").value);
    act(() => send(CMD.AUTOPLAY, [$("#shuffle").checked ? 1 : 0, secs & 255, secs >> 8]));
  };
  $("#shuffle").addEventListener("change", sendShuffle);
  $("#shuffleSecs").addEventListener("change", () => { if ($("#shuffle").checked) sendShuffle(); });
}

/* Clock faces: each preview shows the face the way the 24-hour and Date switches would. previews/clocks.json
   (tools/sim/make_previews.py) names the file for each face and combination: [24 h + date, 12 h + date,
   24 h, 12 h]; a face that ignores a switch uses the same file for both. */
let clockFiles = null;
function showClockPreviews() {
  const v = ($("#h24").checked ? 0 : 1) + ($("#showDate").checked ? 0 : 2);
  $$("#clockTiles .tile img").forEach((img, i) => {
    const src = pic(clockFiles && clockFiles[i] ? `previews/${clockFiles[i][v]}` : previewOf("clock", i));
    if (img.getAttribute("src") !== src) img.src = src;
  });
}
function initClocks() {
  const grid = $("#clockTiles");
  $("#h24").checked = store.get("clock.h24", true);
  $("#showDate").checked = store.get("clock.date", true);
  CLOCKS.forEach((name, i) => grid.append(tile(name, previewOf("clock", i), () => tooOld("clock", i) || act(async () => {
    const d = new Date();
    await send(CMD.TIME, [d.getHours(), d.getMinutes(), d.getSeconds(), 0]);
    const dow = d.getDay() === 0 ? 7 : d.getDay();
    await send(CMD.CLOCK, [i, $("#h24").checked ? 1 : 0, $("#showDate").checked ? 1 : 0, d.getFullYear() % 100, d.getMonth() + 1, d.getDate(), dow]);
  }))));
  $("#h24").addEventListener("change", (e) => { store.set("clock.h24", e.target.checked); showClockPreviews(); });
  $("#showDate").addEventListener("change", (e) => { store.set("clock.date", e.target.checked); showClockPreviews(); });
  fetch("previews/clocks.json").then((r) => (r.ok ? r.json() : null)).then((j) => {
    if (Array.isArray(j) && j.length === CLOCKS.length) { clockFiles = j; showClockPreviews(); }
  }).catch(() => { /* the 24-hour, with-date previews stay */ });
}

function initTools() {
  // equalizer
  let style = 0, timer = 0;
  const levels = [3, 6, 9, 12, 15, 13, 10, 8, 6, 4, 2];
  const sendEq = () => act(() => send(CMD.RHYTHM, [style, ...levels]));
  levels.forEach((v, i) => {
    const s = el("input", { type: "range", min: 0, max: 15, value: v, "aria-label": `Bar ${i + 1}` });
    s.addEventListener("input", () => { levels[i] = Number(s.value); clearTimeout(timer); timer = setTimeout(sendEq, 70); });
    $("#eq").append(s);
  });
  $$("#eqStyle button").forEach((b) => b.addEventListener("click", () => {
    style = Number(b.dataset.value);
    $$("#eqStyle button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
    sendEq();
  }));
  $("#btnEqDance").addEventListener("click", () => act(() => send(CMD.RHYTHM_ANIM, [3, style])));
  // scoreboard
  const score = [0, 0];
  const sendScore = () => { $("#s1").textContent = score[0]; $("#s2").textContent = score[1]; act(() => send(CMD.SCORE, [score[0], 0, score[1]])); };
  $$("[data-score]").forEach((b) => b.addEventListener("click", () => {
    const [p, d] = b.dataset.score.split(":").map(Number);
    score[p - 1] = Math.max(0, Math.min(99, score[p - 1] + d));
    sendScore();
  }));
  $("#btnScoreReset").addEventListener("click", () => { score[0] = score[1] = 0; sendScore(); });
  // stopwatch
  $$("[data-timer]").forEach((b) => b.addEventListener("click", () => act(() => send(CMD.TIMER, [Number(b.dataset.timer)]))));
  // test patterns
  TESTS.forEach((n, i) => $("#testRow").append(el("button", { class: "btn small", onclick: () => act(() => send(CMD.MODE, [MODE.TEST, i])) }, n)));
}

export function initPlay() {
  initEffects();
  initClocks();
  initTools();
  $("#nEffects").textContent = EFFECTS.length + 1;       /* how many of each, beside the section names (+ Welcome) */
  $("#nClocks").textContent = CLOCKS.length;
  on("library", (items) => { $("#nAnims").textContent = items.length || ""; });
  on("status", (r) => {
    const mode = r[5], flags = r[12];
    $$("#effectTiles .tile").forEach((t) => t.setAttribute("aria-pressed",
      String(Number(t.dataset.mode) === mode && (mode !== MODE.EFFECT || Number(t.dataset.arg) === r[6]))));
    $$("#clockTiles .tile").forEach((t, i) => t.setAttribute("aria-pressed", String(mode === MODE.CLOCK && i === r[14])));
    if (document.activeElement !== $("#shuffle")) $("#shuffle").checked = !!(flags & 2);
    if (r.length >= 16 && document.activeElement !== $("#shuffleSecs")) {
      const v = String(r[15]), sel = $("#shuffleSecs");
      if (![...sel.options].some((o) => o.value === v)) sel.append(el("option", { value: v }, `every ${v} s`));
      sel.value = v;
    }
  });
}

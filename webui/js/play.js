// Play, one page of sections: effects and clock faces as animated preview tiles (drawn by the firmware's
// own code in the simulator, tools/sim/make_previews.py) with shuffle and clock options, the animations
// stored on the panel (library.js), and the tools.
import { $, $$, el, on, store, reducedMotion } from "./core.js";
import { CMD, MODE } from "./protocol.js";
import { send, act } from "./ble.js";
import { EFFECTS, CLOCKS, TESTS, previewOf } from "./catalog.js";

function tile(name, img, onclick) {
  const still = reducedMotion();
  const b = el("button", { class: "tile", "aria-pressed": "false" },
    el("span", { class: "screen" }, el("img", { src: img + (still ? ".png" : ".gif"), alt: "", loading: "lazy", width: 32, height: 32 })),
    el("span", { class: "name" }, name));
  b.addEventListener("click", onclick);
  return b;
}

function initEffects() {
  const grid = $("#effectTiles");
  EFFECTS.forEach((name, i) => grid.append(tile(name, previewOf("effect", i), () => act(async () => {
    await send(CMD.MODE, [MODE.EFFECT, i]);
    $("#shuffle").checked = false;
  }))));
  const sendShuffle = () => {
    const secs = Number($("#shuffleSecs").value);
    act(() => send(CMD.AUTOPLAY, [$("#shuffle").checked ? 1 : 0, secs & 255, secs >> 8]));
  };
  $("#shuffle").addEventListener("change", sendShuffle);
  $("#shuffleSecs").addEventListener("change", () => { if ($("#shuffle").checked) sendShuffle(); });
}

function initClocks() {
  const grid = $("#clockTiles");
  $("#h24").checked = store.get("clock.h24", true);
  $("#showDate").checked = store.get("clock.date", true);
  CLOCKS.forEach((name, i) => grid.append(tile(name, previewOf("clock", i), () => act(async () => {
    const d = new Date();
    await send(CMD.TIME, [d.getHours(), d.getMinutes(), d.getSeconds(), 0]);
    const dow = d.getDay() === 0 ? 7 : d.getDay();
    await send(CMD.CLOCK, [i, $("#h24").checked ? 1 : 0, $("#showDate").checked ? 1 : 0, d.getFullYear() % 100, d.getMonth() + 1, d.getDate(), dow]);
  }))));
  $("#h24").addEventListener("change", (e) => store.set("clock.h24", e.target.checked));
  $("#showDate").addEventListener("change", (e) => store.set("clock.date", e.target.checked));
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
  $("#nEffects").textContent = EFFECTS.length;           /* how many of each, beside the section names */
  $("#nClocks").textContent = CLOCKS.length;
  on("library", (items) => { $("#nAnims").textContent = items.length || ""; });
  on("status", (r) => {
    const mode = r[5], flags = r[12];
    $$("#effectTiles .tile").forEach((t, i) => t.setAttribute("aria-pressed", String(mode === MODE.EFFECT && i === r[6])));
    $$("#clockTiles .tile").forEach((t, i) => t.setAttribute("aria-pressed", String(mode === MODE.CLOCK && i === r[14])));
    if (document.activeElement !== $("#shuffle")) $("#shuffle").checked = !!(flags & 2);
    if (r.length >= 16 && document.activeElement !== $("#shuffleSecs")) {
      const v = String(r[15]), sel = $("#shuffleSecs");
      if (![...sel.options].some((o) => o.value === v)) sel.append(el("option", { value: v }, `every ${v} s`));
      sel.value = v;
    }
  });
}

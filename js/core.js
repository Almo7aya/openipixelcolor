// Small shared helpers: element lookup, events between modules, saved preferences.

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

/* el("button", { class: "btn", onclick }, "Text", child) */
export function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k.startsWith("on") && typeof v === "function") e.addEventListener(k.slice(2), v);
    else if (k === "class") e.className = v;
    else if (k === "html") e.innerHTML = v;
    else e.setAttribute(k, v === true ? "" : v);
  }
  for (const kid of kids.flat()) if (kid != null && kid !== false) e.append(kid instanceof Node ? kid : String(kid));
  return e;
}

export const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
export const hexRgb = (h) => [1, 3, 5].map((i) => parseInt(h.substr(i, 2), 16));
export const rgbHex = (c) => "#" + c.map((v) => v.toString(16).padStart(2, "0")).join("");
export const kb = (n) => (n < 1024 ? `${n} bytes` : `${(n / 1024).toFixed(n < 10240 && n % 1024 ? 1 : 0)} KB`);
export const reducedMotion = () => matchMedia("(prefers-reduced-motion: reduce)").matches;

/* events between modules: connected, disconnected, status, install, install-progress, release, navigate */
const bus = new EventTarget();
export const emit = (name, detail) => bus.dispatchEvent(new CustomEvent(name, { detail }));
export const on = (name, fn) => bus.addEventListener(name, (e) => fn(e.detail));

/* preferences kept in this browser (private windows may refuse: then nothing is remembered) */
export const store = {
  get(key, fallback) { try { const v = JSON.parse(localStorage.getItem("oipc." + key)); return v == null ? fallback : v; } catch (e) { return fallback; } },
  set(key, value) { try { localStorage.setItem("oipc." + key, JSON.stringify(value)); } catch (e) { /* not kept */ } },
  del(key) { try { localStorage.removeItem("oipc." + key); } catch (e) { /* not kept */ } },
};

/* a select filled with [value, label] pairs */
export function fillSelect(sel, items) {
  sel.replaceChildren(...items.map(([v, label]) => el("option", { value: v }, label)));
}

/* a button that acts only when held down for a moment (destructive actions): .hold with a .fill inside */
export function holdToConfirm(btn, done, ms = 1600) {
  const fill = btn.querySelector(".fill");
  let t0 = 0, raf = 0;
  const stop = () => { cancelAnimationFrame(raf); t0 = 0; fill.style.transform = "scaleX(0)"; };
  const tick = (now) => {
    const p = Math.min(1, (now - t0) / ms);
    fill.style.transform = `scaleX(${p})`;
    if (p < 1) raf = requestAnimationFrame(tick); else { stop(); done(); }
  };
  const start = () => { if (btn.disabled || t0) return; t0 = performance.now(); raf = requestAnimationFrame(tick); };
  btn.addEventListener("pointerdown", (e) => { btn.setPointerCapture(e.pointerId); start(); });
  for (const t of ["pointerup", "pointercancel", "pointerleave", "blur"]) btn.addEventListener(t, stop);
  btn.addEventListener("keydown", (e) => { if ((e.key === " " || e.key === "Enter") && !e.repeat) { e.preventDefault(); start(); } });
  btn.addEventListener("keyup", (e) => { if (e.key === " " || e.key === "Enter") stop(); });
}

/* a segmented control or a chip row: one pressed button, onchange(value) */
export function segmented(root, onchange) {
  const buttons = $$("button", root);
  const set = (v) => buttons.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.value === String(v))));
  buttons.forEach((b) => b.addEventListener("click", () => { set(b.dataset.value); onchange(b.dataset.value); }));
  return { set, get: () => (buttons.find((b) => b.getAttribute("aria-pressed") === "true") || {}).dataset?.value };
}

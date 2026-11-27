// Notices in one live region. Errors can carry an action (Connect, Retry...), shown on the same line. Every
// notice goes away by itself: confirmations after 3.5 s, errors after 10 s (they're also in the activity log),
// never while the pointer is on it or it has the focus. timeout 0 keeps one until it's closed.
// toast() returns { update(text), close(), node }.
import { $, el } from "./core.js?v=8f6fef3";
import { log } from "./log.js?v=8f6fef3";

export function toast(text, { kind = "info", title = "", action = null, timeout } = {}) {
  const box = $("#toasts");
  const msg = el("div", { class: "toast-msg" }, title ? el("strong", {}, title) : null, el("span", {}, text));
  let timer = 0;
  const close = () => { clearTimeout(timer); node.remove(); };
  const act = action ? el("button", { class: "btn small", onclick: () => { close(); action.fn(); } }, action.label) : null;
  const node = el("div", { class: `toast ${kind}`, role: kind === "err" ? "alert" : "status" },
    el("div", { class: "toast-head" }, el("span", { class: "dot", "aria-hidden": "true" }), msg, act,
      el("button", { class: "x", "aria-label": "Close" }, "×")));
  node.querySelector(".x").addEventListener("click", close);
  box.append(node);
  const ms = timeout ?? (kind === "err" ? 10000 : 3500);
  if (ms) {
    const start = () => { clearTimeout(timer); timer = setTimeout(close, ms); };
    const hold = () => clearTimeout(timer);
    const free = () => { if (!node.matches(":hover") && !node.contains(document.activeElement)) start(); };
    node.addEventListener("pointerenter", hold);
    node.addEventListener("pointerleave", free);
    node.addEventListener("focusin", hold);
    node.addEventListener("focusout", () => setTimeout(free));
    start();
  }
  return { update: (t) => { msg.lastChild.textContent = t; }, close, node };
}
export const toastError = (text, action) => { log(text, "err"); return toast(text, { kind: "err", action }); };

// Notices in one live region. Errors stay until closed and can carry an action (Connect, Retry...), shown on
// the same line; confirmations go away by themselves. toast() returns { update(text), close(), node }.
import { $, el } from "./core.js?v=c02e0ab";
import { log } from "./log.js?v=c02e0ab";

export function toast(text, { kind = "info", title = "", action = null, timeout } = {}) {
  const box = $("#toasts");
  const msg = el("div", { class: "toast-msg" }, title ? el("strong", {}, title) : null, el("span", {}, text));
  const close = () => { clearTimeout(timer); node.remove(); };
  const act = action ? el("button", { class: "btn small", onclick: () => { close(); action.fn(); } }, action.label) : null;
  const node = el("div", { class: `toast ${kind}`, role: kind === "err" ? "alert" : "status" },
    el("div", { class: "toast-head" }, el("span", { class: "dot", "aria-hidden": "true" }), msg, act,
      el("button", { class: "x", "aria-label": "Close", onclick: close }, "×")));
  box.append(node);
  const ms = timeout ?? (kind === "err" ? 0 : 3500);
  const timer = ms ? setTimeout(close, ms) : 0;
  return { update: (t) => { msg.lastChild.textContent = t; }, close, node };
}
export const toastError = (text, action) => { log(text, "err"); return toast(text, { kind: "err", action }); };

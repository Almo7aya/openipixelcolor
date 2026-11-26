// Notices in one live region. Errors stay until closed and can carry an action (Connect, Retry...);
// confirmations go away by themselves. toast() returns { update(text), close() }.
import { $, el } from "./core.js";

export function toast(text, { kind = "info", title = "", action = null, timeout } = {}) {
  const box = $("#toasts");
  const msg = el("div", { class: "msg" }, title ? el("strong", {}, title) : null, el("span", {}, text));
  const close = () => { clearTimeout(timer); node.remove(); };
  const node = el("div", { class: `toast ${kind}`, role: kind === "err" ? "alert" : "status" },
    el("div", { class: "head" }, el("span", { class: "dot", "aria-hidden": "true" }), msg,
      el("button", { class: "x", "aria-label": "Close", onclick: close }, "×")));
  if (action) node.append(el("div", { class: "row" }, el("button", { class: "btn small", onclick: () => { close(); action.fn(); } }, action.label)));
  box.append(node);
  const ms = timeout ?? (kind === "err" ? 0 : 3500);
  const timer = ms ? setTimeout(close, ms) : 0;
  return { update: (t) => { msg.lastChild.textContent = t; }, close, node };
}
export const toastError = (text, action) => toast(text, { kind: "err", action });

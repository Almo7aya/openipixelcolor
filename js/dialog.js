// A dialog over the page (dimmed, and kept from scrolling) for what needs the person's attention first: a panel
// openipixelcolor doesn't run on, and a firmware update. Laid out like an alert: an icon, a short title, a line
// or two of text, facts, then at most two buttons with the main one on the right (on top on a phone), and room
// for one minor action under them. One at a time; × or Escape closes it, and the focus goes back where it was.
//
// dialog({ tone, icon, title, text, body, actions, after, focus, wide, onClose }) -> { close(), node }
//   tone: "info" or "warn"; icon: a key of ICONS; text: one or more paragraphs; body: nodes (dlgFacts,
//   dlgSection, links); actions: dlgButton()s, the main one last; after: a minor action under them;
//   focus: the button to focus; wide: room for more (the debug menu).
import { el } from "./core.js?v=62301d0";

const ICONS = {
  update: '<path d="M12 4v11m0 0-4.5-4.5M12 15l4.5-4.5M5 19h14"/>',
  warn: '<path d="M12 3.8 2.6 20h18.8L12 3.8Z"/><path d="M12 10v4.2M12 17.2v.01"/>',
  debug: '<path d="m8.5 8-4 4 4 4M15.5 8l4 4-4 4M13.2 5.5l-2.4 13"/>',
};
let current = null;

export function dialog({ tone = "info", icon = null, title, text = [], body = [], actions = [], after = null,
  focus = null, wide = false, onClose = null }) {
  if (current) current.close();
  const x = el("button", { class: "dlg-x", "aria-label": "Close" }, "×");
  const node = el("dialog", { class: `dlg ${tone}${wide ? " wide" : ""}`, "aria-labelledby": "dlgTitle", "aria-describedby": "dlgText" },
    x,
    icon ? el("div", { class: "dlg-icon", "aria-hidden": "true", html: `<svg viewBox="0 0 24 24">${ICONS[icon]}</svg>` }) : null,
    el("h2", { id: "dlgTitle" }, title),
    el("div", { id: "dlgText", class: "dlg-text" }, [].concat(text).map((t) => el("p", {}, t))),
    body,
    el("div", { class: "dlg-actions" }, actions),
    after ? el("div", { class: "dlg-after" }, after) : null);
  const api = { node, close: () => { if (node.open) node.close(); } };
  x.addEventListener("click", api.close);

  /* the page behind stays put: the CSS hides its overflow, and the wheel over the dimmed part does nothing */
  const wheel = (e) => {
    const r = node.getBoundingClientRect();
    if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) e.preventDefault();
  };
  window.addEventListener("wheel", wheel, { passive: false });
  node.addEventListener("close", () => {
    window.removeEventListener("wheel", wheel);
    node.remove();
    if (current === api) current = null;
    if (onClose) onClose();
  });

  document.body.append(node);
  node.showModal();
  (focus || actions[actions.length - 1] || x).focus();
  current = api;
  return api;
}

/* a dialog button: { primary } for the main action, { quiet } for a minor one, { href } for a link that opens
   in a new tab, { icon } for an icon from the page's sprite (16 x 16) before the words */
export function dlgButton(label, fn, { primary = false, quiet = false, href = null, icon = null } = {}) {
  const cls = `btn${primary ? " primary" : ""}${quiet ? " ghost small" : ""}`;
  const html = icon ? `<svg viewBox="0 0 16 16" aria-hidden="true"><use href="#${icon}"/></svg>` : null;
  return href ? el("a", { class: cls, href, target: "_blank", rel: "noopener", onclick: fn, html }, label)
    : el("button", { class: cls, onclick: fn, html }, label);
}

/* a titled group of things in a dialog (the debug menu) */
export const dlgSection = (title, ...kids) => el("section", { class: "dlg-section" }, el("h3", {}, title), ...kids);

/* label / value rows; a third item is a class for the value ("warn") */
export function dlgFacts(rows) {
  return el("dl", { class: "dlg-facts" },
    rows.map(([dt, dd, cls]) => el("div", {}, el("dt", {}, dt), el("dd", { class: cls || null }, dd))));
}

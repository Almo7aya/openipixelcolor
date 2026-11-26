// Destinations (Play, Text, Create, Device) and their sub-views, kept in the address: #play/clocks.
// Older addresses (#effects, #clock, #animations, #pictures, #extras, #settings) still lead somewhere.
import { $, $$, on, store, emit, reducedMotion } from "./core.js?v=b9006af";

const OLD = { effects: "play", clock: "play/clocks", animations: "create/animation", pictures: "create/picture",
              extras: "play/tools", settings: "device" };
const DESTS = ["play", "text", "create", "device"];
const subviews = {};                 // dest -> { set(sub), def }

/* a sub-view switcher (chips or a segmented control) inside a destination */
export function registerSub(dest, root, def, onshow) {
  const buttons = $$("[data-sub]", root);
  const set = (sub) => {
    if (!buttons.some((b) => b.dataset.sub === sub)) sub = def;
    buttons.forEach((b) => {
      const on = b.dataset.sub === sub;
      b.setAttribute(b.getAttribute("role") === "tab" ? "aria-selected" : "aria-pressed", String(on));
      const panel = $("#" + b.dataset.panel);
      if (panel) panel.hidden = !on;
    });
    store.set("sub." + dest, sub);
    if (onshow) onshow(sub);
    return sub;
  };
  buttons.forEach((b) => b.addEventListener("click", () => go(`${dest}/${b.dataset.sub}`)));
  subviews[dest] = { set, def };
}

export function go(target, replace) {
  let [dest, sub] = (OLD[target] || target || "").split("/");
  if (!DESTS.includes(dest)) dest = store.get("dest", "play");
  for (const d of DESTS) {
    $("#view-" + d).hidden = d !== dest;
    const b = $(`.navbtn[data-dest="${d}"]`);
    if (d === dest) b.setAttribute("aria-current", "page"); else b.removeAttribute("aria-current");
  }
  store.set("dest", dest);
  const sv = subviews[dest];
  if (sv) sub = sv.set(sub || store.get("sub." + dest, sv.def));
  else if (sub) {                                      /* a page of sections (Play): scroll to that one */
    const part = document.getElementById(`${dest}-${sub}`);
    if (part) requestAnimationFrame(() => part.scrollIntoView({ behavior: reducedMotion() ? "auto" : "smooth", block: "start" }));
    else sub = undefined;
  }
  const hash = "#" + dest + (sub ? "/" + sub : "");
  if (location.hash !== hash) history[replace ? "replaceState" : "pushState"](null, "", hash);
  emit("view", { dest, sub });
}

export function initNav() {
  $$(".navbtn").forEach((b) => b.addEventListener("click", () => { go(b.dataset.dest); $("#main").scrollTo?.(0, 0); window.scrollTo(0, 0); }));
  window.addEventListener("popstate", () => go(location.hash.slice(1), true));
  on("navigate", (t) => go(t));
}
export const startNav = () => go(location.hash.slice(1), true);

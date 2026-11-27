// The device column (and the top strip on phones): connection, the panel preview (virtual.js draws what
// the panel shows), power, brightness. The activity log under it is log.js.
import { $, on, emit } from "./core.js?v=123ecfd";
import { CMD, MODE } from "./protocol.js?v=123ecfd";
import { panel, supported, connect, send, act } from "./ble.js?v=123ecfd";
import { EFFECTS, CLOCKS, MODES } from "./catalog.js?v=123ecfd";
import { drawDotText } from "./led.js?v=123ecfd";
import { initMirror } from "./mirror.js?v=123ecfd";
import { sizeText } from "./size.js?v=123ecfd";

let animName = (slot) => `slot ${slot + 1}`;
export const setAnimNamer = (fn) => { animName = fn; };

/* the panel card: not connected = a line and a big Connect button; connected = the panel's name, its
   firmware and size, a small Disconnect, and the controls */
function showState(s) {
  const on = s === "connected", btn = $("#btnConnect");
  $("#devCard").dataset.state = s;
  $("#connChip").dataset.state = s === "idle" ? "" : s;
  $("#connText").textContent = on ? panel.name : s === "connecting" ? "Connecting…" : s === "lost" ? "Connection lost" : "Not connected";
  $("#devVersion").textContent = on ? `${panel.custom ? `Firmware ${panel.version}` : `Stock firmware ${panel.version}`}, ${sizeText()}`
    : s === "connecting" ? "Pick the panel in the list." : s === "lost" ? "Switch the panel off and on, then connect again." : "Switch the panel on, then connect.";
  btn.textContent = on || s === "connecting" ? "Disconnect" : s === "lost" ? "Connect again" : "Connect panel";
  btn.classList.toggle("primary", !on && s !== "connecting");
  btn.classList.toggle("small", on || s === "connecting");
  $("#devControls").hidden = !on;
}

/* the caption under the live preview: what the panel shows, in words */
function showStatus(r) {
  const mode = r[5], eff = r[6], bright = r[7], power = r[8], flags = r[12];
  let what = MODES[mode] || "Something", kind = "";
  if (mode === MODE.EFFECT) { what = EFFECTS[eff] || `Effect ${eff + 1}`; kind = flags & 2 ? "effects shuffle" : "effect"; }
  else if (mode === MODE.CLOCK) { what = CLOCKS[r[14]] || "Clock"; kind = "clock face"; }
  else if (mode === MODE.ANIM) { what = eff === 255 ? "No animations stored" : animName(eff); kind = flags & 16 ? "animations, all in turn" : "animation"; }
  emit("now", power ? `${what}${kind ? `, ${kind}` : ""}` : "Switched off");
  if (document.activeElement !== $("#bright")) { $("#bright").value = bright; $("#brightOut").value = bright + "%"; }
  $("#power").checked = !!power;
}

export function initDevice() {
  initMirror();
  /* the name as on the banner: "openipixel" white, "color" in five colours from the logo's ring */
  const RING = ["#ff2222", "#e3ff22", "#22ff59", "#22acff", "#9122ff"];   /* tools/make_brand.py ring_hue() */
  const phone = matchMedia("(max-width: 860px)");                  /* layout.css: the strip at the top */
  const wordmark = () => drawDotText($("#wordmark"), "openipixelcolor", phone.matches ? 12 : 16, (i) => (i < 10 ? "#f0f4ff" : RING[i - 10]));
  wordmark();
  phone.addEventListener("change", wordmark);
  if (!supported) $("#noBt").hidden = false;
  $("#btnConnect").disabled = !supported;
  $("#btnConnect").addEventListener("click", () => connect());
  on("state", showState);
  on("size", () => { if (panel.state === "connected") showState("connected"); });
  on("status", showStatus);
  showState("idle");

  let brightTimer = 0;
  $("#bright").addEventListener("input", (e) => {
    $("#brightOut").value = e.target.value + "%";
    clearTimeout(brightTimer);
    brightTimer = setTimeout(() => act(() => send(CMD.BRIGHTNESS, [Number(e.target.value)])), 120);
  });
  $("#power").addEventListener("change", (e) => act(() => send(CMD.POWER, [e.target.checked ? 1 : 0])));
}

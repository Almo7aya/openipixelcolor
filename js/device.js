// The device column (and the top strip on phones): connection, the panel preview (virtual.js draws what
// the panel shows), power, brightness, the panel button.
import { $, on, emit } from "./core.js?v=06a6a24";
import { CMD, MODE } from "./protocol.js?v=06a6a24";
import { panel, supported, connect, send, act } from "./ble.js?v=06a6a24";
import { EFFECTS, CLOCKS, MODES } from "./catalog.js?v=06a6a24";
import { drawDotText } from "./led.js?v=06a6a24";
import { initMirror } from "./mirror.js?v=06a6a24";
import { sizeText } from "./size.js?v=06a6a24";

let animName = (slot) => `slot ${slot + 1}`;
export const setAnimNamer = (fn) => { animName = fn; };

function showState(s) {
  const chip = $("#connChip"), text = $("#connText"), btn = $("#btnConnect");
  chip.dataset.state = s === "idle" ? "" : s;
  text.textContent = s === "connected" ? `Connected to ${panel.name}` : s === "connecting" ? "Connecting…" :
    s === "lost" ? "Connection lost" : "Not connected";
  btn.textContent = s === "connected" || s === "connecting" ? "Disconnect" : s === "lost" ? "Connect again" : "Connect panel";
  btn.classList.toggle("primary", s !== "connected");
  $("#devVersion").textContent = s === "connected" ? `${panel.custom ? `Firmware ${panel.version}` : `Vendor firmware ${panel.version}`}, ${sizeText()}` : "";
  for (const id of ["#power", "#bright", "#btnKey"]) $(id).disabled = s !== "connected";
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
  drawDotText($("#wordmark"), "openipixelcolor", 16, (i) => (i < 10 ? "#f0f4ff" : RING[i - 10]));
  if (!supported) $("#noBt").hidden = false;
  $("#btnConnect").disabled = !supported;
  $("#btnConnect").addEventListener("click", () => connect());
  on("state", showState);
  on("status", showStatus);
  showState("idle");

  let brightTimer = 0;
  $("#bright").addEventListener("input", (e) => {
    $("#brightOut").value = e.target.value + "%";
    clearTimeout(brightTimer);
    brightTimer = setTimeout(() => act(() => send(CMD.BRIGHTNESS, [Number(e.target.value)])), 120);
  });
  $("#power").addEventListener("change", (e) => act(() => send(CMD.POWER, [e.target.checked ? 1 : 0])));
  $("#btnKey").addEventListener("click", () => act(() => send(CMD.KEY, [1])));
}

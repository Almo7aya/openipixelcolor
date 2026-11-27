// The Device destination (the firmware install itself is firmware.js): rotation, panel details, and the
// factory reset, which needs the button held down for a moment.
import { $, $$, on, holdToConfirm } from "../core.js?v=f294bea";
import { CMD } from "../protocol.js?v=f294bea";
import { panel, send, act, healthOf } from "../ble.js?v=f294bea";
import { toast, toastError } from "../toast.js?v=f294bea";
import { size, sizeText } from "../size.js?v=f294bea";

function details() {
  const on = !!panel.chWrite;
  $("#dvFw").textContent = on ? panel.versionFull : "–";
  $("#dvSize").textContent = on ? `${sizeText()}${size.known ? "" : " (model not recognised: assumed)"}${panel.type != null ? `, type 0x${panel.type.toString(16).padStart(2, "0")}` : ""}` : "–";
  $("#dvBt").textContent = on ? panel.btVersion : "–";
  $("#dvState").textContent = on ? healthOf() : "–";
  if (!on) $("#dvSaved").textContent = "–";
  else if (panel.custom && panel.level < 12) $("#dvSaved").textContent = "Not kept (needs newer firmware)";
  $$("#rotate .btn").forEach((b) => { b.disabled = !on; });
  $("#resetHold").disabled = !on;
}

export function initDeviceView() {
  $$("#rotate .btn").forEach((b) => b.addEventListener("click", () => act(() => send(CMD.ORIENT, [Number(b.dataset.orient)]))));
  holdToConfirm($("#resetHold"), () => {
    if (panel.custom && panel.level < 12) { toastError("Resetting needs newer firmware on the panel. Install it above."); return; }
    act(async () => { await send(CMD.FACTORY, [0xA5]); toast("Back to factory settings. They're saved in a few seconds.", { kind: "ok" }); });
  });
  on("state", details);
  on("connected", details);
  on("status", (r) => { if (r.length >= 14) $$("#rotate .btn").forEach((b) => b.setAttribute("aria-pressed", String(Number(b.dataset.orient) === r[13]))); });
  on("saved", (text) => { $("#dvSaved").textContent = text; });
  details();
}

// The hidden debug menu: hold Ctrl and Shift (Cmd and Shift on a Mac) and click the logo five times. It shows
// what the page knows about the panel, previews the dialogs and notices without their effects, and has a few
// tools for testing. Nothing in it sends anything to the panel.
import { $$, el, on } from "./core.js?v=8f6fef3";
import { panel, supported } from "./ble.js?v=8f6fef3";
import { size, sizeText } from "./size.js?v=8f6fef3";
import { healthText } from "./protocol.js?v=8f6fef3";
import { toast, toastError } from "./toast.js?v=8f6fef3";
import { log, logEverything, loggingEverything } from "./log.js?v=8f6fef3";
import { dialog, dlgButton, dlgFacts, dlgSection } from "./dialog.js?v=8f6fef3";
import { latest, fwDebug } from "./firmware.js?v=8f6fef3";

const CLICKS = 5, WITHIN = 1500;                       /* five clicks, each within 1.5 s of the last */
let liveFacts = null;                                  /* the facts in the open menu, kept up to date */

function facts() {
  const on = !!panel.chWrite, p = fwDebug.panelInfo(), ok = on && fwDebug.canInstall();
  return [
    ["Connection", on ? "Connected" : panel.state === "connecting" ? "Connecting" : "Not connected"],
    ["Firmware", !on ? "–" : panel.custom ? `openipixelcolor ${panel.version}${panel.rescue ? " (rescue mode)" : ""}` : `Stock ${panel.version}`],
    ["Model", !on ? "–" : p.model ? `${p.model}, ${size.known ? sizeText() : "size unknown"}` : "Unknown"],
    ["Bluetooth module", on && panel.btVersion ? panel.btVersion : "–"],
    ["Health", on && panel.health ? healthText(panel.health) : "–"],
    ["Install", !on ? "–" : ok ? "Allowed" : "Turned off: not supported", on && !ok ? "warn" : null],
    ["Latest release", latest ? `${latest.version}, ${new Date(latest.date).toLocaleDateString()}` : "Not loaded"],
  ];
}
function refresh() {
  if (!liveFacts || !liveFacts.isConnected) return;
  const n = dlgFacts(facts());
  liveFacts.replaceWith(n);
  liveFacts = n;
}

/* for a bug report: the page, the browser and the panel's details; not its name or Bluetooth address */
function report() {
  const p = fwDebug.panelInfo();
  return JSON.stringify({
    time: new Date().toISOString(),
    page: location.origin + location.pathname,
    browser: navigator.userAgent,
    webBluetooth: supported,
    panel: panel.chWrite ? {
      firmware: panel.version, openipixelcolor: panel.custom, rescueMode: panel.rescue, model: p.model,
      size: `${size.w} x ${size.h}`, sizeKnown: size.known, bluetoothModule: panel.btVersion,
      health: panel.health ? healthText(panel.health) : null, installAllowed: fwDebug.canInstall(),
    } : null,
    latestRelease: latest ? latest.version : null,
  }, null, 2);
}

function sampleNotices() {
  toast("This is how the page tells you something.", { title: "A notice" });
  toast("Done.", { kind: "ok" });
  toastError("This is a sample error. Nothing went wrong.");
}

export function openDebug() {
  const back = (how) => { if (how === undefined || how === "close") openDebug(); };   /* previews come back here */
  const status = el("p", { class: "dlg-note", role: "status" });
  const say = (t) => { status.textContent = t; };
  const everything = el("input", { type: "checkbox", checked: loggingEverything() });
  everything.addEventListener("change", () => {
    logEverything(everything.checked);
    log(everything.checked ? "Logging every message and reply" : "Logging the usual messages");
    say(everything.checked ? "The activity log now shows every message and reply." : "The activity log is back to the usual.");
  });
  const check = dlgButton("Check for a release", async () => {
    check.disabled = true; say("Checking…");
    try { const l = await fwDebug.checkRelease(); say(l ? `Latest release: ${l.version}.` : "No release found."); }
    catch (e) { say("Couldn't check: the page is offline, or the release list didn't load."); }
    check.disabled = false; refresh();
  });
  liveFacts = dlgFacts(facts());
  const d = dialog({
    tone: "info",
    icon: "debug",
    wide: true,
    title: "Debug",
    text: "For testing this page. Nothing here sends anything to the panel.",
    body: [
      liveFacts,
      dlgSection("Show",
        el("div", { class: "dlg-grid" },
          dlgButton("Update dialog", () => fwDebug.previewUpdate(back)),
          dlgButton("Not supported dialog", () => fwDebug.previewNotSupported(back)),
          dlgButton("Stock firmware notice", () => { d.close(); fwDebug.previewStockHint(); }),
          dlgButton("Sample notices", () => { d.close(); sampleNotices(); }))),
      dlgSection("Tools",
        el("label", { class: "switch dlg-switch" }, everything, "Log every message and reply"),
        el("div", { class: "dlg-grid" },
          check,
          dlgButton("Reset dismissed notices", () => {
            fwDebug.resetNotices();
            say("Later, Skip this version and the other notices will show again on the next connection.");
          }),
          dlgButton("Copy debug info", async () => {
            try { await navigator.clipboard.writeText(report()); say("Copied the page, browser and panel details (not the panel's name or address)."); }
            catch (e) { say("Couldn't copy: the browser didn't allow it."); }
          })),
        status),
    ],
    actions: [dlgButton("Close", () => d.close(), { primary: true })],
    onClose: () => { liveFacts = null; },
  });
}

export function initDebug() {
  let n = 0, t = 0;
  const click = (e) => {
    if (!e.shiftKey || !(e.ctrlKey || e.metaKey)) { n = 0; return; }
    e.preventDefault();
    const now = performance.now();
    n = now - t < WITHIN ? n + 1 : 1;
    t = now;
    if (n >= CLICKS) { n = 0; openDebug(); }
  };
  /* the logo top left (the rail), or at the top of the device column when the rail is a bottom bar */
  for (const logo of $$(".rail .mark, .dev-brand > img")) {
    logo.addEventListener("click", click);
    logo.addEventListener("mousedown", (e) => { if (e.shiftKey) e.preventDefault(); });   /* no text selection */
  }
  on("state", refresh);
  on("connected", refresh);
  on("release", refresh);
}

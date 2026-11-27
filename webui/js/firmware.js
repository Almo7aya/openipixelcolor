// Firmware install over Bluetooth (same rules as tools/ble/ota_flash_guarded.py). The Device card shows what
// the panel runs and the latest release, with one button for what to do (update, install, reinstall); a
// different file (the build on this computer, any .bin) can be chosen instead. Each install: 12 KB blocks
// that must each be confirmed, the restart, connect again, check the version. Also the flash self-test and
// the update notice.
import { $, $$, el, on, emit, store, kb } from "./core.js";
import { CMD, packet, u32, crc32, sameBytes, fwVersionOf, healthText, versionLevel, newerVersion,
         fwEncode, fwDecode, hasImageHeader, OTA_CHUNK } from "./protocol.js";
import { panel, rawSend, send, idle, reconnect, disconnect, refreshStatus, connect, supported } from "./ble.js";
import { size, sizeText } from "./size.js";
import { toast, toastError } from "./toast.js";
import { dialog, dlgButton, dlgFacts } from "./dialog.js";

/* openipixelcolor runs on one board: the 32 x 32 panel (model byte 0x81 in the time reply) whose stock
   firmware reports 3.x. Another panel could take the update and then never start again (there is no
   programming port), so the page installs nothing on it. A panel that runs openipixelcolor is that board. */
const OUR_MODEL = 0x81;
const canInstall = () => panel.custom || (panel.type === OUR_MODEL && /^3\./.test(panel.version));
/* what the page knows about the connected panel: size (when the model is known), model byte, versions */
const panelInfo = () => ({
  size: size.known ? sizeText() : "", model: panel.type != null ? `0x${panel.type.toString(16).padStart(2, "0")}` : "",
  firmware: panel.version, bt: panel.btVersion,
});
const panelText = (p) => `${p.size || (p.model ? `Model ${p.model}, size unknown` : "Unknown model")}, stock firmware ${p.firmware}`;
/* a new issue from .github/ISSUE_TEMPLATE/new-panel.yml, with what the page knows about the panel filled in
   (the form's field ids are the query keys); nothing that identifies the panel or its owner */
function reportLink(p) {
  const title = p.size ? `Support for the ${p.size} panel (stock firmware ${p.firmware})`
    : `Support for another panel (model ${p.model || "unknown"}, stock firmware ${p.firmware})`;
  return "https://github.com/Almo7aya/openipixelcolor/issues/new?" + new URLSearchParams({ template: "new-panel.yml",
    title, size: p.size, model: p.model, firmware: p.firmware, bt: p.bt });
}
function notSupportedDialog(p, onClose) {
  const close = dlgButton("Close", () => d.close());
  const d = dialog({
    tone: "warn",
    icon: "warn",
    title: "This panel isn't supported yet",
    text: "We'd be happy to add support for it. Until then, installing is turned off to keep the panel safe.",
    body: [dlgFacts([p.size ? ["Size", p.size] : ["Model", p.model || "Unknown"], ["Firmware", `Stock ${p.firmware}`]])],
    actions: [close, dlgButton("Report on GitHub", () => d.close(), { primary: true, href: reportLink(p), icon: "i-github" })],
    focus: close,
    onClose,
  });
}
/* shown once per connection when such a panel connects, and from Details on the Device card */
let nopeShown = false;
function showNotSupported() {
  nopeShown = true;
  notSupportedDialog(panelInfo());
}

/* Releases: the CI puts the latest one next to the hosted page (firmware/latest.json + the update file).
   Served by webui/serve.py on this computer, the page reads the hosted copy instead (GitHub Pages allows
   that) and also offers the local build (firmware/build/fw.ota.bin). */
const HOSTED_PAGE = "https://almo7aya.dev/openipixelcolor/";
const LOCAL_BUILD = "local-build/fw.ota.bin";
const onThisComputer = /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname);
const RELEASES = (onThisComputer ? HOSTED_PAGE : "") + "firmware/";

/* what the Install button installs: the latest release (fetched when it's clicked) unless a file was chosen
   under "Install a different file" */
const fw = { data: null, version: null, name: "", source: "release" };   // data: the encoded update file
export let latest = null;                             // { version, tag, file, size, date, release }

/* ---- what is chosen ---- */
function choose(bytes, name, source = "file") {
  let raw = null, enc = null, kind, ok = false;
  if (hasImageHeader(bytes)) { raw = bytes; enc = fwEncode(bytes); }
  else { const d = fwDecode(bytes); if (hasImageHeader(d)) { raw = d; enc = bytes; } }
  fw.version = null;
  if (!raw) kind = "Not a firmware file for this panel.";
  else if (raw.length > 0xF7800) kind = "Too big for this panel.";
  else {
    const m = new TextDecoder("latin1").decode(raw).match(/openipixelcolor (\d+(?:\.\d\d)?)\b/);
    fw.version = m ? m[1] : null;
    kind = m ? `openipixelcolor ${m[1]}` : "Other firmware for this panel (for example the vendor's)";
    ok = true;
  }
  fw.source = source;
  if (source === "file") {
    $("#fwFacts").hidden = false;
    $("#fwName").textContent = name;
    $("#fwSize").textContent = kb(bytes.length);
    $("#fwKind").textContent = kind;
    $("#fwKind").classList.toggle("bad", !ok);
    $("#fwBack").hidden = !latest;
  }
  $("#fwTwice").disabled = !fw.version;
  fw.data = ok ? enc : null;
  fw.name = name;
  disarm();
  render();
  return ok;
}
async function chooseRelease() {
  const r = await fetch(RELEASES + encodeURIComponent(latest.file), { cache: "no-store" });
  if (!r.ok) throw new Error("Couldn't download the release. Reload and try again.");
  return choose(new Uint8Array(await r.arrayBuffer()), `${latest.file} (latest release)`, "release") && fw.version === latest.version;
}
function backToRelease() {
  fw.source = "release"; fw.data = null; fw.version = latest ? latest.version : null;
  $("#fwFacts").hidden = true; $("#fwBack").hidden = true;
  disarm(); render();
}

/* ---- the steps shown while installing: check, send, restart, verify ---- */
function step(name, state) {
  const li = $(`#fwSteps [data-step="${name}"]`);
  if (li) li.dataset.state = state;
}
function resetSteps(round, rounds) {
  $("#fwRun").hidden = false;
  $$("#fwSteps li").forEach((li) => { li.dataset.state = ""; });
  $("#fwRound").textContent = rounds > 1 ? `Round ${round} of ${rounds}${round === 2 ? ": the new firmware installs itself" : ""}` : "";
}
function log(msg, err) {
  $("#fwRun").hidden = false;
  $("#fwLog").prepend(el("li", { class: err ? "err" : null }, msg));
  emit("install", { msg, err: !!err });
}

/* ---- sending ---- */
function expectAnswer(r, expect) {
  if (expect && !sameBytes([...r.slice(0, expect.length)], expect)) throw new Error(`Unexpected answer ${[...r].map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);
  return r;
}
async function frame(cmd, payload, expect, timeout = 20000) {
  return expectAnswer(await rawSend(packet(cmd, payload), timeout), expect);
}
/* st.heard: the panel answered the first update command, so it restarts 10 s after the last one */
async function otaSend(data, st) {
  const n = Math.ceil(data.length / OTA_CHUNK), prog = $("#fwProg");
  prog.hidden = false; prog.max = n; prog.value = 0;
  const r = await rawSend(packet(CMD.OTA_BEGIN, [n, ...u32(crc32(data)), ...u32(data.length)]), 10000);
  st.heard = true;
  expectAnswer(r, [5, 0, 0, 0xC0, 1]);
  for (let i = 0; i < n; i++) {
    const chunk = data.slice(i * OTA_CHUNK, (i + 1) * OTA_CHUNK);
    await frame(CMD.OTA_CHUNK, [i, ...u32(crc32(chunk)), ...u32(chunk.length), ...chunk], [5, 0, 1, 0xC0, 1], 30000);
    prog.value = i + 1;
    emit("install-progress", { done: i + 1, total: n });
  }
}

/* keep the computer awake and warn before closing the page while installing */
let wake = null;
const stay = (e) => { e.preventDefault(); e.returnValue = ""; };
async function guard(onOff) {
  if (onOff) {
    window.addEventListener("beforeunload", stay);
    try { wake = await navigator.wakeLock?.request("screen"); } catch (e) { wake = null; }
  } else {
    window.removeEventListener("beforeunload", stay);
    try { await wake?.release(); } catch (e) { /* already released */ }
    wake = null;
  }
}

/* Install what choose() prepared (the caller has shown the versions and the user said yes): send, wait
   for the restart, connect again, check the version; rounds 2 = once more with the new firmware receiving
   it. Resolves to { ok, error }. */
export async function runInstall(rounds) {
  if (!panel.chWrite || !fw.data || panel.busy) return { ok: false, error: "The panel isn't ready for an install." };
  if (!canInstall()) return { ok: false, error: `This panel isn't supported yet (${panelText(panelInfo())}). Nothing was installed.` };
  panel.busy = true; panel.holdDevice = true;
  render();
  await guard(true);
  const data = fw.data, want = fw.version, st = { heard: false };
  let sent = false;
  try {
    await idle();
    for (let round = 1; round <= rounds; round++) {
      resetSteps(round, rounds);
      step("check", "done");
      step("send", "active");
      log(round === 1 ? "Sending…" : "Installing again, with the new firmware receiving it…");
      sent = false; st.heard = false;
      await otaSend(data, st);
      sent = true;
      step("send", "done"); step("restart", "active");
      log("Sent. The panel is installing and restarting…");
      const { v, health } = await reconnect();
      step("restart", "done"); step("verify", "active");
      const got = fwVersionOf(v).text;
      if (want && got !== want) throw new Error(`The panel came back with ${got}, not ${want}: the update didn't install.`);
      if (health && health[5] !== 0) throw new Error(`The panel came back in rescue mode (${healthText(health)}). Install a working firmware.`);
      step("verify", "done");
      log(`Back, running ${got}${fwVersionOf(v).custom ? "" : " (stock firmware)"}.`);
    }
    log(rounds === 2 ? `Done. ${want} is installed and can update itself.` : "Done.");
    panel.busy = false; panel.holdDevice = false;
    refreshStatus();
    emit("connected", { custom: panel.custom, level: panel.level, rescue: panel.rescue, version: panel.version });
    return { ok: true };
  } catch (e) {
    const msg = sent ? e.message : `${e.message} Nothing was installed${st.heard ? "; the panel restarts in 10 s" : ""}.`;
    const active = $("#fwSteps li[data-state='active']");
    if (active) active.dataset.state = "error";
    log(msg, true);
    panel.busy = false; panel.holdDevice = false;
    if (!panel.chWrite) disconnect();
    else emit("connected", { custom: panel.custom, level: panel.level, rescue: panel.rescue, version: panel.version });
    return { ok: false, error: msg };
  } finally {
    await guard(false);
    $("#fwProg").hidden = true;
    render();
  }
}

/* ---- the card: what the panel runs, what's newest, one button for what to do ---- */
let armed = 0;
function disarm() { clearTimeout(armed); armed = 0; $("#fwConfirm").textContent = ""; render(); }
function render() {
  const on = !!panel.chWrite, btn = $("#fwInstall");
  $("#fwConnect").hidden = on;
  $("#fwOnPanel").textContent = !on ? "–" : panel.custom ? `openipixelcolor ${panel.version}${panel.rescue ? " (rescue mode)" : ""}` : `Stock firmware ${panel.version}`;
  $("#fwLatest").textContent = latest ? `openipixelcolor ${latest.version}` : "–";
  $("#fwLatestDate").textContent = latest ? new Date(latest.date).toLocaleDateString() : "";
  $("#fwNotes").hidden = !latest || !latest.release;
  if (latest && latest.release) $("#fwNotes").href = latest.release;
  $("#fwSelfTest").disabled = panel.busy || !on || !panel.custom;

  let status, label = "", primary = true;
  if (!on) status = "Connect a panel to check its firmware.";
  else if (panel.busy) status = "Installing. Keep the panel on and nearby.";
  else if (!canInstall()) status = "This panel isn't supported yet.";
  else if (fw.source === "file") {
    status = fw.data ? `Ready: ${fw.name}.` : "This file can't be installed.";
    label = fw.data ? `Install ${fw.version ? `openipixelcolor ${fw.version}` : "this file"}` : "";
  } else if (!latest) status = "No release found. You can still install a file.";
  else if (!panel.custom) { status = "Stock firmware."; label = `Install openipixelcolor ${latest.version}`; }
  else if (newerVersion(latest.version, panel.version)) { status = `Update available: ${latest.version}.`; label = `Update to ${latest.version}`; }
  else { status = "Up to date."; label = `Reinstall ${latest.version}`; primary = false; }
  $("#fwStatus").textContent = status;
  $("#fwStatus").classList.toggle("ok", on && status === "Up to date.");
  const unsupported = on && !panel.busy && !canInstall();
  $("#fwStatus").classList.toggle("bad", unsupported);
  $("#fwWhy").hidden = !unsupported;
  btn.hidden = !on || !label;
  btn.disabled = panel.busy;
  btn.textContent = armed ? "Click again to install" : label;
  btn.classList.toggle("primary", primary || !!armed);
}
async function onInstall() {
  if (!panel.chWrite || panel.busy || !canInstall()) return;
  if (!armed) {
    if (fw.source === "release") {                     /* the release is fetched when it's needed */
      try { if (!latest || !(await chooseRelease())) throw new Error("The release download didn't check out. Reload and try again."); }
      catch (e) { log(e.message, true); return; }
    }
    if (!fw.data) return;
    const v = await send(CMD.VERSION).catch(() => null);
    if (!v) { log("The panel didn't answer. Nothing was installed.", true); return; }
    const now = fwVersionOf(v);
    $("#fwConfirm").textContent = `${now.custom ? now.text : `Stock ${now.text}`} → ${fw.version || "this file"}. Keep the panel on and nearby.`;
    armed = setTimeout(disarm, 8000);
    render();
    return;
  }
  disarm();
  const r = await runInstall(fw.version && $("#fwTwice").checked ? 2 : 1);
  if (r.ok) toast(fw.version ? `Installed ${fw.version}.` : "Installed.", { kind: "ok" });
  else toastError(r.error);
}

async function selfTest() {
  if (!panel.chWrite || !panel.custom || panel.busy) return;
  panel.busy = true; render();
  try {
    await idle();
    const data = new Uint8Array(OTA_CHUNK).map((_, i) => (i * 37 + 11) & 255), good = crc32(data);
    await frame(CMD.OTA_BEGIN, [1, ...u32(~good >>> 0), ...u32(data.length)], [5, 0, 0, 0xC0, 1], 10000);
    const t0 = performance.now();
    const r = await frame(CMD.OTA_CHUNK, [0, ...u32(good), ...u32(data.length), ...data], null, 30000);
    const s = ((performance.now() - t0) / 1000).toFixed(1);
    if (sameBytes([...r.slice(0, 5)], [5, 0, 1, 0xC0, 0])) log("Flash check passed. The panel restarts in 10 s.");
    else log(`Flash check failed (unexpected answer after ${s} s). Don't install firmware on this panel.`, true);
  } catch (e) { log(e.message + " Don't install firmware until the flash check passes.", true); }
  panel.busy = false; render();
}

/* ---- update notice, in the dialog over the page: after connecting, when the panel runs an older
   openipixelcolor than the release. Update now goes to Device, where the install shows its steps. Panels
   with the stock firmware get a hint only (a toast), and only when they look like this board (canInstall);
   other panels get the "not supported" dialog. ---- */
let notice = null, later = false, vendorHinted = false;
async function updateFromNotice(want) {
  emit("navigate", "device");
  requestAnimationFrame(() => $(".card.fw").scrollIntoView({ block: "start", behavior: "smooth" }));
  let ok = false;
  try { ok = await chooseRelease(); } catch (e) { /* below */ }
  if (!ok) {
    const msg = "Couldn't download the update. Nothing was installed. Reload the page and try again.";
    log(msg, true); toastError(msg);
    return;
  }
  const r = await runInstall(1);
  if (r.ok) toast(`Updated to ${want}.`, { kind: "ok" });
  else toastError(r.error);
}
/* the update dialog; onUpdate / onSkip run as it closes, then onClose(how): "update", "skip" or "close" */
function updateDialog({ have, want, release, onUpdate, onSkip, onClose }) {
  let how = "close";
  const d = dialog({
    tone: "info",
    icon: "update",
    title: `openipixelcolor ${want} is ready`,
    text: "The update takes about a minute and the panel restarts by itself. Keep it on and nearby until it's done.",
    body: [
      dlgFacts([["On the panel", have], ["Latest release", want]]),
      release ? el("p", { class: "dlg-note" }, el("a", { href: release, target: "_blank", rel: "noopener" }, `What's new in ${want}`)) : null,
    ],
    actions: [
      dlgButton("Later", () => d.close()),
      dlgButton("Update now", () => { if (panel.busy) return; how = "update"; d.close(); onUpdate(); }, { primary: true }),
    ],
    after: dlgButton("Skip this version", () => { how = "skip"; d.close(); onSkip(); }, { quiet: true }),
    onClose: () => { if (onClose) onClose(how); },
  });
  return d;
}
const stockHint = (version) => toast(`openipixelcolor ${version} can be installed under Device.`,
  { title: "This panel runs the stock firmware", timeout: 12000, action: { label: "Open Device", fn: () => emit("navigate", "device") } });
function showUpdate() {
  if (!latest || !panel.chWrite || panel.busy || notice) return;
  if (!panel.custom) {
    if (vendorHinted || !canInstall()) return;
    stockHint(latest.version);
    vendorHinted = true;
    return;
  }
  if (!newerVersion(latest.version, panel.version) || later || store.get("skipUpdate", "") === latest.version) return;
  const want = latest.version;
  notice = updateDialog({
    have: panel.version, want, release: latest.release,
    onUpdate: () => updateFromNotice(want),
    onSkip: () => { store.set("skipUpdate", want); toast(`Skipped ${want}. You can still install it under Device.`); },
    onClose: (how) => { if (how !== "update") later = true; notice = null; },
  });
}

/* the release list: firmware/latest.json next to the hosted page */
async function loadLatest() {
  const r = await fetch(RELEASES + "latest.json", { cache: "no-store" });
  const j = r.ok ? await r.json() : null;
  if (!j || !j.file || !versionLevel(String(j.version))) return latest;
  latest = Object.assign(j, { version: String(j.version) });
  render();
  emit("release", latest);
  return latest;
}

/* ---- for the debug menu (debug.js): the dialogs and notices as previews, with sample data when the
   connected panel doesn't show them; previews never install anything ---- */
const SAMPLE_PANEL = { size: "48 × 24", model: "0x89", firmware: "23.11", bt: "1.05" };
export const fwDebug = {
  canInstall,
  panelInfo,
  checkRelease: loadLatest,
  previewUpdate(onClose) {
    const want = latest ? latest.version : "106";
    const have = panel.custom && newerVersion(want, panel.version) ? panel.version : String(Math.max(100, Number(want) - 1));
    updateDialog({
      have, want, release: latest && latest.release,
      onUpdate: () => toast("Preview only: nothing was installed."),
      onSkip: () => toast("Preview only: the version wasn't skipped."),
      onClose,
    });
  },
  previewNotSupported(onClose) {
    notSupportedDialog(panel.chWrite && !canInstall() ? panelInfo() : SAMPLE_PANEL, onClose);
  },
  previewStockHint() { stockHint(latest ? latest.version : "106"); },
  /* Later, Skip this version, the stock firmware hint and the "not supported" dialog show again */
  resetNotices() { later = false; vendorHinted = false; nopeShown = false; store.del("skipUpdate"); },
};

export function initFirmware() {
  const drop = $("#fwDrop");
  const take = async (f) => { if (f) choose(new Uint8Array(await f.arrayBuffer()), f.name); };
  $("#fwFile").addEventListener("change", (e) => { take(e.target.files[0]); e.target.value = ""; });
  drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
  drop.addEventListener("dragleave", () => drop.classList.remove("over"));
  drop.addEventListener("drop", (e) => { e.preventDefault(); drop.classList.remove("over"); take(e.dataTransfer.files[0]); });
  $("#fwBack").addEventListener("click", backToRelease);
  $("#fwLocal").addEventListener("click", async () => {
    try {
      const r = await fetch(LOCAL_BUILD, { cache: "no-store" });
      if (!r.ok) throw new Error("The build isn't there any more. Build the firmware first.");
      choose(new Uint8Array(await r.arrayBuffer()), "the build on this computer");
    } catch (e) { log(e.message, true); }
  });
  $("#fwInstall").addEventListener("click", onInstall);
  $("#fwWhy").addEventListener("click", showNotSupported);
  $("#fwSelfTest").addEventListener("click", selfTest);
  $("#fwConnect").addEventListener("click", () => connect());
  $("#fwConnect").disabled = !supported;

  if (onThisComputer) {
    fetch(LOCAL_BUILD, { method: "HEAD", cache: "no-store" }).then((r) => {
      if (!r.ok) return;
      const t = r.headers.get("Last-Modified");
      $("#fwLocalNote").textContent = t ? `firmware/build/fw.ota.bin, built ${new Date(t).toLocaleString()}` : "firmware/build/fw.ota.bin";
      $("#fwLocalRow").hidden = false;
      $("#fwOther").open = true;                       /* working on the firmware: show it */
    }).catch(() => { /* not served by serve.py */ });
  }
  loadLatest().then((l) => { if (l) showUpdate(); }).catch(() => { /* offline, or no release published yet */ });

  on("connected", () => {
    render();
    if (!panel.chWrite || panel.busy) return;
    if (!canInstall()) { if (!nopeShown) showNotSupported(); }
    else showUpdate();
  });
  on("disconnected", () => { nopeShown = false; if (!panel.busy) disarm(); else render(); });
  on("state", render);
  render();
}

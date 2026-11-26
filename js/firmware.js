// Firmware install over Bluetooth (same rules as tools/ble/ota_flash_guarded.py). The Device card shows what
// the panel runs and the latest release, with one button for what to do (update, install, reinstall); a
// different file (the build on this computer, any .bin) can be chosen instead. Each install: 12 KB blocks
// that must each be confirmed, the restart, connect again, check the version. Also the flash self-test and
// the update notice.
import { $, $$, el, on, emit, store, kb } from "./core.js?v=7d6fc0c";
import { CMD, packet, u32, crc32, sameBytes, fwVersionOf, healthText, versionLevel, newerVersion,
         fwEncode, fwDecode, hasImageHeader, OTA_CHUNK } from "./protocol.js?v=7d6fc0c";
import { panel, rawSend, send, idle, reconnect, disconnect, refreshStatus, connect, supported } from "./ble.js?v=7d6fc0c";
import { toast, toastError } from "./toast.js?v=7d6fc0c";

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
async function frame(cmd, payload, expect, timeout = 20000) {
  const r = await rawSend(packet(cmd, payload), timeout);
  if (expect && !sameBytes([...r.slice(0, expect.length)], expect)) throw new Error(`Unexpected answer ${[...r].map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);
  return r;
}
async function otaSend(data) {
  const n = Math.ceil(data.length / OTA_CHUNK), prog = $("#fwProg");
  prog.hidden = false; prog.max = n; prog.value = 0;
  await frame(CMD.OTA_BEGIN, [n, ...u32(crc32(data)), ...u32(data.length)], [5, 0, 0, 0xC0, 1], 10000);
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
  panel.busy = true; panel.holdDevice = true;
  render();
  await guard(true);
  const data = fw.data, want = fw.version;
  let sent = false;
  try {
    await idle();
    for (let round = 1; round <= rounds; round++) {
      resetSteps(round, rounds);
      step("check", "done");
      step("send", "active");
      log(round === 1 ? "Sending…" : "Installing again, with the new firmware receiving it…");
      sent = false;
      await otaSend(data);
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
    const msg = sent ? e.message : `${e.message} Nothing was installed; the panel restarts in 10 s.`;
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
  else if (fw.source === "file") {
    status = fw.data ? `Ready: ${fw.name}.` : "This file can't be installed.";
    label = fw.data ? `Install ${fw.version ? `openipixelcolor ${fw.version}` : "this file"}` : "";
  } else if (!latest) status = "No release found. You can still install a file.";
  else if (!panel.custom) { status = "Stock firmware."; label = `Install openipixelcolor ${latest.version}`; }
  else if (newerVersion(latest.version, panel.version)) { status = `Update available: ${latest.version}.`; label = `Update to ${latest.version}`; }
  else { status = "Up to date."; label = `Reinstall ${latest.version}`; primary = false; }
  $("#fwStatus").textContent = status;
  $("#fwStatus").classList.toggle("ok", on && status === "Up to date.");
  btn.hidden = !on || !label;
  btn.disabled = panel.busy;
  btn.textContent = armed ? "Click again to install" : label;
  btn.classList.toggle("primary", primary || !!armed);
}
async function onInstall() {
  if (!panel.chWrite || panel.busy) return;
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

/* ---- update notice: after connecting, when the panel runs an older openipixelcolor than the release.
   Panels with the vendor firmware get a hint only: the page can't tell whether they have this board. ---- */
let notice = null, later = false, vendorHinted = false;
function showUpdate() {
  if (!latest || !panel.chWrite || panel.busy || notice) return;
  if (!panel.custom) {
    if (!vendorHinted) toast(`openipixelcolor ${latest.version} can be installed under Device.`,
      { title: "This panel runs the stock firmware", timeout: 12000, action: { label: "Open Device", fn: () => emit("navigate", "device") } });
    vendorHinted = true;
    return;
  }
  if (!newerVersion(latest.version, panel.version) || later || store.get("skipUpdate", "") === latest.version) return;
  const t = toast(`This panel has ${panel.version}. Takes about a minute.`,
    { kind: "update", title: `Update available: ${latest.version}`, timeout: 0 });
  notice = t;
  const prog = el("progress", { value: 0, max: 1, hidden: true });
  const now = el("button", { class: "btn small primary" }, "Update now");
  const wait = el("button", { class: "btn small" }, "Later");
  const skip = el("button", { class: "btn small ghost" }, "Skip this version");
  const row = el("div", { class: "row" }, now, wait, skip);
  const notes = latest.release ? el("a", { href: latest.release, target: "_blank", rel: "noopener" }, "What's new in this version") : null;
  t.node.append(prog, row);
  if (notes) t.node.append(notes);
  const close = () => { t.close(); notice = null; };
  t.node.querySelector(".x").addEventListener("click", () => { later = true; notice = null; });
  wait.addEventListener("click", () => { later = true; close(); });
  skip.addEventListener("click", () => { store.set("skipUpdate", latest.version); close(); toast(`Skipped ${latest.version}. You can still install it under Device.`); });
  now.addEventListener("click", async () => {
    if (panel.busy) return;
    const want = latest.version, title = t.node.querySelector(".toast-msg strong");
    row.hidden = true; if (notes) notes.hidden = true;
    title.textContent = `Updating to ${want}`;
    t.update("Downloading…");
    prog.hidden = false;
    const follow = (d) => { if (notice === t) t.update(d.msg); };
    const moving = (d) => { prog.max = d.total; prog.value = d.done; };
    on("install", follow); on("install-progress", moving);
    let ok = false;
    try { ok = await chooseRelease(); } catch (e) { /* shown below */ }
    const r = ok ? await runInstall(1) : { ok: false, error: "Couldn't download it. Nothing was installed." };
    prog.hidden = true;
    if (r.ok) { title.textContent = `Updated to ${want}`; t.update("All done."); setTimeout(close, 8000); }
    else {
      t.node.classList.replace("update", "err");
      title.textContent = "Update failed"; t.update(r.error);
      row.replaceChildren(el("button", { class: "btn small", onclick: () => { close(); emit("navigate", "device"); } }, "Open Device"));
      row.hidden = false;
    }
  });
}

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
  fetch(RELEASES + "latest.json", { cache: "no-store" }).then((r) => (r.ok ? r.json() : null)).then((j) => {
    if (!j || !j.file || !versionLevel(String(j.version))) return;
    latest = Object.assign(j, { version: String(j.version) });
    render();
    emit("release", latest);
    showUpdate();
  }).catch(() => { /* offline, or no release published yet */ });

  on("connected", () => { render(); if (!panel.busy) showUpdate(); });
  on("disconnected", () => { if (!panel.busy) disarm(); else render(); });
  on("state", render);
  render();
}

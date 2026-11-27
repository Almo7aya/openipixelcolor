// Web Bluetooth link to the panel: connect, send packets, match replies, poll the status, reconnect after a
// restart. Other modules read `panel` and listen for: connected, disconnected, state, status, saved.
import { emit, sleep } from "./core.js?v=d0fb26c";
import { CMD, packet, fwVersionOf, healthText, savedText } from "./protocol.js?v=d0fb26c";
import { toastError } from "./toast.js?v=d0fb26c";
import { setSize, typeSize } from "./size.js?v=d0fb26c";
import { logPacket } from "./log.js?v=d0fb26c";

const SVC = 0x00fa, WR = 0xfa02, NT = 0xfa03;
export const supported = !!navigator.bluetooth;

export const panel = {
  device: null, chWrite: null, name: "",
  state: "idle",                     // idle | connecting | connected | lost
  custom: false, level: 0, version: "", versionFull: "", btVersion: "", rescue: false, health: null,
  busy: false,                       // a firmware step runs: other sends are refused
  holdDevice: false,                 // a firmware install keeps the device to reconnect after the restart
  status: null,                      // last 0xF106 reply
};
let waiter = null, chain = Promise.resolve(), pollTimer = 0;
const taps = [];
/* fn(packet) for every packet the panel accepted for writing (the panel preview replays them) */
export const onPacket = (fn) => taps.push(fn);
const setState = (s) => { panel.state = s; emit("state", s); };

/* Every reply repeats the command it answers (bytes 2-3). The module delivers 16-byte replies twice, so a
   reply only counts for the request with the same command (and match(), when given); strays are ignored.
   A request finishes when its write has completed AND its reply has arrived. */
function onNotify(ev) {
  const dv = ev.target.value;
  const v = new Uint8Array(dv.buffer.slice(dv.byteOffset, dv.byteOffset + dv.byteLength));
  if (waiter && v.length >= 4 && (v[2] | (v[3] << 8)) === waiter.cmd && (!waiter.match || waiter.match(v))) waiter.take(v);
}
export function rawSend(bytes, timeout, match) {
  return new Promise(async (resolve, reject) => {
    if (!panel.chWrite) { reject(new Error("Connect the panel first.")); return; }
    let reply = null, written = false, done = false;
    const end = (fn, arg) => { if (done) return; done = true; clearTimeout(w.timer); if (waiter === w) waiter = null; fn(arg); };
    const w = waiter = {
      cmd: bytes[2] | (bytes[3] << 8), match,
      take: (v) => { if (!reply) reply = v; if (written) end(resolve, reply); },
      timer: setTimeout(() => end(reject, new Error("The panel didn't answer.")), timeout),
    };
    try {
      for (let o = 0; o < bytes.length; o += 244) await panel.chWrite.writeValueWithResponse(bytes.slice(o, o + 244));
      written = true;
      logPacket(bytes);
      for (const t of taps) try { t(bytes); } catch (e) { /* the preview must never break a request */ }
      if (reply) end(resolve, reply);
    } catch (e) { end(reject, e); }
  });
}
/* queued: one request at a time */
export function send(cmd, payload = [], timeout = 4000, match) {
  if (panel.busy) return Promise.reject(new Error("Wait until the firmware step finishes."));
  const job = chain.then(() => rawSend(packet(cmd, payload), timeout, match));
  chain = job.catch(() => {});
  return job;
}
export const idle = () => chain;

/* run an action that talks to the panel; errors become a notice (with Connect when that's the problem) */
export async function act(fn) {
  if (!panel.chWrite) { toastError("Connect the panel first.", supported ? { label: "Connect", fn: connect } : null); return false; }
  try { await fn(); refreshSoon(); return true; }
  catch (e) { toastError(e.message); return false; }
}
export const needsFirmware = (level) => panel.custom && panel.level >= level;

/* ---- connecting ---- */
export function releaseDevice() {
  const d = panel.device;
  panel.device = null;
  if (!d) return;
  d.removeEventListener("gattserverdisconnected", onDisconnected);
  try { d.gatt.disconnect(); } catch (e) { /* already gone */ }
}
function onDisconnected() {
  panel.chWrite = null; waiter = null; panel.custom = false; panel.level = 0; panel.version = ""; panel.status = null;
  clearInterval(pollTimer);
  if (linking) return;                                 /* openLink() tries again with the same panel */
  if (panel.holdDevice) { setState("connecting"); emit("disconnected", { expected: true }); return; }
  const wasBusy = panel.busy;
  releaseDevice();
  panel.busy = false;
  setState(wasBusy ? "lost" : "idle");
  emit("disconnected", { expected: false });
}
window.addEventListener("pagehide", releaseDevice);

/* The GATT link, retried: on Windows especially, the link often drops right after it opens ("GATT Server
   is disconnected. Cannot retrieve services."), more so just after another page or app let the panel go.
   A second try a moment later usually works. While this runs, a disconnect doesn't forget the panel. */
let linking = false;
async function openLink() {
  for (let i = 0; ; i++) {
    try {
      const server = await panel.device.gatt.connect();
      const svc = await server.getPrimaryService(SVC);
      const wr = await svc.getCharacteristic(WR), nt = await svc.getCharacteristic(NT);
      await nt.startNotifications();
      return { wr, nt };
    } catch (e) {
      if (i >= 3 || e.name === "NotFoundError" || e.name === "SecurityError") throw e;   /* not a dropped link */
      try { panel.device.gatt.disconnect(); } catch (x) { /* not connected */ }
      await sleep(600 + i * 700);
    }
  }
}

/* GATT connection to panel.device, then time, versions and health. Uses rawSend: also works during a
   firmware step (panel.busy). */
export async function attach() {
  linking = true;
  let link;
  try { link = await openLink(); } finally { linking = false; }
  panel.chWrite = link.wr;
  link.nt.addEventListener("characteristicvaluechanged", onNotify);
  const now = new Date();
  const tr = await rawSend(packet(CMD.TIME, [now.getHours(), now.getMinutes(), now.getSeconds(), 0]), 4000);
  const v = await rawSend(packet(CMD.VERSION), 4000);
  const ver = fwVersionOf(v);
  Object.assign(panel, { custom: ver.custom, level: ver.level, version: ver.text,
    versionFull: `${ver.text}${ver.custom ? " (openipixelcolor)" : " (vendor firmware)"}`,
    btVersion: `${v[6]}.${String(v[7]).padStart(2, "0")}`, health: null, rescue: false });
  if (ver.custom) {
    panel.health = await rawSend(packet(CMD.DIAG), 4000).catch(() => null);
    panel.rescue = !!(panel.health && panel.health[5] !== 0);
  }
  /* the matrix size: the model's type byte in the time reply; openipixelcolor 101+ says it exactly */
  const type = tr.length > 4 ? tr[4] : null, known = typeSize(type);
  let w = known ? known[0] : 32, h = known ? known[1] : 32, sure = !!known;
  if (ver.custom && ver.level >= 101) {
    const p = await rawSend(packet(CMD.PANEL_INFO), 4000).catch(() => null);
    if (p && p[4] === 1 && p.length >= 9) { w = p[5] | (p[6] << 8); h = p[7] | (p[8] << 8); sure = true; }
  }
  panel.type = type;
  setSize(w, h, type, sure);
  setState("connected");
  clearInterval(pollTimer);
  pollTimer = setInterval(() => { if (!panel.busy) refreshStatus(); }, 4000);
  emit("connected", { custom: panel.custom, level: panel.level, rescue: panel.rescue, version: panel.version });
  return { v, health: panel.health };
}
export async function connect() {
  if (!supported) return;
  if (panel.device) { disconnect(); return; }
  try {
    const device = await navigator.bluetooth.requestDevice({ filters: [{ namePrefix: "LED_BLE" }, { services: [SVC] }], optionalServices: [SVC] });
    panel.device = device;
    panel.name = device.name || "panel";
    device.addEventListener("gattserverdisconnected", onDisconnected);
    setState("connecting");
    emit("connecting");
    await attach();
    await refreshStatus();
  } catch (e) {
    if (e.name === "NotFoundError") {
      if (panel.state !== "connected") setState("idle");
      toastError("No panel picked. If it isn't in the list: it takes one connection at a time, so close other tabs or apps using it, then switch the panel off and on.");
    } else {
      if (panel.device) { releaseDevice(); onDisconnected(); }
      setState("idle");
      const dropped = /GATT|disconnect|NetworkError|connection/i.test(`${e.name} ${e.message}`);
      toastError(dropped ? "The panel dropped the connection. Close anything else connected to it (the phone app, another tab), " +
        "then try again. If it keeps happening, switch the panel off and on." : e.message, { label: "Try again", fn: connect });
    }
  }
}
export function disconnect() {
  const d = panel.device;
  panel.holdDevice = false;
  onDisconnected();
  try { d && d.gatt.disconnect(); } catch (e) { /* gone */ }
}

/* after a restart: connect to the same panel again (no picker needed), for up to 90 s */
export async function reconnect() {
  const t0 = performance.now();
  while (performance.now() - t0 < 90000) {
    await sleep(3000);
    if (!panel.device) throw new Error("The panel was forgotten. Connect it again by hand.");
    try { return await Promise.race([attach(), sleep(20000).then(() => { throw new Error("timeout"); })]); }
    catch (e) { try { panel.device.gatt.disconnect(); } catch (x) { /* not connected */ } }
  }
  throw new Error("The panel didn't come back within 90 seconds. Switch it off and on, then connect again.");
}

/* ---- status polling ---- */
let refreshTimer = 0;
export function refreshSoon() { clearTimeout(refreshTimer); refreshTimer = setTimeout(refreshStatus, 400); }
export async function refreshStatus() {
  if (!panel.chWrite || !panel.custom || panel.busy) return;
  try {
    const r = await send(CMD.STATUS);
    if (r[2] !== 0x06 || r[3] !== 0xF1) return;
    panel.status = r;
    emit("status", r);
    if (panel.level >= 12) {
      const s = await send(CMD.SAVED);
      if (s[2] === 0x09 && s[3] === 0xF1 && s.length >= 13) emit("saved", savedText(s));
    }
  } catch (e) { /* the next poll retries */ }
}
export const healthOf = () => (panel.health ? healthText(panel.health) : "–");

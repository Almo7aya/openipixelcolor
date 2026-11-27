// The activity log in the device column: what the page did with the panel and what came back, newest at
// the bottom. Commands are logged as they're sent (ble.js), except the ones that would flood it (the status
// poll every 4 s, animation read-back and upload pieces, firmware blocks); errors from every notice;
// connecting, installs, releases. The last 200 lines, in this page only.
import { $, el, on } from "./core.js?v=ed0e7b5";
import { CMD } from "./protocol.js?v=ed0e7b5";

const MAX = 200;
const NAMES = Object.fromEntries(Object.entries(CMD).map(([k, v]) => [v, k.toLowerCase().replace(/_/g, " ")]));
const QUIET = new Set([CMD.STATUS, CMD.SAVED, CMD.ANIM_INFO, CMD.ANIM_READ, CMD.ANIM_DATA, CMD.OTA_CHUNK, CMD.DIAG]);
let list = null, pending = [], last = null;
/* from the debug menu, for this page only: also the quiet commands, and every reply from the panel */
let everything = false;
export const logEverything = (on) => { everything = !!on; };
export const loggingEverything = () => everything;

const stamp = () => new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false });
function add(li) {
  if (!list) { pending.push(li); return; }
  const stick = list.scrollHeight - list.scrollTop - list.clientHeight < 24;   /* at the bottom: follow it */
  list.append(li);
  while (list.children.length > MAX) list.firstChild.remove();
  if (stick) list.scrollTop = list.scrollHeight;
}
/* kind: info | ok | err | send */
export function log(text, kind = "info") {
  last = null;
  add(el("li", { class: kind }, el("time", {}, stamp()), el("span", {}, text)));
}
/* a packet the page sent: its name and the first 8 bytes of what it carried. The same command again within
   2 s (drawing, the brightness slider, a text in pieces) updates its line: "×5" and the latest bytes. */
export function logPacket(bytes) {
  const cmd = bytes[2] | (bytes[3] << 8), t = performance.now();
  if (QUIET.has(cmd) && !everything) return;
  const args = [...bytes.subarray(4, 12)].join(" ");                  /* as numbers: brightness 65 reads "65" */
  const text = `→ ${NAMES[cmd] || "0x" + cmd.toString(16)}${args ? " " + args : ""}${bytes.length > 12 ? " …" : ""}`;
  if (last && last.cmd === cmd && t - last.t < 2000 && last.li.isConnected) {
    last.n++; last.t = t;
    last.li.firstChild.textContent = stamp();
    last.li.lastChild.textContent = `${text}  ×${last.n}`;
    return;
  }
  const li = el("li", { class: "send" }, el("time", {}, stamp()), el("span", {}, text));
  add(li);
  last = { cmd, t, n: 1, li };
}
/* a reply from the panel (only when logging everything): its command and the first 12 bytes after it */
export function logReply(bytes) {
  if (!everything || bytes.length < 4) return;
  const cmd = bytes[2] | (bytes[3] << 8);
  last = null;
  add(el("li", { class: "recv" }, el("time", {}, stamp()),
    el("span", {}, `← ${NAMES[cmd] || "0x" + cmd.toString(16)} ${[...bytes.subarray(4, 16)].join(" ")}${bytes.length > 16 ? " …" : ""}`)));
}

export function initLog() {
  list = $("#logList");
  list.append(...pending);
  pending = [];
  $("#logClear").addEventListener("click", () => { list.replaceChildren(); last = null; });
  on("connecting", () => log("Connecting…"));
  on("connected", (d) => log(`Connected: ${d.custom ? `openipixelcolor ${d.version}` : `stock firmware ${d.version}`}${d.rescue ? " (rescue mode)" : ""}`, "ok"));
  on("disconnected", (d) => log(d.expected ? "Restarting…" : "Disconnected"));
  on("install", (d) => log(d.msg, d.err ? "err" : "info"));
  on("release", (r) => log(`Latest firmware release: ${r.version}`));
  let count = -1;                                      /* "library" comes again for every preview read back */
  on("library", (items) => {
    if (items.length === count) return;
    count = items.length;
    if (count) log(`${count} ${count === 1 ? "animation" : "animations"} on the panel`);
  });
  log("Ready");
}

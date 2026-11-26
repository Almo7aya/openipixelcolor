// A still picture: any image file, cut or fitted to the panel's size, shown on the panel preview, sent with 0xF103.
// (Moving pictures go to the animation maker: they need the panel's animation store.)
import { $, on, emit, segmented } from "../core.js?v=7f0cc15";
import { CMD } from "../protocol.js?v=7f0cc15";
import { send, act } from "../ble.js?v=7f0cc15";
import { pix, drawMirror, claimMirror, mirrorNote, mirrorOwner } from "../mirror.js?v=7f0cc15";
import { toast } from "../toast.js?v=7f0cc15";
import { readFiles, fitFrames } from "./import.js?v=7f0cc15";
import { size, sizeText } from "../size.js?v=7f0cc15";

const P = { src: null, name: "", frames: 0, fit: "cover", rgb: null };

function render() {
  if (!P.src) return;
  const [f] = fitFrames([P.src], { fit: P.fit, smooth: $("#pSmooth").value === "1", bg: $("#pBg").value, w: size.w, h: size.h });
  P.rgb = f.rgb;
  show();
}
function show() {
  if (!P.rgb) return;
  claimMirror("picture");
  if (pix.length === P.rgb.length) pix.set(P.rgb);
  drawMirror();
  mirrorNote(`Picture preview: ${P.name}, ${sizeText()}.`);
}
async function load(list) {
  const note = $("#pNote");
  note.classList.remove("err");
  try {
    const r = await readFiles([...list].slice(0, 1), 10);
    P.src = r.frames[0]; P.name = r.name; P.frames = r.frames.length;
    note.textContent = P.frames > 1 ? `${r.name}: a moving picture. Only its first frame is used here.` : r.name;
    $("#pAnim").hidden = P.frames < 2;
    $("#pSend").disabled = false;
    render();
  } catch (e) { note.textContent = e.message || "That file couldn't be read."; note.classList.add("err"); }
}

export function initPicture() {
  const drop = $("#pDrop");
  $("#pFile").addEventListener("change", (e) => { if (e.target.files.length) load(e.target.files); e.target.value = ""; });
  drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
  drop.addEventListener("dragleave", () => drop.classList.remove("over"));
  drop.addEventListener("drop", (e) => { e.preventDefault(); drop.classList.remove("over"); if (e.dataTransfer.files.length) load(e.dataTransfer.files); });
  segmented($("#pFit"), (v) => { P.fit = v; render(); });
  $("#pSmooth").addEventListener("change", render);
  $("#pBg").addEventListener("input", render);
  $("#pAnim").addEventListener("click", () => emit("navigate", "create/animation"));
  on("view", ({ dest, sub }) => { if (dest === "create" && sub === "picture") show(); });
  on("size", ({ changed }) => {                       /* fit the picture to the new panel's size */
    if (!changed || !P.src) return;
    P.rgb = fitFrames([P.src], { fit: P.fit, smooth: $("#pSmooth").value === "1", bg: $("#pBg").value, w: size.w, h: size.h })[0].rgb;
    if (mirrorOwner() === "picture") show();
  });
  $("#pSend").addEventListener("click", () => {
    if (!P.rgb) return;
    show();
    act(async () => { await send(CMD.PICTURE, [...P.rgb], 8000); toast("Picture sent. The panel keeps it after switching off.", { kind: "ok" }); emit("show-live"); });
  });
}

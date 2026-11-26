/* Animations section: what is stored on the panel, making animations (files, a frame editor, generators),
 * a preview decoded exactly like the panel plays it (anim.js) and the upload (docs/PROTOCOL.md, Animations).
 * Uses the Bluetooth helpers and the mirror of index.html (send, status, pix, drawMirror, ...). */
"use strict";
(() => {
  const NPX = OPA.NPX, FW_MIN = 18;
  const CMD = { BEGIN: 0xF10C, DATA: 0xF10D, LIST: 0xF10E, INFO: 0xF10F, DELETE: 0xF110, PLAY: 0xF111 };
  const ALL = 0xFF, KEEP = 0xFE;
  const REASONS = {
    3: "The panel refused the request.", 4: "The panel isn't ready for animations (is it in rescue mode?).",
    5: "The upload stopped: the panel waited too long for data.", 6: "A piece arrived out of order.",
    7: "A piece had the wrong size.", 8: "The panel refused the file's header.",
    9: "The panel couldn't write its flash memory.", 10: "The copy in the panel's memory didn't check out.",
  };

  const rd32 = (v, o) => (v[o] | (v[o + 1] << 8) | (v[o + 2] << 16) | (v[o + 3] << 24)) >>> 0;
  const kb = (n) => (n < 1024 ? `${n} bytes` : `${(n / 1024).toFixed(n < 10240 ? 1 : 0)} KB`);
  const store = {
    get(k, d) { try { const v = JSON.parse(localStorage.getItem(k)); return v == null ? d : v; } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* private window or full */ } },
  };
  const toB64 = (u8) => { let s = ""; for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode(...u8.subarray(i, i + 4096)); return btoa(s); };
  const fromB64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
  function canvas32() { const c = document.createElement("canvas"); c.width = c.height = 32; return c; }
  function rgbOf(rgba) { const o = new Uint8Array(NPX * 3); for (let i = 0; i < NPX; i++) { o[i * 3] = rgba[i * 4]; o[i * 3 + 1] = rgba[i * 4 + 1]; o[i * 3 + 2] = rgba[i * 4 + 2]; } return o; }
  function thumbOf(rgb) {
    const c = canvas32(), ctx = c.getContext("2d"), im = ctx.createImageData(32, 32);
    for (let i = 0; i < NPX; i++) { im.data[i * 4] = rgb[i * 3]; im.data[i * 4 + 1] = rgb[i * 3 + 1]; im.data[i * 4 + 2] = rgb[i * 3 + 2]; im.data[i * 4 + 3] = 255; }
    ctx.putImageData(im, 0, 0);
    return c.toDataURL("image/png");
  }
  const ready = () => chWrite && custom && fwMinor >= FW_MIN;
  function needPanel() {
    if (!chWrite) { status("Connect the panel first.", true); $("#btnConnect").focus(); return false; }
    if (!custom || fwMinor < FW_MIN) { status(`Animations need openpanel32 99.${FW_MIN} or newer. Install it in the Firmware section.`, true); return false; }
    return true;
  }

  const A = { src: "file", fileSrc: null, lastFiles: null, enc: null, encFrames: null, nameAuto: true,
              lib: [], store: null, showing: -1, all: false, uploading: false };
  const libCache = store.get("op32.animlib", {});          // crc -> { name, thumb, t }

  /* ================================================================================================
     Preview on the mirror: the decoded file, with its own frame times
     ================================================================================================ */
  const pv = { frames: null, i: 0, timer: 0, on: false };
  function previewStop() { pv.on = false; clearTimeout(pv.timer); }
  function previewPlay(frames) {
    if (!frames || !frames.length) return;
    previewStop();
    pvStop();
    announce("mirror", "anim");
    Object.assign(pv, { frames, i: 0, on: true });
    $("#mirrorNote").textContent = "Animation preview, exactly as the panel will play it.";
    const step = () => {
      if (!pv.on) return;
      const f = pv.frames[pv.i];
      pix.set(f.rgb);
      drawMirror();
      pv.i = (pv.i + 1) % pv.frames.length;
      pv.timer = setTimeout(step, Math.max(OPA.MIN_DELAY, f.delay));
    };
    step();
  }
  window.addEventListener("op32:mirror", (e) => { if (e.detail !== "anim") previewStop(); });

  /* ================================================================================================
     The animation being made: frames from the chosen source -> colours reduced, encoded, decoded again
     ================================================================================================ */
  let encTimer = 0;
  function scheduleEncode(play) { clearTimeout(encTimer); encTimer = setTimeout(() => encodeNow(play), 220); }
  function facts(msg, err) { const el = $("#aFacts"); el.textContent = msg; el.classList.toggle("err", !!err); }
  function buttons() {
    $("#aSave").disabled = !A.enc || !A.enc.fits || A.uploading;
    $("#aPreview").disabled = !A.encFrames;
  }
  function defaultName() {
    if (A.src === "file") return A.fileSrc ? A.fileSrc.name : "";
    if (A.src === "draw") return "Drawing";
    return $("#gKind").value === "emoji" ? `${$("#gText").value.trim()} ${$("#gMotion").selectedOptions[0].textContent.toLowerCase()}`
                                         : $("#gPreset").selectedOptions[0].textContent;
  }
  function currentFrames() {
    if (A.src === "file") return fileFrames();
    if (A.src === "draw") return ed.frames.map((rgb, i) => ({ rgb, delay: ed.delays[i] }));
    return genFrames();
  }
  function encodeNow(play) {
    if (A.nameAuto) $("#aName").value = defaultName().slice(0, 40);
    let frames;
    try { frames = currentFrames(); }
    catch (e) { A.enc = A.encFrames = null; facts(e.message, true); buttons(); return; }
    if (!frames || !frames.length) { A.enc = A.encFrames = null; facts("Nothing made yet."); buttons(); return; }
    const tempo = Number($("#aTempo").value);
    try {
      A.enc = OPA.encode(frames.map((f) => ({ rgb: f.rgb, delay: f.delay / tempo })),
                         { name: $("#aName").value, maxColors: Number($("#aColors").value) });
      A.encFrames = OPA.decode(A.enc.bytes);
    } catch (e) { A.enc = A.encFrames = null; facts(e.message, true); buttons(); return; }
    const secs = A.encFrames.reduce((a, f) => a + f.delay, 0) / 1000;
    const slots = Math.ceil(A.enc.size / OPA.SLOT_SIZE);
    facts(`${A.enc.frames} ${A.enc.frames === 1 ? "frame" : "frames"}${A.enc.merged ? ` (${A.enc.merged} repeats joined)` : ""}, ` +
          `${secs.toFixed(1)} s per round, ${A.enc.colors} colours, ${kb(A.enc.size)} = ${slots} of the panel's ${OPA.SLOTS} slots` +
          (A.enc.fits ? "" : ". Too big: use fewer frames (Frames: every 2nd) or fewer colours."), !A.enc.fits);
    buttons();
    if (play) previewPlay(A.encFrames);
  }

  /* ---- source 1: files ------------------------------------------------------------------------------ */
  const SRC_BOX = 160, MAX_SRC_FRAMES = 600;
  function note(msg, err) { const el = $("#aFileNote"); el.textContent = msg; el.classList.toggle("err", !!err); }
  function toBox(img, w, h) {                               // keep at most 160 px; small pixel art stays exact
    const s = Math.min(1, SRC_BOX / Math.max(w, h, 1));
    const c = document.createElement("canvas");
    c.width = Math.max(1, Math.round(w * s)); c.height = Math.max(1, Math.round(h * s));
    const ctx = c.getContext("2d");
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(img, 0, 0, c.width, c.height);
    return c;
  }
  async function imageFrames(file) {
    if ("ImageDecoder" in window && await ImageDecoder.isTypeSupported(file.type).catch(() => false)) {
      const dec = new ImageDecoder({ data: await file.arrayBuffer(), type: file.type });
      await dec.tracks.ready;
      await dec.completed.catch(() => {});
      const n = dec.tracks.selectedTrack ? dec.tracks.selectedTrack.frameCount : 1, out = [];
      for (let i = 0; i < n && out.length < MAX_SRC_FRAMES; i++) {
        const { image } = await dec.decode({ frameIndex: i });
        let d = image.duration ? image.duration / 1000 : 100;
        if (d <= 10) d = 100;                               // as browsers play GIFs: 0 and 10 ms mean 100 ms
        out.push({ canvas: toBox(image, image.displayWidth, image.displayHeight), delay: n === 1 ? 1000 : d });
        image.close();
      }
      dec.close();
      if (out.length) return out;
    }
    const bmp = await createImageBitmap(file);
    return [{ canvas: toBox(bmp, bmp.width, bmp.height), delay: 1000 }];
  }
  async function videoFrames(file) {
    const fps = Number($("#aFps").value), v = document.createElement("video");
    v.muted = true; v.playsInline = true; v.preload = "auto";
    v.src = URL.createObjectURL(file);
    try {
      await new Promise((ok, bad) => { v.onloadeddata = ok; v.onerror = () => bad(new Error("This browser can't open that video.")); });
      const secs = Math.min(v.duration || 0, MAX_SRC_FRAMES / fps, 60);
      const n = Math.max(1, Math.floor(secs * fps)), out = [];
      for (let i = 0; i < n; i++) {
        await new Promise((ok) => { v.onseeked = ok; v.currentTime = (i + 0.5) / fps; });
        out.push({ canvas: toBox(v, v.videoWidth, v.videoHeight), delay: 1000 / fps });
        if (i % 8 === 0) note(`Reading the video… ${Math.round(i * 100 / n)}%`);
      }
      return out;
    } finally { URL.revokeObjectURL(v.src); }
  }
  async function pictureFrames(files) {
    const fps = Number($("#aFps").value), out = [];
    for (const f of files.slice(0, MAX_SRC_FRAMES)) {
      const bmp = await createImageBitmap(f);
      out.push({ canvas: toBox(bmp, bmp.width, bmp.height), delay: 1000 / fps });
    }
    return out;
  }
  async function importFiles(list) {
    const files = [...list].filter((f) => /^(image|video)\//.test(f.type))
                           .sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
    if (!files.length) { note("Those files aren't pictures or videos.", true); return; }
    A.lastFiles = files;
    note("Reading…");
    try {
      const frames = files.length > 1 ? await pictureFrames(files)
                   : files[0].type.startsWith("video/") ? await videoFrames(files[0]) : await imageFrames(files[0]);
      A.fileSrc = { frames, name: files[0].name.replace(/\.[^.]+$/, "") };
      note(`${frames.length} ${frames.length === 1 ? "frame" : "frames"} from ${files.length > 1 ? files.length + " pictures" : files[0].name}` +
           (frames.length >= MAX_SRC_FRAMES ? ` (the first ${MAX_SRC_FRAMES} only)` : "") + ".");
      $("#aToEditor").disabled = false;
      A.nameAuto = true;
      encodeNow(true);
    } catch (e) { note(e.message || "That file couldn't be read.", true); }
  }
  function fileFrames() {
    if (!A.fileSrc) return null;
    const step = Number($("#aStep").value), fit = $("#aFit").value, bg = $("#aBg").value;
    const c = canvas32(), ctx = c.getContext("2d", { willReadFrequently: true }), src = A.fileSrc.frames, out = [];
    ctx.imageSmoothingEnabled = $("#aSmooth").value === "1";
    ctx.imageSmoothingQuality = "high";
    for (let i = 0; i < src.length; i += step) {
      let delay = 0;
      for (let k = i; k < Math.min(i + step, src.length); k++) delay += src[k].delay;   // skipped frames keep their time
      const s = src[i].canvas, w = s.width, h = s.height;
      const k = fit === "cover" ? Math.max(32 / w, 32 / h) : Math.min(32 / w, 32 / h);
      const dw = fit === "stretch" ? 32 : w * k, dh = fit === "stretch" ? 32 : h * k;
      ctx.fillStyle = bg; ctx.fillRect(0, 0, 32, 32);
      ctx.drawImage(s, (32 - dw) / 2, (32 - dh) / 2, dw, dh);
      out.push({ rgb: rgbOf(ctx.getImageData(0, 0, 32, 32).data), delay });
    }
    return out;
  }

  /* ---- source 2: frame editor ------------------------------------------------------------------------- */
  const ed = { frames: [], delays: [], cur: 0, tool: "pen", last: null, painting: false };
  const edCv = $("#aEd"), edCtx = edCv.getContext("2d");
  (function edLoad() {
    const s = store.get("op32.anim.editor", null);
    if (s && Array.isArray(s.frames) && s.frames.length) {
      ed.frames = s.frames.map(fromB64).filter((f) => f.length === NPX * 3);
      ed.delays = ed.frames.map((_, i) => Math.max(OPA.MIN_DELAY, Number(s.delays && s.delays[i]) || 150));
    }
    if (!ed.frames.length) { ed.frames = [new Uint8Array(NPX * 3)]; ed.delays = [150]; }
  })();
  let edSaveTimer = 0;
  function edChanged(structure) {
    clearTimeout(edSaveTimer);
    edSaveTimer = setTimeout(() => store.set("op32.anim.editor", { frames: ed.frames.map(toB64), delays: ed.delays }), 400);
    if (structure) edStrip(); else edThumb(ed.cur);
    edDraw();
    if (A.src === "draw") scheduleEncode(false);
  }
  function edDraw() {
    const s = edCv.width / 32, f = ed.frames[ed.cur], prev = $("#aOnion").checked && ed.cur > 0 ? ed.frames[ed.cur - 1] : null;
    edCtx.fillStyle = "#07080a";
    edCtx.fillRect(0, 0, edCv.width, edCv.height);
    for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) {
      const i = (y * 32 + x) * 3;
      let r = f[i], g = f[i + 1], b = f[i + 2];
      const lit = r + g + b > 0;
      if (!lit && prev && prev[i] + prev[i + 1] + prev[i + 2] > 0) { r = prev[i] >> 2; g = prev[i + 1] >> 2; b = prev[i + 2] >> 2; }
      edCtx.fillStyle = r + g + b > 0 ? `rgb(${r},${g},${b})` : "#15171b";
      edCtx.fillRect(x * s + 1, y * s + 1, s - 2, s - 2);
    }
    $("#aDur").value = ed.delays[ed.cur];
    $("#aDurOut").value = `${ed.delays[ed.cur]} ms`;
  }
  function edThumb(i) {
    const b = $("#aStrip").children[i];
    if (!b) return;
    const ctx = b.querySelector("canvas").getContext("2d"), im = ctx.createImageData(32, 32), f = ed.frames[i];
    for (let k = 0; k < NPX; k++) { im.data[k * 4] = f[k * 3]; im.data[k * 4 + 1] = f[k * 3 + 1]; im.data[k * 4 + 2] = f[k * 3 + 2]; im.data[k * 4 + 3] = 255; }
    ctx.putImageData(im, 0, 0);
  }
  function edStrip() {
    const box = $("#aStrip");
    box.innerHTML = "";
    ed.frames.forEach((_, i) => {
      const b = document.createElement("button");
      b.className = i === ed.cur ? "lit" : "";
      b.setAttribute("role", "option");
      b.setAttribute("aria-selected", i === ed.cur ? "true" : "false");
      b.innerHTML = `<canvas width="32" height="32"></canvas><span>${i + 1}</span>`;
      b.addEventListener("click", () => { ed.cur = i; edStrip(); edDraw(); });
      box.appendChild(b);
      edThumb(i);
    });
    const lit = box.children[ed.cur];
    if (lit) lit.scrollIntoView({ block: "nearest", inline: "nearest" });
  }
  const penRgb = () => [1, 3, 5].map((i) => parseInt($("#aPen").value.substr(i, 2), 16));
  function edCell(e) {
    const r = edCv.getBoundingClientRect();
    return [Math.floor((e.clientX - r.left) / r.width * 32), Math.floor((e.clientY - r.top) / r.height * 32)];
  }
  function edSet(x, y, c) { if (x >= 0 && x < 32 && y >= 0 && y < 32) ed.frames[ed.cur].set(c, (y * 32 + x) * 3); }
  function edFill(x, y, c) {
    const f = ed.frames[ed.cur], i0 = (y * 32 + x) * 3, from = [f[i0], f[i0 + 1], f[i0 + 2]];
    if (from.every((v, k) => v === c[k])) return;
    const st = [[x, y]];
    while (st.length) {
      const [cx, cy] = st.pop();
      if (cx < 0 || cx > 31 || cy < 0 || cy > 31) continue;
      const i = (cy * 32 + cx) * 3;
      if (f[i] !== from[0] || f[i + 1] !== from[1] || f[i + 2] !== from[2]) continue;
      f.set(c, i);
      st.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
    }
  }
  function edStroke(e) {
    const [x, y] = edCell(e);
    if (x < 0 || x > 31 || y < 0 || y > 31) return;
    if (ed.tool === "pick") {
      const f = ed.frames[ed.cur], i = (y * 32 + x) * 3;
      $("#aPen").value = "#" + [f[i], f[i + 1], f[i + 2]].map((v) => v.toString(16).padStart(2, "0")).join("");
      setTool("pen");
      return;
    }
    if (ed.tool === "fill") { edFill(x, y, penRgb()); edChanged(false); return; }
    const c = ed.tool === "erase" ? [0, 0, 0] : penRgb();
    let [x0, y0] = ed.last || [x, y];
    const dx = Math.abs(x - x0), dy = -Math.abs(y - y0), sx = x0 < x ? 1 : -1, sy = y0 < y ? 1 : -1;
    let err = dx + dy;
    for (;;) {                                              // join to the previous point: fast strokes have no gaps
      edSet(x0, y0, c);
      if (x0 === x && y0 === y) break;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
    ed.last = [x, y];
    edChanged(false);
  }
  edCv.addEventListener("pointerdown", (e) => { ed.painting = true; ed.last = null; edCv.setPointerCapture(e.pointerId); edStroke(e); });
  edCv.addEventListener("pointermove", (e) => {
    if (!ed.painting || ed.tool === "fill" || ed.tool === "pick") return;
    for (const ev of e.getCoalescedEvents ? e.getCoalescedEvents() : [e]) edStroke(ev);
  });
  ["pointerup", "pointercancel"].forEach((t) => edCv.addEventListener(t, () => { ed.painting = false; ed.last = null; }));
  function setTool(t) { ed.tool = t; $$("#aTool button").forEach((b) => b.classList.toggle("lit", b.dataset.tool === t)); }
  $$("#aTool button").forEach((b) => b.addEventListener("click", () => setTool(b.dataset.tool)));
  ["#ffffff", "#ff3355", "#ff8a00", "#ffd400", "#3ddc84", "#00c2ff", "#3d5afe", "#b44dff", "#ff4fd8", "#7a4a2a"].forEach((c) => {
    const b = document.createElement("button");
    b.style.background = c; b.title = c; b.setAttribute("aria-label", "Colour " + c);
    b.addEventListener("click", () => { $("#aPen").value = c; setTool("pen"); });
    $("#aPal").appendChild(b);
  });
  $("#aOnion").addEventListener("change", edDraw);
  $("#aDur").addEventListener("input", (e) => { ed.delays[ed.cur] = Number(e.target.value); $("#aDurOut").value = `${e.target.value} ms`; edChanged(false); });
  $("#aDurAll").addEventListener("click", () => { ed.delays = ed.delays.map(() => ed.delays[ed.cur]); edChanged(false); status(`Every frame now stays ${ed.delays[ed.cur]} ms.`); });
  $("#aFrAdd").addEventListener("click", () => { ed.frames.splice(ed.cur + 1, 0, new Uint8Array(NPX * 3)); ed.delays.splice(ed.cur + 1, 0, ed.delays[ed.cur]); ed.cur++; edChanged(true); });
  $("#aFrDup").addEventListener("click", () => { ed.frames.splice(ed.cur + 1, 0, ed.frames[ed.cur].slice()); ed.delays.splice(ed.cur + 1, 0, ed.delays[ed.cur]); ed.cur++; edChanged(true); });
  $("#aFrDel").addEventListener("click", () => {
    if (ed.frames.length === 1) { ed.frames[0].fill(0); edChanged(true); return; }
    ed.frames.splice(ed.cur, 1); ed.delays.splice(ed.cur, 1); ed.cur = Math.min(ed.cur, ed.frames.length - 1); edChanged(true);
  });
  function edMove(d) {
    const j = ed.cur + d;
    if (j < 0 || j >= ed.frames.length) return;
    [ed.frames[ed.cur], ed.frames[j]] = [ed.frames[j], ed.frames[ed.cur]];
    [ed.delays[ed.cur], ed.delays[j]] = [ed.delays[j], ed.delays[ed.cur]];
    ed.cur = j; edChanged(true);
  }
  $("#aFrLeft").addEventListener("click", () => edMove(-1));
  $("#aFrRight").addEventListener("click", () => edMove(1));
  $$("[data-shift]").forEach((b) => b.addEventListener("click", () => {    // move the picture, wrapping around
    const [dx, dy] = b.dataset.shift.split(",").map(Number), f = ed.frames[ed.cur], o = f.slice();
    for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) f.set(o.subarray(((y - dy + 32) % 32 * 32 + (x - dx + 32) % 32) * 3, ((y - dy + 32) % 32 * 32 + (x - dx + 32) % 32) * 3 + 3), (y * 32 + x) * 3);
    edChanged(false);
  }));
  $("#aFlip").addEventListener("click", () => {
    const f = ed.frames[ed.cur], o = f.slice();
    for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) f.set(o.subarray((y * 32 + 31 - x) * 3, (y * 32 + 31 - x) * 3 + 3), (y * 32 + x) * 3);
    edChanged(false);
  });
  $("#aClr").addEventListener("click", () => { ed.frames[ed.cur].fill(0); edChanged(false); });
  $("#aToEditor").addEventListener("click", () => {                // touch up imported frames by hand
    const fr = fileFrames();
    if (!fr || !fr.length) return;
    const n = Math.min(fr.length, 300);
    ed.frames = fr.slice(0, n).map((f) => f.rgb);
    ed.delays = fr.slice(0, n).map((f) => Math.max(OPA.MIN_DELAY, Math.round(f.delay)));
    ed.cur = 0;
    setSource("draw");
    edChanged(true);
    status(n < fr.length ? `The first ${n} frames are in the editor.` : "The frames are in the editor.");
  });

  /* ---- source 3: generators ------------------------------------------------------------------------- */
  const FORMULAS = [
    ["Plasma", "hsv(sin(x/5 + t) + sin(y/4 - t*1.3) + sin((x + y)/6 + t*0.7), 1, 1)"],
    ["Rings", "hsv(hypot(x - 15.5, y - 15.5)/12 - t/2, 1, 1)"],
    ["Spiral", "hsv(atan2(y - 15.5, x - 15.5)/(2*PI) + hypot(x - 15.5, y - 15.5)/24 - t/2, 1, 1)"],
    ["Pulse", "hsv(0.95, 0.7, max(0, 1 - abs(hypot(x - 15.5, y - 15.5) - t*16 % 24)/3))"],
    ["Rain", "((y - t*(14 + x*7 % 5*5) + x*13) % 16 + 16) % 16 < 4 ? [40, 200, 255] : [0, 0, 18]"],
    ["Checkers", "((x >> 2) + (y >> 2) + floor(t*4)) % 2 ? hsv(t/4, 1, 1) : [0, 0, 0]"],
    ["Sunset sea", "y < 19 + sin(x/3 + t*2) ? hsv(0.03 + y/110, 0.9, 1 - y/36) : hsv(0.58, 0.8, 0.3 + 0.2*sin(x/2 - t*4 + y))"],
  ];
  FORMULAS.forEach(([n], i) => { const o = document.createElement("option"); o.value = i; o.textContent = n; $("#gPreset").appendChild(o); });
  $("#gFormula").value = FORMULAS[0][1];
  const MATH = ["sin", "cos", "tan", "abs", "sqrt", "floor", "ceil", "round", "min", "max", "atan2", "hypot", "pow", "exp", "log", "sign", "random", "PI"];
  function hsv01(h, s = 1, v = 1) {
    h = ((h % 1) + 1) % 1; s = Math.max(0, Math.min(1, s)); v = Math.max(0, Math.min(1, v));
    const i = Math.floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    const [r, g, b] = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6];
    return [r * 255, g * 255, b * 255];
  }
  function formulaFrames(secs, fps) {
    let fn;
    try { fn = new Function(...MATH, "hsv", "x", "y", "t", "i", `"use strict"; return (${$("#gFormula").value}\n);`); }
    catch (e) { throw new Error("The formula has a mistake: " + e.message); }
    const args = MATH.map((k) => Math[k]), n = Math.round(secs * fps), out = [];
    for (let f = 0; f < n; f++) {
      const rgb = new Uint8Array(NPX * 3), t = f / fps;
      for (let i = 0; i < NPX; i++) {
        let c;
        try { c = fn(...args, hsv01, i & 31, i >> 5, t, i); } catch (e) { throw new Error("The formula failed: " + e.message); }
        if (typeof c === "number") c = [c * 255, c * 255, c * 255];
        if (!Array.isArray(c)) throw new Error("The formula must give [r, g, b] or hsv(…).");
        for (let k = 0; k < 3; k++) rgb[i * 3 + k] = Math.max(0, Math.min(255, Math.round(Number(c[k]) || 0)));
      }
      out.push({ rgb, delay: 1000 / fps });
    }
    return out;
  }
  const bump = (p, c, w) => Math.exp(-(((p - c) * w) ** 2));
  function emojiFrames(secs, fps) {
    const text = $("#gText").value.trim() || "❤️", motion = $("#gMotion").value, n = Math.round(secs * fps), out = [];
    const big = document.createElement("canvas"); big.width = big.height = 128;
    const bctx = big.getContext("2d"), c = canvas32(), ctx = c.getContext("2d", { willReadFrequently: true });
    const family = '"Segoe UI Emoji", "Apple Color Emoji", "Noto Color Emoji", "Segoe UI", system-ui, sans-serif';
    let size = 104;
    for (; size > 12; size -= 4) {                          // largest size that fits 112 x 112
      bctx.font = `bold ${size}px ${family}`;
      const m = bctx.measureText(text);
      if (m.width <= 112 && m.actualBoundingBoxAscent + m.actualBoundingBoxDescent <= 112) break;
    }
    const m = bctx.measureText(text), yMid = (m.actualBoundingBoxAscent - m.actualBoundingBoxDescent) / 2;
    for (let f = 0; f < n; f++) {
      const p = f / n, tau = Math.PI * 2;
      bctx.setTransform(1, 0, 0, 1, 0, 0);
      bctx.filter = "none";
      bctx.fillStyle = $("#gBg").value; bctx.fillRect(0, 0, 128, 128);
      bctx.translate(64, 64);
      let sx = 1, sy = 1;
      switch (motion) {
      case "beat": sx = sy = 0.82 + 0.2 * (bump(p, 0.12, 14) + 0.75 * bump(p, 0.32, 14)); break;
      case "bounce": {
        const h = Math.abs(Math.sin(p * tau));             // two hops per round
        bctx.translate(0, 22 - h * 40);
        const squash = Math.max(0, 1 - h * 5) * 0.22;
        sx = 0.8 + squash; sy = 0.8 - squash;
        break; }
      case "spin": bctx.rotate(p * tau); sx = sy = 0.85; break;
      case "wobble": bctx.rotate(Math.sin(p * tau * 2) * 0.35); sx = sy = 0.85; break;
      case "float": bctx.translate(Math.sin(p * tau) * 12, Math.sin(p * tau * 2) * 8); sx = sy = 0.8; break;
      case "zoom": sx = sy = 0.35 + 0.6 * (0.5 - 0.5 * Math.cos(p * tau)); break;
      case "flash": bctx.filter = `hue-rotate(${Math.round(p * 360)}deg) saturate(1.6)`; sx = sy = 0.9; break;
      }
      bctx.scale(sx, sy);
      bctx.font = `bold ${size}px ${family}`;
      bctx.textAlign = "center"; bctx.textBaseline = "alphabetic";
      bctx.fillStyle = motion === "flash" ? "#ff3c3c" : $("#gInk").value;
      bctx.fillText(text, 0, yMid);
      ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = "high";
      ctx.drawImage(big, 0, 0, 32, 32);
      out.push({ rgb: rgbOf(ctx.getImageData(0, 0, 32, 32).data), delay: 1000 / fps });
    }
    return out;
  }
  function genFrames() {
    const secs = Number($("#gSecs").value), fps = Number($("#gFps").value);
    return $("#gKind").value === "emoji" ? emojiFrames(secs, fps) : formulaFrames(secs, fps);
  }
  function genKind() { const k = $("#gKind").value; $$("[data-gen]").forEach((el) => { el.hidden = el.dataset.gen !== k; }); }
  $("#gKind").addEventListener("change", () => { genKind(); A.nameAuto = true; scheduleEncode(true); });
  $("#gPreset").addEventListener("change", (e) => { $("#gFormula").value = FORMULAS[Number(e.target.value)][1]; A.nameAuto = true; scheduleEncode(true); });
  for (const id of ["#gText", "#gMotion", "#gBg", "#gInk", "#gSecs", "#gFps"]) $(id).addEventListener(id === "#gText" ? "input" : "change", () => { A.nameAuto = true; scheduleEncode(true); });
  $("#gFormula").addEventListener("input", () => scheduleEncode(true));
  genKind();

  /* ---- switching source, shared settings ------------------------------------------------------------- */
  function setSource(src) {
    A.src = src;
    $$("#aSrcSeg button").forEach((b) => b.classList.toggle("lit", b.dataset.src === src));
    $("#aSrcFile").hidden = src !== "file"; $("#aSrcDraw").hidden = src !== "draw"; $("#aSrcGen").hidden = src !== "gen";
    if (src === "draw") { edStrip(); edDraw(); }
    A.nameAuto = true;
    scheduleEncode(src !== "draw");
  }
  $$("#aSrcSeg button").forEach((b) => b.addEventListener("click", () => setSource(b.dataset.src)));
  const aDrop = $("#aDrop");
  $("#aFile").addEventListener("change", (e) => { if (e.target.files.length) importFiles(e.target.files); e.target.value = ""; });
  aDrop.addEventListener("dragover", (e) => { e.preventDefault(); aDrop.classList.add("over"); });
  aDrop.addEventListener("dragleave", () => aDrop.classList.remove("over"));
  aDrop.addEventListener("drop", (e) => { e.preventDefault(); aDrop.classList.remove("over"); importFiles(e.dataTransfer.files); });
  for (const id of ["#aFit", "#aSmooth", "#aStep", "#aBg"]) $(id).addEventListener("change", () => scheduleEncode(true));
  $("#aFps").addEventListener("change", () => {                // only videos and picture series depend on it
    if (A.lastFiles && (A.lastFiles.length > 1 || A.lastFiles[0].type.startsWith("video/"))) importFiles(A.lastFiles);
  });
  $("#aColors").addEventListener("change", () => scheduleEncode(true));
  $("#aTempo").addEventListener("change", () => scheduleEncode(true));
  $("#aName").addEventListener("input", () => { A.nameAuto = !$("#aName").value; scheduleEncode(false); });
  $("#aPreview").addEventListener("click", () => previewPlay(A.encFrames));

  /* ================================================================================================
     On the panel: list, play, delete, upload
     ================================================================================================ */
  async function animList() {
    const r = await send(CMD.LIST, [], 4000);
    if (r[4] !== 1) throw new Error(REASONS[4]);
    return { slots: r[5], slotKb: r[6], valid: rd32(r, 7), used: rd32(r, 11), uploading: !!(r[15] & 1) };
  }
  async function animInfo(slot) {
    const r = await send(CMD.INFO, [slot, 0], 4000, (v) => v[5] === slot && v[6] === 0);
    return r[4] !== 1 ? null : { slot, frames: r[7] | (r[8] << 8), size: r[9] | (r[10] << 8) | (r[11] << 16), crc: rd32(r, 12) };
  }
  async function animName(slot) {
    const bytes = [];
    for (let part = 1; part <= 3; part++) {
      const r = await send(CMD.INFO, [slot, part], 4000, (v) => v[5] === slot && v[6] === part);
      if (r[4] !== 1) break;
      bytes.push(...r.slice(7, 16));
    }
    const z = bytes.indexOf(0);
    return new TextDecoder().decode(new Uint8Array(z < 0 ? bytes : bytes.slice(0, z)));
  }
  function remember(crc, name, thumb) {
    libCache[crc] = { name, thumb, t: Date.now() };
    const keys = Object.keys(libCache);
    if (keys.length > 60) keys.sort((a, b) => libCache[a].t - libCache[b].t).slice(0, keys.length - 60).forEach((k) => delete libCache[k]);
    store.set("op32.animlib", libCache);
  }
  let libBusy = false;
  async function refreshLibrary() {
    if (!ready()) { renderLib(); return; }
    if (libBusy) return;
    libBusy = true;
    try {
      const l = await animList(), items = [];
      for (let s = 0; s < l.slots; s++) {
        if (!((l.valid >>> s) & 1)) continue;
        const inf = await animInfo(s);
        if (!inf) continue;
        const c = libCache[inf.crc];
        inf.name = c ? c.name : await animName(s);
        inf.thumb = c ? c.thumb : null;
        items.push(inf);
      }
      A.store = l; A.lib = items;
      renderLib();
    } catch (e) { status(e.message, true); }
    finally { libBusy = false; }
  }
  function renderLib() {
    const box = $("#aLib");
    box.innerHTML = "";
    if (!chWrite) { box.innerHTML = '<span class="empty-note">Connect the panel to see its animations.</span>'; $("#aStore").textContent = ""; return; }
    if (!ready()) { box.innerHTML = `<span class="empty-note">Animations need openpanel32 99.${FW_MIN} or newer on the panel.</span>`; $("#aStore").textContent = ""; return; }
    if (!A.lib.length) box.innerHTML = '<span class="empty-note">No animations on the panel yet. Make one below.</span>';
    for (const it of A.lib) {
      const t = document.createElement("div");
      t.className = "atile" + (it.slot === A.showing ? " lit" : "");
      t.tabIndex = 0;
      t.setAttribute("role", "button");
      t.title = "Play this on the panel";
      t.innerHTML = (it.thumb ? `<img alt="">` : `<span class="film">no picture</span>`) +
                    `<span class="nm"></span><span class="meta">${it.frames} ${it.frames === 1 ? "frame" : "frames"}, ${kb(it.size)}</span>` +
                    `<button class="del" title="Delete from the panel">Delete</button>`;
      if (it.thumb) t.querySelector("img").src = it.thumb;
      t.querySelector(".nm").textContent = it.name || `Slot ${it.slot + 1}`;
      const play = () => act(`Playing ${it.name || "the animation"}.`, async () => {
        const r = await send(CMD.PLAY, [it.slot, Number($("#aSpeed").value)]);
        if (r[4] !== 1) throw new Error("The panel couldn't play it. Refresh and try again.");
      });
      t.addEventListener("click", (e) => { if (!e.target.classList.contains("del")) play(); });
      t.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); play(); } });
      const del = t.querySelector(".del");
      let armed = 0;
      del.addEventListener("click", () => {
        if (!armed) { del.textContent = "Sure?"; del.classList.add("armed"); armed = setTimeout(() => { armed = 0; del.textContent = "Delete"; del.classList.remove("armed"); }, 4000); return; }
        clearTimeout(armed);
        act(`Deleted ${it.name || "the animation"}.`, async () => {
          const r = await send(CMD.DELETE, [it.slot], 8000);
          if (r[4] !== 1) throw new Error("The panel couldn't delete it.");
          await refreshLibrary();
        });
      });
      box.appendChild(t);
    }
    const l = A.store;
    if (l) {
      let n = 0;
      for (let s = 0; s < l.slots; s++) n += (l.used >>> s) & 1;
      $("#aStore").textContent = `${n} of ${l.slots} slots used`;
    }
  }
  function speedText() { $("#aSpeedOut").value = `${(Number($("#aSpeed").value) / 10).toFixed(1).replace(/\.0$/, "")}×`; }
  let speedTimer = 0;
  $("#aSpeed").addEventListener("input", () => {
    speedText();
    clearTimeout(speedTimer);
    speedTimer = setTimeout(() => { if (ready()) act(`Animation speed ${$("#aSpeedOut").value}.`, () => send(CMD.PLAY, [KEEP, Number($("#aSpeed").value)])); }, 200);
  });
  speedText();
  $("#aPlayAll").addEventListener("click", () => {
    if (!needPanel()) return;
    act("Playing all animations in turn.", async () => {
      const r = await send(CMD.PLAY, [ALL, Number($("#aSpeed").value)]);
      if (r[4] !== 1) throw new Error("There are no animations on the panel yet.");
    });
  });

  async function uploadOnce(bytes, slot) {
    const prog = $("#aProg");
    prog.hidden = false; prog.max = bytes.length; prog.value = 0;
    const b = await send(CMD.BEGIN, [slot, ...u32(bytes.length), 1], 15000);
    if (b[4] !== 1) throw new Error(REASONS[b[5]] || "The panel refused the upload.");
    for (let off = 0; off < bytes.length; off += OPA.PIECE) {
      const piece = bytes.subarray(off, off + OPA.PIECE), payload = new Uint8Array(4 + piece.length);
      payload.set(u32(off)); payload.set(piece, 4);
      const r = await send(CMD.DATA, payload, 20000, (v) => v.length >= 10 && rd32(v, 5) === off);
      if (r[4] === 0) { const e = new Error(REASONS[r[9]] || "The panel stopped the upload."); e.reason = r[9]; throw e; }
      prog.value = off + piece.length;
      status(`Saving on the panel… ${Math.round(prog.value * 100 / bytes.length)}%`);
    }
  }
  $("#aSave").addEventListener("click", async () => {
    if (!needPanel() || !A.enc || !A.enc.fits || A.uploading) return;
    const enc = A.enc, bytes = enc.bytes, name = $("#aName").value.trim() || defaultName();
    A.uploading = true; buttons();
    try {
      const l = await animList();
      const need = Math.ceil(bytes.length / (l.slotKb * 1024));
      let slot = -1;
      for (let s = 0; s + need <= l.slots && slot < 0; s++) {
        let free = true;
        for (let k = s; k < s + need; k++) if ((l.used >>> k) & 1) free = false;
        if (free) slot = s;
      }
      if (slot < 0) throw new Error(`Not enough room on the panel: this one needs ${need} free ${need === 1 ? "slot" : "slots in a row"}. Delete an animation first.`);
      const t0 = performance.now();
      try { await uploadOnce(bytes, slot); }
      catch (e) { if (e.reason !== 10) throw e; status("Checking failed once, sending again…"); await uploadOnce(bytes, slot); }
      remember(OPA.info(bytes).crc, name, thumbOf(A.encFrames[0].rgb));
      const d = new Date();                                 // flash writes pause the panel's clock a little
      await send(0x8001, [d.getHours(), d.getMinutes(), d.getSeconds(), 0]).catch(() => {});
      status(`Saved "${name}" on the panel in ${((performance.now() - t0) / 1000).toFixed(1)} s. It plays now.`);
      await refreshLibrary();
    } catch (e) { status(e.message, true); }
    finally { A.uploading = false; buttons(); $("#aProg").hidden = true; }
  });

  /* ---- panel events ---------------------------------------------------------------------------------- */
  window.addEventListener("op32:connected", (e) => { A.lib = []; A.store = null; if (e.detail.custom && !e.detail.rescue && e.detail.fwMinor >= FW_MIN) refreshLibrary(); else renderLib(); });
  window.addEventListener("op32:disconnected", () => { A.lib = []; A.store = null; renderLib(); });
  window.addEventListener("op32:status", (e) => {
    const r = e.detail, mode = r[5];
    A.showing = mode === 9 ? r[6] : -1;
    A.all = mode === 9 && !!(r[12] & 16);
    if (mode === 9) {
      const it = A.lib.find((x) => x.slot === r[6]);
      $("#nowShowing").textContent = r[6] === 255 ? "Animations (none stored)" :
        `Animation: ${it ? it.name || "slot " + (it.slot + 1) : "slot " + (r[6] + 1)}${A.all ? ", all in turn" : ""}`;
    }
    $$("#aLib .atile").forEach((t, i) => t.classList.toggle("lit", A.lib[i] && A.lib[i].slot === A.showing));
  });
  setSource("file");
})();

// Frames from files: an animated GIF / WebP / PNG (ImageDecoder), a video (seeking a <video>), or several
// pictures (one per frame). Each source frame is kept at most 160 px; fitting to the panel's size happens later, so
// the fit / scaling / background choices can change without reading the file again.
const BOX = 160;
export const MAX_SOURCE_FRAMES = 600;

function toBox(img, w, h) {                           // small pixel art stays exact
  const s = Math.min(1, BOX / Math.max(w, h, 1)), c = document.createElement("canvas");
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
    for (let i = 0; i < n && out.length < MAX_SOURCE_FRAMES; i++) {
      const { image } = await dec.decode({ frameIndex: i });
      let d = image.duration ? image.duration / 1000 : 100;
      if (d <= 10) d = 100;                           // as browsers play GIFs: 0 and 10 ms mean 100 ms
      out.push({ canvas: toBox(image, image.displayWidth, image.displayHeight), delay: n === 1 ? 1000 : d });
      image.close();
    }
    dec.close();
    if (out.length) return out;
  }
  const bmp = await createImageBitmap(file);
  return [{ canvas: toBox(bmp, bmp.width, bmp.height), delay: 1000 }];
}

async function videoFrames(file, fps, progress) {
  const v = document.createElement("video");
  v.muted = true; v.playsInline = true; v.preload = "auto";
  v.src = URL.createObjectURL(file);
  try {
    await new Promise((ok, bad) => { v.onloadeddata = ok; v.onerror = () => bad(new Error("This browser can't open that video.")); });
    const secs = Math.min(v.duration || 0, MAX_SOURCE_FRAMES / fps, 60);
    const n = Math.max(1, Math.floor(secs * fps)), out = [];
    for (let i = 0; i < n; i++) {
      await new Promise((ok) => { v.onseeked = ok; v.currentTime = (i + 0.5) / fps; });
      out.push({ canvas: toBox(v, v.videoWidth, v.videoHeight), delay: 1000 / fps });
      if (i % 8 === 0) progress(i / n);
    }
    return out;
  } finally { URL.revokeObjectURL(v.src); }
}

async function pictureFrames(files, fps) {
  const out = [];
  for (const f of files.slice(0, MAX_SOURCE_FRAMES)) {
    const bmp = await createImageBitmap(f);
    out.push({ canvas: toBox(bmp, bmp.width, bmp.height), delay: 1000 / fps });
  }
  return out;
}

/* files -> { frames: [{ canvas, delay }], name, kind } ; progress(0..1) for videos */
export async function readFiles(list, fps, progress = () => {}) {
  const files = [...list].filter((f) => /^(image|video)\//.test(f.type))
                         .sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
  if (!files.length) throw new Error("Those files aren't pictures or videos.");
  const kind = files.length > 1 ? "pictures" : files[0].type.startsWith("video/") ? "video" : "image";
  const frames = kind === "pictures" ? await pictureFrames(files, fps) : kind === "video" ? await videoFrames(files[0], fps, progress) : await imageFrames(files[0]);
  return { frames, kind, files, name: files[0].name.replace(/\.[^.]+$/, "") };
}

/* source frames -> w x h RGB frames (the panel's size); every `step`-th frame, the skipped ones' time added */
export function fitFrames(src, { step = 1, fit = "cover", smooth = true, bg = "#000000", w = 32, h = 32 }) {
  const c = document.createElement("canvas");
  c.width = w; c.height = h;
  const ctx = c.getContext("2d", { willReadFrequently: true }), out = [];
  ctx.imageSmoothingEnabled = smooth;
  ctx.imageSmoothingQuality = "high";
  for (let i = 0; i < src.length; i += step) {
    let delay = 0;
    for (let k = i; k < Math.min(i + step, src.length); k++) delay += src[k].delay;
    const s = src[i].canvas, sw = s.width, sh = s.height;
    const k = fit === "cover" ? Math.max(w / sw, h / sh) : Math.min(w / sw, h / sh);
    const dw = fit === "stretch" ? w : sw * k, dh = fit === "stretch" ? h : sh * k;
    ctx.fillStyle = bg; ctx.fillRect(0, 0, w, h);
    ctx.drawImage(s, (w - dw) / 2, (h - dh) / 2, dw, dh);
    out.push({ rgb: rgbOf(ctx.getImageData(0, 0, w, h).data), delay });
  }
  return out;
}

export function rgbOf(rgba) {
  const n = rgba.length / 4, o = new Uint8Array(n * 3);
  for (let i = 0; i < n; i++) { o[i * 3] = rgba[i * 4]; o[i * 3 + 1] = rgba[i * 4 + 1]; o[i * 3 + 2] = rgba[i * 4 + 2]; }
  return o;
}
export function thumbOf(rgb, w = 32, h = 32) {
  const c = document.createElement("canvas");
  c.width = w; c.height = h;
  const ctx = c.getContext("2d"), im = ctx.createImageData(w, h);
  for (let i = 0; i < w * h; i++) { im.data[i * 4] = rgb[i * 3]; im.data[i * 4 + 1] = rgb[i * 3 + 1]; im.data[i * 4 + 2] = rgb[i * 3 + 2]; im.data[i * 4 + 3] = 255; }
  ctx.putImageData(im, 0, 0);
  return c.toDataURL("image/png");
}

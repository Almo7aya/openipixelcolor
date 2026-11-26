// Test animations for "sim anim": encoded by the web UI's encoder (webui/anim.js), plus what its decoder
// says the panel must show. The simulator plays the files through the firmware's player and compares.
//   node tools/sim/anim_vectors.js <out-dir>
// Writes <name>.opa (the file), <name>.rgb (every frame, 32*32*3 each) and <name>.del (u16 delays).
"use strict";
const fs = require("fs"), path = require("path");
const OPA = require("../../webui/anim.js");

const out = process.argv[2] || ".";
fs.mkdirSync(out, { recursive: true });

function frame(fn) {
  const rgb = new Uint8Array(OPA.NPX * 3);
  for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) rgb.set(fn(x, y).map((v) => Math.max(0, Math.min(255, Math.round(v)))), (y * 32 + x) * 3);
  return rgb;
}

const sets = {
  // a square bouncing over a two-colour background, a repeated frame (merged), few colours, one slot
  bounce: Array.from({ length: 14 }, (_, f) => {
    const k = f === 7 ? 6 : f, bx = 4 + Math.abs(((k * 3) % 40) - 20), by = 6 + Math.abs(((k * 5) % 36) - 18);
    return { delay: [50, 100, 75, 150][f % 4], rgb: frame((x, y) => (x >= bx && x < bx + 8 && y >= by && y < by + 8) ? [255, 200, 40]
                                                         : y < 16 ? [10, 20, 60] : [0, 40, 10]) };
  }),
  // plasma: thousands of colours (median cut to 256), every pixel changes: several slots
  plasma: Array.from({ length: 70 }, (_, f) => ({ delay: 50, rgb: frame((x, y) => {
    const v = Math.sin(x / 4 + f / 5) + Math.sin(y / 5 - f / 7) + Math.sin((x + y + f) / 6);
    return [128 + 127 * Math.sin(v * 2), 128 + 127 * Math.sin(v * 2 + 2.1), 128 + 127 * Math.sin(v * 2 + 4.2)];
  }) })),
  // one still picture
  still: [{ delay: 1000, rgb: frame((x, y) => [x * 8, y * 8, ((x ^ y) & 1) * 255]) }],
};

for (const [name, frames] of Object.entries(sets)) {
  const enc = OPA.encode(frames, { name: name + " ✓ test" });
  const dec = OPA.decode(enc.bytes);
  fs.writeFileSync(path.join(out, name + ".opa"), enc.bytes);
  fs.writeFileSync(path.join(out, name + ".rgb"), Buffer.concat(dec.map((d) => Buffer.from(d.rgb))));
  const del = Buffer.alloc(dec.length * 2);
  dec.forEach((d, i) => del.writeUInt16LE(d.delay, i * 2));
  fs.writeFileSync(path.join(out, name + ".del"), del);
  let note = "";
  if (name === "bounce") {                          // 3 colours: must come back exactly; frame 7 repeats frame 6
    const src = frames.filter((_, i) => i !== 7);
    const ok = dec.length === 13 && dec[6].delay === frames[6].delay + frames[7].delay &&
               src.every((f, i) => Buffer.compare(Buffer.from(f.rgb), Buffer.from(dec[i].rgb)) === 0);
    note = ok ? ", decodes exactly" : ", DECODE MISMATCH";
    if (!ok) process.exitCode = 1;
  }
  console.log(`${name}: ${frames.length} frames -> ${enc.frames} stored (${enc.merged} merged), ${enc.colors} colours, ${enc.size} bytes${note}`);
}

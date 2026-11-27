// Your picture (or GIF, its own animation keeps running) in 3D and 2D: on a turning card, a cube, a coin,
// a ball, a carousel; or with glitches, ripples, a kaleidoscope, flying pixels, manga speed lines, an old TV...
// The picture comes in as frames of RGB (fitFrames in import.js, at a few times the panel's size). No DOM.
import { sceneFrames, kit } from "./scenes3d.js?v=d0fb26c";

const { sin, cos, abs, sqrt, floor, atan2, PI, exp, max, min, round } = Math;
const TAU = 2 * PI;
const fract = (v) => v - floor(v);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const hash = (i) => { let h = Math.imul(i ^ 0x9e3779b9, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };

export const PIC_EFFECTS = [
  ["card", "Turning card"], ["cube", "Cube"], ["coin", "Coin"], ["ball", "Ball"], ["tilt", "Floating tilt"], ["carousel", "Carousel"],
  ["glitch", "Glitch"], ["ripple", "Ripples"], ["kaleido", "Kaleidoscope"], ["assemble", "Pixels fly in"], ["speed", "Speed lines"],
  ["tv", "Old TV"], ["shake", "Shake"], ["spin", "Spin and zoom"], ["hearts", "Hearts"],
];

/* the picture at time ms, (u, v) in 0..1 from the top left; null outside. Bilinear. */
function sampler(tex) {
  const total = tex.frames.reduce((a, f) => a + (f.delay || 100), 0) || 1;
  const at = (ms) => {
    let t = ((ms % total) + total) % total;
    for (const f of tex.frames) { t -= f.delay || 100; if (t < 0) return f.rgb; }
    return tex.frames[0].rgb;
  };
  return (ms, u, v, rgb = at(ms)) => {
    if (u < 0 || v < 0 || u >= 1 || v >= 1) return null;
    const x = u * tex.w - 0.5, y = v * tex.h - 0.5, x0 = clamp(floor(x), 0, tex.w - 1), y0 = clamp(floor(y), 0, tex.h - 1);
    const x1 = min(x0 + 1, tex.w - 1), y1 = min(y0 + 1, tex.h - 1), fx = clamp(x - x0, 0, 1), fy = clamp(y - y0, 0, 1);
    const o00 = (y0 * tex.w + x0) * 3, o10 = (y0 * tex.w + x1) * 3, o01 = (y1 * tex.w + x0) * 3, o11 = (y1 * tex.w + x1) * 3, c = [0, 0, 0];
    for (let k = 0; k < 3; k++) c[k] = (rgb[o00 + k] * (1 - fx) + rgb[o10 + k] * fx) * (1 - fy) + (rgb[o01 + k] * (1 - fx) + rgb[o11 + k] * fx) * fy;
    return c;
  };
}
const lin = (c) => [(c[0] / 255) ** 2, (c[1] / 255) ** 2, (c[2] / 255) ** 2];   /* for the 3D renderer */

/* ---- 3D: scenes whose surfaces show the picture ---- */
function scenes(samp) {
  const { rotation, T, sdBox, sdCyl, sdSphere, S } = kit;
  const pic = (self, u, v) => { const c = samp(self.ms, u, v); return c ? lin(c) : [0, 0, 0]; };
  return {
    card: {
      cam: [0, 0.1, 2.2], look: [0, 0, 0], light: [-0.4, 0.5, 0.8],
      setup(p, secs) { this.ms = p * secs * 1000; this.m = rotation(0.15 * sin(TAU * p), TAU * p, 0); },
      map(x, y, z) { const [X, Y, Z] = T(this.m, x, y, z); return sdBox(X, Y, Z, 0.75, 0.75, 0.025, 0.02); },
      paint(x, y, z, nx, ny, nz) {
        const [X, Y] = T(this.m, x, y, z), [, , NZ] = T(this.m, nx, ny, nz);
        S.shine = 0.4;
        if (abs(NZ) < 0.5) return [0.6, 0.6, 0.65];                     /* the edge */
        return pic(this, NZ > 0 ? (X + 0.75) / 1.5 : (0.75 - X) / 1.5, (0.75 - Y) / 1.5);   /* the back reads the right way round too */
      },
    },
    cube: {
      cam: [0, 0.8, 2.2], look: [0, 0, 0],
      setup(p, secs) { this.ms = p * secs * 1000; this.m = rotation(0.5 + 0.4 * sin(TAU * p), TAU * p, 0.25 * sin(TAU * p * 2)); },
      map(x, y, z) { const [X, Y, Z] = T(this.m, x, y, z); return sdBox(X, Y, Z, 0.55, 0.55, 0.55, 0.05); },
      paint(x, y, z, nx, ny, nz) {
        const [X, Y, Z] = T(this.m, x, y, z), [NX, NY, NZ] = T(this.m, nx, ny, nz);
        S.shine = 0.3;
        const ax = abs(NX), ay = abs(NY), az = abs(NZ);
        const [u, v] = ax > ay && ax > az ? [NX > 0 ? -Z : Z, -Y] : ay > az ? [X, NY > 0 ? Z : -Z] : [NZ > 0 ? X : -X, -Y];
        return pic(this, (u + 0.55) / 1.1, (v + 0.55) / 1.1);
      },
    },
    coin: {
      cam: [0, 0.15, 2.2], look: [0, 0, 0], light: [-0.5, 0.6, 0.7],
      setup(p, secs) { this.ms = p * secs * 1000; this.m = rotation(PI / 2, TAU * p, 0); },
      map(x, y, z) { const [X, Y, Z] = T(this.m, x, y, z); return sdCyl(X, Y, Z, 0.82, 0.05, 0.02); },
      paint(x, y, z, nx, ny, nz) {
        const [X, , Z] = T(this.m, x, y, z), [, NY] = T(this.m, nx, ny, nz);
        S.shine = 0.8;
        const r = sqrt(X * X + Z * Z);
        if (abs(NY) < 0.6 || r > 0.74) return [0.95, 0.7, 0.2];          /* gold rim */
        return pic(this, NY > 0 ? (X + 0.74) / 1.48 : (0.74 - X) / 1.48, (Z + 0.74) / 1.48);
      },
    },
    ball: {
      cam: [0, 0.2, 2.4], look: [0, 0, 0],
      setup(p, secs) { this.ms = p * secs * 1000; this.m = rotation(0.2, TAU * p, 0); },
      map(x, y, z) { return sdSphere(x, y, z, 0.85); },
      paint(x, y, z, nx, ny, nz) {
        const [X, Y, Z] = T(this.m, nx, ny, nz);
        S.shine = 0.6;
        return pic(this, fract(atan2(X, Z) / TAU * 2 + 0.5), 0.5 - Math.asin(clamp(Y, -1, 1)) / PI);   /* twice round */
      },
    },
    tilt: {
      cam: [0, 0, 2.3], look: [0, 0, 0], light: [-0.3, 0.6, 0.8], shadows: true,
      setup(p, secs) { this.ms = p * secs * 1000; this.p = p; this.y = 0.06 * sin(TAU * p * 2); this.m = rotation(0.3 * sin(TAU * p), 0.35 * cos(TAU * p), 0); },
      map(x, y, z) { const [X, Y, Z] = T(this.m, x, y - this.y, z); return min(sdBox(X, Y, Z, 0.72, 0.72, 0.02, 0.01), z + 0.5); },
      paint(x, y, z, nx, ny, nz) {
        if (z < -0.48) { S.shine = 0; return [0.05, 0.05, 0.1]; }       /* the wall behind, for the shadow */
        const [X, Y] = T(this.m, x, y - this.y, z);
        S.shine = 0.9;
        const sheen = fract((X - Y) * 0.6 - this.p * 2) < 0.05;
        const c = pic(this, (X + 0.72) / 1.44, (0.72 - Y) / 1.44);
        return sheen ? [min(1, c[0] + 0.4), min(1, c[1] + 0.4), min(1, c[2] + 0.4)] : c;
      },
    },
    carousel: {
      cam: [0, 0.55, 2.6], look: [0, 0, 0],
      setup(p, secs) { this.ms = p * secs * 1000; this.a = TAU * p / 4; },             /* a quarter turn: seamless */
      map(x, y, z) {
        let d = 9;
        for (let k = 0; k < 4; k++) {
          const a = this.a + k * PI / 2, c = cos(a), s = sin(a), px = c * x - s * z, pz = s * x + c * z - 0.75;
          d = min(d, sdBox(px, y, pz, 0.42, 0.42, 0.015, 0.01));
        }
        return d;
      },
      paint(x, y, z) {
        S.shine = 0.4;
        for (let k = 0; k < 4; k++) {
          const a = this.a + k * PI / 2, c = cos(a), s = sin(a), px = c * x - s * z, pz = s * x + c * z - 0.75;
          if (abs(pz) < 0.03 && abs(px) < 0.43 && abs(y) < 0.43) return pic(this, (px + 0.42) / 0.84, (0.42 - y) / 0.84);
        }
        return [0.5, 0.5, 0.5];
      },
    },
  };
}

/* ---- 2D: a colour function of (u, v) in 0..1 from the top left, for each frame ---- */
function flat(samp, kind, ms, p, f) {
  const bgc = [0, 0, 0], pick = (u, v, ms2 = ms) => samp(ms2, u, v) || bgc;
  switch (kind) {
  case "glitch": {
    const burst = fract(p * 3) < 0.25, sh = (y) => (burst && hash(floor(y * 9) + f * 31) > 0.6 ? (hash(floor(y * 9) * 7 + f) - 0.5) * 0.25 : 0);
    return (u, v) => {
      const d = burst ? 0.04 : 0.012, o = sh(v);
      const r = pick(u + o + d, v)[0], g = pick(u + o, v)[1], b = pick(u + o - d, v)[2];
      const n = burst && hash(floor(u * 32) + floor(v * 32) * 32 + f * 1024) > 0.96 ? 120 : 0;
      return [r + n, g + n, b + n];
    };
  }
  case "ripple": return (u, v) => {
    const dx = u - 0.5, dy = v - 0.5, r = sqrt(dx * dx + dy * dy), w = sin(r * 40 - p * TAU * 3) * 0.012 * (1 - r);
    const c = pick(u + dx / (r + 1e-6) * w, v + dy / (r + 1e-6) * w), k = 1 + sin(r * 40 - p * TAU * 3) * 0.12;
    return [c[0] * k, c[1] * k, c[2] * k];
  };
  case "kaleido": return (u, v) => {
    const dx = u - 0.5, dy = v - 0.5, r = sqrt(dx * dx + dy * dy);
    let a = atan2(dy, dx) + p * TAU;
    const seg = TAU / 6;
    a = abs(((a % seg) + seg) % seg - seg / 2);
    return pick(0.5 + r * cos(a) * 1.3, 0.35 + r * sin(a) * 1.3);
  };
  case "assemble": {                                                   /* pixels fly in, hold, fly out */
    const k = p < 0.4 ? 1 - kit.smoothstep(0, 0.4, p) : p < 0.75 ? 0 : kit.smoothstep(0.75, 1, p);
    return (u, v) => {
      const ix = floor(u * 32), iy = floor(v * 32), h1 = hash(ix + iy * 32), h2 = hash(ix * 31 + iy * 7 + 5);
      if (k > 0.98) return [0, 0, 0];
      const su = u + (h1 - 0.5) * 2.2 * k, sv = v + (h2 - 0.5) * 2.2 * k + k * k * 0.4;
      return su < 0 || su > 1 || sv < 0 || sv > 1 ? [0, 0, 0] : pick(u, v).map((c) => c * (1 - k * 0.6));
    };
  }
  case "speed": return (u, v) => {                                   /* manga focus lines round the picture */
    const dx = u - 0.5, dy = v - 0.5, r = sqrt(dx * dx + dy * dy), z = 1 + 0.05 * sin(p * TAU * 4);
    const c = pick(0.5 + dx / z, 0.5 + dy / z);
    if (r < 0.36) return c;
    const a = atan2(dy, dx), line = hash(floor((a / TAU + 0.5) * 90) + floor(p * 8) * 97) > 0.55;
    return line ? [0, 0, 0] : r > 0.42 ? [255, 255, 255] : c;
  };
  case "tv": return (u, v) => {
    const roll = fract(p * 2), d = abs(v - roll), c = pick(u + (d < 0.05 ? 0.02 : 0), v);
    const scan = (floor(v * 32) & 1) ? 0.72 : 1, vig = 1 - ((u - 0.5) ** 2 + (v - 0.5) ** 2) * 1.6, glow = d < 0.08 ? 1.25 : 1;
    const k = scan * vig * glow, n = hash(floor(u * 32) + floor(v * 32) * 32 + f * 1024) * 30;
    return [c[0] * k * 0.85 + n, c[1] * k + n, c[2] * k * 0.8 + n];
  };
  case "shake": {
    const big = fract(p * 2) < 0.5, ox = (hash(f * 3) - 0.5) * (big ? 0.12 : 0.03), oy = (hash(f * 3 + 1) - 0.5) * (big ? 0.12 : 0.03);
    return (u, v) => pick(u + ox, v + oy);
  }
  case "spin": return (u, v) => {
    const dx = u - 0.5, dy = v - 0.5, r = sqrt(dx * dx + dy * dy);
    if (r > 0.48) return [0, 0, 0];
    const a = -p * TAU, z = 0.8 + 0.25 * sin(p * TAU * 2), c = cos(a), s = sin(a);
    return pick(0.5 + (dx * c - dy * s) * z, 0.5 + (dx * s + dy * c) * z);
  };
  case "hearts": {
    const H = [];
    for (let i = 0; i < 7; i++) { const k = 1 + (i % 2); H.push([hash(i) * 0.9 + 0.05, 1.15 - fract(p * k + hash(i + 50)) * 1.3, 0.05 + hash(i + 9) * 0.04]); }
    return (u, v) => {
      for (const [hx, hy, s] of H) {                                 /* a heart: two bumps and a point */
        const x = (u - hx) / s, y = (v - hy) / s, yy = -y + 0.2;
        if ((x * x + yy * yy - 1) ** 3 - x * x * yy ** 3 < 0) return [255, 70, 140];
      }
      return pick(u, v);
    };
  }
  default: return (u, v) => pick(u, v);
  }
}

function drawFlat(fn, w, h) {
  const out = new Uint8Array(w * h * 3), ss = 3;
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      let r = 0, g = 0, b = 0;
      for (let j = 0; j < ss; j++)
        for (let i = 0; i < ss; i++) { const c = fn((x + (i + 0.5) / ss) / w, (y + (j + 0.5) / ss) / h); r += c[0]; g += c[1]; b += c[2]; }
      const o = (y * w + x) * 3, n = ss * ss;
      out[o] = clamp(round(r / n), 0, 255); out[o + 1] = clamp(round(g / n), 0, 255); out[o + 2] = clamp(round(b / n), 0, 255);
    }
  return out;
}

/* tex: { frames: [{ rgb, delay }], w, h } -> [{ rgb, delay }] */
export async function pictureFrames({ tex, kind = "card", bg = "stars", secs = 3, fps = 15, w = 32, h = 32, onFrame }) {
  if (!tex || !tex.frames || !tex.frames.length) throw new Error("Choose a picture first.");
  const samp = sampler(tex), sc = scenes(samp)[kind];
  if (sc) {
    const def = { ...sc, setup(p) { sc.setup.call(this, p, secs); } };
    return sceneFrames({ def, bg, secs, fps, w, h, ss: 3, onFrame });
  }
  const n = Math.max(1, Math.round(secs * fps)), out = [];
  for (let f = 0; f < n; f++) {
    const p = f / n;
    out.push({ rgb: drawFlat(flat(samp, kind, p * secs * 1000, p, f), w, h), delay: 1000 / fps });
    if (onFrame) onFrame(f + 1, n);
    if (f % 3 === 2) await new Promise((r) => setTimeout(r, 0));
  }
  return out;
}

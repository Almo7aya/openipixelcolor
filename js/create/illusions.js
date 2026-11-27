// Optical illusions for the animation maker. Most are drawn here, LED by LED (3 x 3 samples each); the
// Penrose triangle is a 3D scene (scenes3d.js). Every one loops. No DOM here.
import { sceneFrames } from "./scenes3d.js?v=8f6fef3";

const { sin, cos, abs, sqrt, floor, atan2, PI, exp, log, max, min, round } = Math;
const TAU = 2 * PI;
const fract = (v) => v - floor(v);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const hash = (i) => { let h = Math.imul(i ^ 0x9e3779b9, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };

export const ILLUSIONS = [
  ["lilac", "Lilac chaser", "Look at the cross: a green dot runs round, and the pink ones fade away."],
  ["dots", "Spinning dot sphere", "Which way does it turn? It can flip while you watch."],
  ["spiral", "Hypnotic spiral", "Stare at the middle, then look at something else: it seems to move."],
  ["barber", "Barber pole", "The stripes move sideways, but look like they climb."],
  ["wheel", "Wagon wheel", "The wheel turns forwards, but seems to roll back."],
  ["feet", "Stepping feet", "Both bars move steadily, but seem to take turns stepping."],
  ["zoom", "Endless zoom", "Squares inside squares, falling in for ever."],
  ["necker", "Ambiguous cube", "Which face is in front? Let it flip."],
  ["cafe", "Café wall", "The rows are straight, but look tilted."],
  ["penrose", "Penrose triangle", "Three bars that can't make a triangle, until you see them from just the right place."],
];
export const illusionNote = (id) => (ILLUSIONS.find(([k]) => k === id) || ILLUSIONS[0])[2];

/* a frame from a colour function of (u, v): -1..1 across the shorter side, v up; 3 x 3 samples */
function draw(w, h, fn) {
  const out = new Uint8Array(w * h * 3), s = min(w, h) / 2, ss = 3;
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      let r = 0, g = 0, b = 0;
      for (let j = 0; j < ss; j++)
        for (let i = 0; i < ss; i++) {
          const c = fn((x + (i + 0.5) / ss - w / 2) / s, (h / 2 - (y + (j + 0.5) / ss)) / s);
          r += c[0]; g += c[1]; b += c[2];
        }
      const o = (y * w + x) * 3, n = ss * ss;
      out[o] = clamp(round(r / n), 0, 255); out[o + 1] = clamp(round(g / n), 0, 255); out[o + 2] = clamp(round(b / n), 0, 255);
    }
  return out;
}
const segDist = (px, py, ax, ay, bx, by) => {
  const ex = bx - ax, ey = by - ay, k = clamp(((px - ax) * ex + (py - ay) * ey) / (ex * ex + ey * ey || 1), 0, 1);
  return sqrt((px - ax - ex * k) ** 2 + (py - ay - ey * k) ** 2);
};

/* each: (f, n, fps) -> fn(u, v) for frame f of n */
const MAKE = {
  lilac(f, n, fps) {                                                 /* one dot missing, about 8 steps a second */
    const step = floor(f / max(1, round(fps / 8))) % 12;
    return (u, v) => {
      let k = 0;
      for (let i = 0; i < 12; i++) {
        if (i === step) continue;
        const a = i * TAU / 12, d2 = (u - 0.72 * cos(a)) ** 2 + (v - 0.72 * sin(a)) ** 2;
        k += exp(-d2 / 0.018);
      }
      k = min(1, k);
      if ((abs(u) < 0.05 && abs(v) < 0.16) || (abs(v) < 0.05 && abs(u) < 0.16)) return [0, 0, 0];   /* the cross */
      return [110 + (205 - 110) * k, 110 + (60 - 110) * k, 110 + (200 - 110) * k];
    };
  },
  dots(f, n) {                                                       /* no shading, no size change: no depth cue */
    const a = TAU * f / n, pts = [];
    for (let i = 0; i < 70; i++) {
      const z = hash(i) * 2 - 1, t = hash(i + 999) * TAU, r = sqrt(1 - z * z);
      const x = r * cos(t), y = z, zz = r * sin(t);
      pts.push([0.85 * (x * cos(a) + zz * sin(a)), 0.85 * y]);
    }
    return (u, v) => {
      let k = 0;
      for (const [px, py] of pts) { const d2 = (u - px) ** 2 + (v - py) ** 2; if (d2 < 0.01) k += exp(-d2 / 0.0012); }
      k = min(1, k);
      return [255 * k, 255 * k, 255 * k];
    };
  },
  spiral(f, n) {
    const a = TAU * f / n;
    return (u, v) => {
      const r = sqrt(u * u + v * v) + 1e-6, s = sin(atan2(v, u) * 3 + log(r) * 6 - a * 3);
      const k = clamp(s * 4, -1, 1) * 0.5 + 0.5;
      return [20 + 235 * k, 20 + 150 * k * k, 60 + 195 * k];
    };
  },
  barber(f, n) {
    const p = f / n;
    return (u, v) => {
      if (abs(u) > 0.42) return abs(u) < 0.5 ? [90, 90, 100] : [0, 0, 0];     /* the window and its frame */
      const s = fract((u * 0.7 + v) * 1.6 - p * 3), c = s < 0.33 ? [230, 30, 40] : s < 0.5 ? [240, 240, 240] : s < 0.83 ? [40, 70, 220] : [240, 240, 240];
      return c;
    };
  },
  wheel(f, n, fps) {                                                 /* each frame turns 92 % of a spoke: it seems to go back */
    const rot = f * (TAU / 8) * 0.92;
    return (u, v) => {
      const r = sqrt(u * u + v * v), a = atan2(v, u) - rot;
      if (r > 0.92) return [0, 0, 0];
      if (r > 0.8) return [180, 180, 190];
      if (r < 0.12) return [200, 160, 60];
      const s = abs(fract(a * 8 / TAU + 0.5) - 0.5);                    /* 0 on a spoke */
      return s < 0.06 ? [220, 220, 230] : [10, 10, 14];
    };
  },
  feet(f, n) {
    const p = f / n;
    return (u, v) => {
      const bx = -1.6 + 3.2 * p;
      if (abs(v - 0.45) < 0.15 && abs(u - bx) < 0.32) return [255, 225, 40];  /* light bar */
      if (abs(v + 0.45) < 0.15 && abs(u - bx) < 0.32) return [20, 30, 120];   /* dark bar */
      return fract(u * 4) < 0.5 ? [0, 0, 0] : [235, 235, 235];
    };
  },
  zoom(f, n) {                                                       /* one level deeper per loop: seamless */
    const p = f / n;
    return (u, v) => {
      const r = max(abs(u), abs(v)) + 1e-6, l = log(r) / log(1.7) + p * 2, lev = floor(l);
      const k = (lev & 1) ? 1 : 0, h = fract(lev / 6 + p / 3);
      const c = [0.5 + 0.5 * cos(TAU * h), 0.5 + 0.5 * cos(TAU * (h + 1 / 3)), 0.5 + 0.5 * cos(TAU * (h + 2 / 3))];
      return k ? [c[0] * 240, c[1] * 240, c[2] * 240] : [10, 10, 20];
    };
  },
  necker(f, n) {                                                     /* a wire cube without perspective or shading */
    const a = TAU * f / n, ca = cos(a), sa = sin(a), cb = cos(0.45), sb = sin(0.45);
    const V = [[-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1], [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]].map(([x, y, z]) => {
      const x1 = x * ca + z * sa, z1 = -x * sa + z * ca, y1 = y * cb - z1 * sb;
      return [x1 * 0.48, y1 * 0.48];
    });
    const E = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
    return (u, v) => {
      let d = 9;
      for (const [i, j] of E) d = min(d, segDist(u, v, V[i][0], V[i][1], V[j][0], V[j][1]));
      const k = clamp(1 - (d - 0.02) / 0.05, 0, 1);
      return [190 * k, 240 * k, 255 * k];
    };
  },
  cafe(f, n) {                                                       /* tiles in rows, every other row slid along */
    const s = sin(TAU * f / n) * 0.5;
    return (u, v) => {
      const row = floor((v + 1) * 3.5), fy = fract((v + 1) * 3.5);
      if (fy < 0.2) return [120, 120, 120];                                 /* the mortar: about 1 LED */
      const off = (row & 1) ? s : -s, t = floor((u + 1) * 3 + off);
      return (t & 1) ? [240, 240, 240] : [0, 0, 0];
    };
  },
};

/* the frames of an illusion: [{ rgb, delay }] */
export async function illusionFrames({ kind, secs = 3, fps = 15, w = 32, h = 32, onFrame }) {
  if (kind === "penrose") return sceneFrames({ scene: "penrose", bg: "black", secs, fps, w, h, ss: 3, onFrame });
  const make = MAKE[kind] || MAKE.lilac, n = Math.max(1, Math.round(secs * fps)), out = [];
  for (let f = 0; f < n; f++) {
    out.push({ rgb: draw(w, h, make(f, n, fps)), delay: 1000 / fps });
    if (onFrame) onFrame(f + 1, n);
    if (f % 4 === 3) await new Promise((r) => setTimeout(r, 0));
  }
  return out;
}

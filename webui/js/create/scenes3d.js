// 3D scenes for the animation maker: a small ray-marching renderer in plain JavaScript (signed distance
// fields, no WebGL), drawn with 2 x 2 samples a LED for smooth edges, then encoded like any other animation.
// Every scene loops: p runs from 0 to 1 over the animation and everything turns a whole number of times.
// No DOM here, so tools can run it too.
const { sin, cos, abs, sqrt, max, min, floor, round, PI, exp, pow } = Math;
const TAU = 2 * PI;

/* ---- maths on plain numbers (millions of calls per animation: no objects) ---- */
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const mix = (a, b, k) => a + (b - a) * k;
const len2 = (x, y) => sqrt(x * x + y * y);
const len3 = (x, y, z) => sqrt(x * x + y * y + z * z);
const smoothstep = (a, b, v) => { const t = clamp((v - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
function smin(a, b, k) { const h = clamp(0.5 + 0.5 * (b - a) / k, 0, 1); return mix(b, a, h) - k * h * (1 - h); }
const fract = (v) => v - floor(v);

/* a rotation (x, then y, then z) as 9 numbers; T() moves a point into the object's own space */
function rotation(ax, ay, az) {
  const cx = cos(ax), sx = sin(ax), cy = cos(ay), sy = sin(ay), cz = cos(az), sz = sin(az);
  return [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
          sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
          -sy, cy * sx, cy * cx];
}
let X = 0, Y = 0, Z = 0;                                            /* T()'s result */
function T(m, x, y, z) { X = m[0] * x + m[3] * y + m[6] * z; Y = m[1] * x + m[4] * y + m[7] * z; Z = m[2] * x + m[5] * y + m[8] * z; }

/* signed distances */
const sdSphere = (x, y, z, r) => len3(x, y, z) - r;
function sdBox(x, y, z, bx, by, bz, r) {
  const qx = abs(x) - bx + r, qy = abs(y) - by + r, qz = abs(z) - bz + r;
  return len3(max(qx, 0), max(qy, 0), max(qz, 0)) + min(max(qx, max(qy, qz)), 0) - r;
}
const sdTorus = (x, y, z, R, r) => len2(len2(x, z) - R, y) - r;
const sdOcta = (x, y, z, s) => (abs(x) + abs(y) + abs(z) - s) * 0.57735027;
function sdCyl(x, y, z, r, h, round_) {                            /* axis y */
  const dx = len2(x, z) - r + round_, dy = abs(y) - h + round_;
  return min(max(dx, dy), 0) + len2(max(dx, 0), max(dy, 0)) - round_;
}
function sdCapsule(x, y, z, ax, ay, az, bx, by, bz, r) {
  const px = x - ax, py = y - ay, pz = z - az, ex = bx - ax, ey = by - ay, ez = bz - az;
  const h = clamp((px * ex + py * ey + pz * ez) / (ex * ex + ey * ey + ez * ez), 0, 1);
  return len3(px - ex * h, py - ey * h, pz - ez * h) - r;
}

/* value noise in 3D, 0..1 */
function hash3(i, j, k) { let h = (i * 374761393 + j * 668265263 + k * 1274126177) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967296; }
function noise3(x, y, z) {
  const i = floor(x), j = floor(y), k = floor(z), fx = x - i, fy = y - j, fz = z - k;
  const ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy), uz = fz * fz * (3 - 2 * fz);
  const a = mix(mix(hash3(i, j, k), hash3(i + 1, j, k), ux), mix(hash3(i, j + 1, k), hash3(i + 1, j + 1, k), ux), uy);
  const b = mix(mix(hash3(i, j, k + 1), hash3(i + 1, j, k + 1), ux), mix(hash3(i, j + 1, k + 1), hash3(i + 1, j + 1, k + 1), ux), uy);
  return mix(a, b, uz);
}
const fbm = (x, y, z) => 0.5 * noise3(x, y, z) + 0.25 * noise3(x * 2.03, y * 2.03, z * 2.03) + 0.125 * noise3(x * 4.1, y * 4.1, z * 4.1) + 0.0625 * noise3(x * 8.3, y * 8.3, z * 8.3);

function hsv(h, s, v) {                                             /* -> [r, g, b] 0..1 */
  h = fract(h);
  const i = floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  return [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6];
}
const hueOf = ([r, g, b]) => {
  const mx = max(r, g, b), mn = min(r, g, b), d = mx - mn;
  if (!d) return 0;
  const h = mx === r ? (g - b) / d + (g < b ? 6 : 0) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return h / 6;
};

/* ---- backgrounds (where a ray hits nothing) ---- */
function starfield(dx, dy, dz) {
  const k = 24, h = hash3(floor(dx * k), floor(dy * k), floor(dz * k));
  return h > 0.992 ? [0.35 + (h - 0.992) * 60, 0.35 + (h - 0.992) * 60, 0.45] : [0, 0, 0];
}
const BACKGROUNDS = {
  black: () => [0, 0, 0],
  stars: starfield,
  dusk: (dx, dy) => { const k = clamp(dy * 1.4 + 0.45, 0, 1); return [mix(0.3, 0.01, k), mix(0.05, 0.0, k), mix(0.1, 0.04, k)]; },
  blue: (dx, dy) => { const k = clamp(dy + 0.5, 0, 1); return [mix(0.0, 0.0, k), mix(0.03, 0.0, k), mix(0.1, 0.02, k)]; },
};
export const BACKDROPS = [["stars", "Stars"], ["black", "Black"], ["dusk", "Dusk"], ["blue", "Deep blue"]];

/* ---- the scenes ----
   cam: where the eye is and what it looks at; light: the sun's direction; setup(p, c) once a frame (c = the
   chosen colour, 0..1); map(x, y, z) = distance to the nearest surface; paint(x, y, z, nx, ny, nz) = the
   surface colour there [r, g, b] (0..1), and optionally S.glow (light of its own), S.shine (gloss, 0..1).
   Optional: sky(dx, dy, dz) instead of the chosen background, shadows, fog, light(x..) for a headlight. */
let S = { shine: 0.5, glow: 0 };
const SCENES = {};

SCENES.donut = {
  name: "Donut", color: "#ff5fae", cam: [0, 0.75, 2.3], look: [0, -0.05, 0],
  setup(p) { this.m = rotation(0.55 + 0.25 * sin(TAU * p), TAU * p, 0.2 * sin(TAU * p)); },
  map(x, y, z) { T(this.m, x, y, z); return sdTorus(X, Y, Z, 0.62, 0.3); },
  paint(x, y, z, nx, ny, nz, c) {
    T(this.m, x, y, z);
    const a = Math.atan2(Z, X), glaze = Y > -0.03 + 0.06 * sin(a * 7) + 0.03 * sin(a * 13);
    if (!glaze) { S.shine = 0.15; return [0.82, 0.55, 0.28]; }
    S.shine = 0.8;
    const h = hash3(floor(X * 14), floor(Y * 14), floor(Z * 14));      /* sprinkles */
    if (h > 0.86) { S.shine = 0.3; return hsv(h * 7, 0.8, 1); }
    return c;
  },
};

SCENES.cube = {
  name: "Cube", color: "#3da5ff", cam: [0, 0.9, 2.1], look: [0, 0, 0],
  setup(p) { this.m = rotation(0.55 + 0.35 * sin(TAU * p), TAU * p, 0.3 * sin(TAU * p * 2)); },
  map(x, y, z) { T(this.m, x, y, z); return sdBox(X, Y, Z, 0.55, 0.55, 0.55, 0.1); },
  paint(x, y, z, nx, ny, nz, c) {
    T(this.m, nx, ny, nz);
    const ax = abs(X) > abs(Y) && abs(X) > abs(Z) ? (X > 0 ? 0 : 1) : abs(Y) > abs(Z) ? (Y > 0 ? 2 : 3) : (Z > 0 ? 4 : 5);
    S.shine = 0.7;
    return hsv(hueOf(c) + ax / 6, 0.75, 1);
  },
};

SCENES.menger = {
  name: "Menger sponge", color: "#ffb43c", cam: [0, 0.85, 1.95], look: [0, 0, 0],
  setup(p) { this.m = rotation(0.62, TAU * p, 0); },
  map(x, y, z) {
    T(this.m, x, y, z);
    let d = sdBox(X, Y, Z, 0.7, 0.7, 0.7, 0), s = 1 / 0.7;
    for (let i = 0; i < 2; i++) {                                    /* two levels: more is noise at 32 LEDs */
      const ax = abs(fract(X * s * 0.5 + 0.5) * 2 - 1), ay = abs(fract(Y * s * 0.5 + 0.5) * 2 - 1), az = abs(fract(Z * s * 0.5 + 0.5) * 2 - 1);
      const rx = abs(1 - 3 * ax), ry = abs(1 - 3 * ay), rz = abs(1 - 3 * az);   /* the cross that is cut out, per cell */
      const c = (min(max(rx, ry), max(ry, rz), max(rz, rx)) - 1) / (s * 3);
      d = max(d, c);
      s *= 3;
    }
    return d;
  },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 0.3; return c; },
};

SCENES.blobs = {
  name: "Blobs", color: "#9a6bff", cam: [0, 0.2, 2.6], look: [0, 0, 0],
  setup(p) {
    const K = [[1, 2, 0], [2, 1, 0.3], [1, 3, 0.6], [3, 1, 0.1], [2, 3, 0.8]];
    this.b = K.map(([a, b, ph]) => [0.55 * sin(TAU * (a * p + ph)), 0.45 * cos(TAU * (b * p + ph)), 0.35 * sin(TAU * (p + ph * 2))]);
  },
  map(x, y, z) {
    let d = 9;
    for (const [bx, by, bz] of this.b) d = smin(d, sdSphere(x - bx, y - by, z - bz, 0.32), 0.3);
    return d;
  },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 0.9; return hsv(hueOf(c) + 0.15 * nx + 0.1 * ny, 0.7, 1); },
};

SCENES.planet = {
  name: "Planet with rings", color: "#e2b46a", cam: [0, 0.7, 3.6], look: [0, -0.05, 0], light: [-0.8, 0.35, 0.5], shadows: true,
  setup(p) {
    this.spin = rotation(0, TAU * p, 0); this.tilt = rotation(0.18, 0, 0.38);
    const a = TAU * p;                                                  /* a moon, round once a loop */
    this.moon = [1.75 * cos(a), 0.35 * sin(a) + 0.1, 1.75 * sin(a)];
  },
  map(x, y, z) {
    T(this.tilt, x, y, z);
    const ring = max(abs(Y) - 0.006, abs(len2(X, Z) - 1.18) - 0.3);
    return min(sdSphere(x, y, z, 0.66), ring, sdSphere(x - this.moon[0], y - this.moon[1], z - this.moon[2], 0.13));
  },
  paint(x, y, z, nx, ny, nz, c) {
    S.shine = 0.15;
    if (len3(x - this.moon[0], y - this.moon[1], z - this.moon[2]) < 0.15) return [0.7, 0.72, 0.78];
    if (len3(x, y, z) < 0.68) {                                       /* bands that flow with the spin */
      T(this.tilt, x, y, z); const ty = Y; T(this.spin, X, Y, Z);
      const v = sin(ty * 18 + 2.5 * fbm(X * 3, Y * 3, Z * 3)) * 0.5 + 0.5;
      return [mix(c[0], 1, v * 0.4), mix(c[1] * 0.75, c[1], v), mix(c[2] * 0.6, c[2], v)];
    }
    T(this.tilt, x, y, z);
    const r = len2(X, Z), v = 0.55 + 0.45 * sin(r * 40) * sin(r * 13);
    return [0.75 * v, 0.68 * v, 0.55 * v];
  },
};

SCENES.globe = {
  name: "Globe", color: "#3a8dff", cam: [0, 0.25, 2.5], look: [0, 0, 0], light: [-0.75, 0.3, 0.6],
  setup(p) { this.m = rotation(0.41, TAU * p, 0); },
  map(x, y, z) { return sdSphere(x, y, z, 0.82); },
  paint(x, y, z, nx, ny, nz, c) {
    T(this.m, nx, ny, nz);
    const h = fbm(X * 2.2 + 7, Y * 2.2, Z * 2.2 + 3);
    S.shine = 0.05;
    if (abs(Y) > 0.86) return [0.92, 0.95, 1];                          /* ice */
    if (h < 0.5) { S.shine = 0.6; return [c[0] * 0.3, c[1] * 0.45 + 0.05, c[2] * 0.8 + 0.1]; }   /* sea */
    return h < 0.53 ? [0.8, 0.75, 0.5] : h < 0.62 ? [0.2, 0.6, 0.2] : [0.45, 0.38, 0.25];
  },
  rim: [0.3, 0.55, 1],
};

SCENES.twist = {
  name: "Twisting tower", color: "#ff7a3d", cam: [0, 0.4, 2.5], look: [0, 0, 0],
  setup(p) { this.k = 2.2 * sin(TAU * p); this.a = TAU * p; },
  map(x, y, z) {
    const a = this.a + this.k * y, c = cos(a), s = sin(a);
    return 0.6 * sdBox(c * x - s * z, y, s * x + c * z, 0.36, 0.95, 0.36, 0.05);
  },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 0.6; return hsv(hueOf(c) + y * 0.25, 0.75, 1); },
};

SCENES.tunnel = {
  name: "Ring tunnel", color: "#33e0ff", cam: [0, 0, 0], look: [0, 0, 1], headlight: true, fog: 0.22,
  setup(p) { this.cam = [0.15 * sin(TAU * p), 0.15 * cos(TAU * p), 8 * p]; this.look = [0, 0, 8 * p + 1]; },
  map(x, y, z) { const zz = fract(z) - 0.5; return len2(len2(x, y) - 0.62, zz) - 0.09; },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 0.6; S.glow = 0.15; return hsv(hueOf(c) + floor(z) * 0.125, 0.8, 1); },
};

SCENES.ball = {
  name: "Bouncing ball", color: "#ffcc00", cam: [0, 0.25, 2.0], look: [0, -0.35, 0], light: [-0.5, 0.8, 0.4], shadows: true,
  setup(p) {
    const b = abs(sin(TAU * p * 2));                                   /* two bounces a loop */
    this.y = -0.65 + b * 1.05; this.sq = 1 - 0.25 * max(0, 0.2 - b) * 5; this.m = rotation(0, 0, -TAU * p * 2);
  },
  map(x, y, z) {
    const by = y - this.y, ball = sdSphere(x, by / this.sq, z, 0.42) * min(1, this.sq);
    return min(ball, y + 1.07);
  },
  paint(x, y, z, nx, ny, nz, c) {
    if (y < -1.05) { S.shine = 0.2; return ((floor(x * 2.5) + floor(z * 2.5)) & 1) ? [0.85, 0.85, 0.9] : [0.25, 0.3, 0.45]; }
    T(this.m, x, y - this.y, z);
    S.shine = 0.85;
    return (floor(Math.atan2(Y, X) / PI * 4) & 1) ? c : [1, 1, 1];
  },
  sky: (dx, dy) => { const k = clamp(dy + 0.35, 0, 1); return [mix(0.55, 0.15, k), mix(0.7, 0.35, k), mix(0.95, 0.85, k)]; },
};

SCENES.gem = {
  name: "Gem", color: "#ff2d6f", cam: [0, 0.35, 2.5], look: [0, 0, 0],
  setup(p) { this.m = rotation(0.3 * sin(TAU * p), TAU * p, 0); },
  map(x, y, z) { T(this.m, x, y, z); return max(sdOcta(X, Y * 0.75, Z, 0.78), Y - 0.32); },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 1; return [c[0] * (0.6 + 0.4 * nx * nx), c[1] * (0.6 + 0.4 * ny * ny), c[2] * (0.6 + 0.4 * nz * nz)]; },
};

SCENES.gyroid = {
  name: "Gyroid", color: "#4cffa0", cam: [0, 0.3, 2.6], look: [0, 0, 0],
  setup(p) { this.m = rotation(TAU * p, 0.5, TAU * p); },
  map(x, y, z) {
    T(this.m, x, y, z);
    const s = 5.5, g = abs(sin(X * s) * cos(Y * s) + sin(Y * s) * cos(Z * s) + sin(Z * s) * cos(X * s)) / s - 0.025;
    return max(sdSphere(X, Y, Z, 0.95), g * 0.7);
  },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 0.5; return hsv(hueOf(c) + len3(x, y, z) * 0.35, 0.75, 1); },
};

SCENES.sea = {
  name: "Waves at sunset", color: "#1d63a8", cam: [0, 0.55, 0], look: [0, 0.3, 4], light: [0, 0.18, 1], fog: 0.06,
  setup(p) { this.t = TAU * p; },
  h(x, z) { const t = this.t; return 0.06 * sin(x * 1.3 + z * 0.9 - t * 2) + 0.05 * sin(x * 2.1 - z * 1.7 - t * 3) + 0.025 * sin(x * 4.3 + z * 3.9 - t * 5); },
  map(x, y, z) { return (y - this.h(x, z)) * 0.6; },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 1; return [c[0] * 0.5, c[1] * 0.6, c[2] * 0.8]; },
  sky(dx, dy, dz) {
    const sun = max(0, dx * 0 + dy * 0.18 + dz * 0.98);
    const k = clamp(dy * 2.2, 0, 1), g = [mix(1, 0.25, k), mix(0.45, 0.12, k), mix(0.2, 0.35, k)];
    const s = pow(sun, 400) * 3 + pow(sun, 30) * 0.4;
    return [g[0] + s, g[1] + s * 0.8, g[2] + s * 0.4];
  },
};

SCENES.helix = {
  name: "DNA helix", color: "#38d6ff", cam: [0, 0, 3.0], look: [0, 0, 0],
  setup(p) { this.a = TAU * p; this.cache = []; },
  strands(i) {
    const yi = i * 0.25, a = this.a + yi * 2.4;
    return [0.55 * cos(a), yi, 0.55 * sin(a), -0.55 * cos(a), yi, -0.55 * sin(a)];
  },
  map(x, y, z) {
    if (abs(y) > 1.3) return abs(y) - 1.25;
    const i0 = round(y / 0.25);
    let d = 9;
    for (let i = i0 - 1; i <= i0 + 1; i++) {
      if (abs(i) > 5) continue;
      const [ax, ay, az, bx, by, bz] = this.strands(i);
      d = min(d, sdSphere(x - ax, y - ay, z - az, 0.13), sdSphere(x - bx, y - by, z - bz, 0.13), sdCapsule(x, y, z, ax, ay, az, bx, by, bz, 0.04));
    }
    return d;
  },
  paint(x, y, z, nx, ny, nz, c) {
    S.shine = 0.7;
    const i = round(y / 0.25), [ax, , az, bx, , bz] = this.strands(i);
    const da = len2(x - ax, z - az), db = len2(x - bx, z - bz);
    if (da < 0.16) return c;
    if (db < 0.16) return [1, 0.35, 0.6];
    return hsv(((i % 4) + 4) % 4 * 0.25 + 0.1, 0.6, 0.95);
  },
};

SCENES.heart = {
  name: "Beating heart", color: "#ff2040", cam: [0, 0.1, 2.5], look: [0, 0.05, 0],
  setup(p) {
    const beat = exp(-(((p - 0.1) * 14) ** 2)) + 0.7 * exp(-(((p - 0.3) * 14) ** 2));
    this.s = 1 + 0.14 * beat; this.m = rotation(0, 0.5 * sin(TAU * p), 0);
  },
  map(x, y, z) {
    T(this.m, x / this.s, y / this.s, z / this.s);
    const c = 0.7071, rx = c * X - c * Y, ry = c * X + c * Y;           /* a turned box for the point */
    const body = sdBox(rx, ry + 0.06, Z * 1.6, 0.42, 0.42, 0.42, 0.18);
    const lobes = min(sdSphere(X - 0.28, Y - 0.22, Z * 1.6, 0.36), sdSphere(X + 0.28, Y - 0.22, Z * 1.6, 0.36));
    return smin(body, lobes, 0.12) * this.s * 0.62;
  },
  paint(x, y, z, nx, ny, nz, c) { S.shine = 0.9; return c; },
};

SCENES.atom = {
  name: "Atom", color: "#40e8ff", cam: [0, 0.4, 2.35], look: [0, 0, 0],
  setup(p) {
    this.orb = [rotation(1.25, 0, 0), rotation(1.25, 0, 2.1), rotation(1.25, 0, 4.2)];
    this.e = this.orb.map((m, k) => { const a = TAU * p * (k + 1) + k; const x = 1.0 * cos(a), z = 1.0 * sin(a); return [m[0] * x + m[2] * z, m[3] * x + m[5] * z, m[6] * x + m[8] * z]; });
    this.n = [[0.12, 0.1, 0], [-0.12, 0.08, 0.06], [0, -0.12, 0.1], [0.05, 0, -0.14], [-0.08, -0.08, -0.08], [0.1, -0.06, 0.12], [0, 0.15, -0.06]];
    this.spin = rotation(TAU * p, TAU * p, 0);
  },
  map(x, y, z) {
    T(this.spin, x, y, z);
    let d = 9;
    for (const [a, b, c] of this.n) d = min(d, sdSphere(X - a, Y - b, Z - c, 0.11));
    for (const m of this.orb) { T(m, x, y, z); d = min(d, len2(len2(X, Z) - 1.0, Y) - 0.02); }
    for (const [a, b, c] of this.e) d = min(d, sdSphere(x - a, y - b, z - c, 0.09));
    return d;
  },
  paint(x, y, z, nx, ny, nz, c) {
    S.shine = 0.6;
    for (const [a, b, cc] of this.e) if (len3(x - a, y - b, z - cc) < 0.11) { S.glow = 0.8; return c; }
    if (len3(x, y, z) < 0.35) {
      T(this.spin, x, y, z);
      let best = 0, bd = 9;
      this.n.forEach(([a, b, cc], i) => { const d = len3(X - a, Y - b, Z - cc); if (d < bd) { bd = d; best = i; } });
      return best & 1 ? [1, 0.3, 0.25] : [0.35, 0.5, 1];
    }
    S.glow = 0.4;
    return [0.4, 0.45, 0.6];
  },
};

SCENES.coin = {
  name: "Spinning coin", color: "#ffc63a", cam: [0, 0.15, 2.4], look: [0, 0, 0], light: [-0.5, 0.6, 0.7],
  setup(p) { this.m = rotation(PI / 2, TAU * p * 2, 0); },        /* face to the eye, turning round the upright */
  map(x, y, z) { T(this.m, x, y, z); return sdCyl(X, Y, Z, 0.78, 0.07, 0.03); },
  paint(x, y, z, nx, ny, nz, c) {                                  /* a rim line and a star on each face */
    T(this.m, x, y, z);
    S.shine = 1;
    const r = len2(X, Z), a = Math.atan2(Z, X), star = r < 0.45 - 0.22 * abs(fract(a / TAU * 5 + 0.25) - 0.5) * 2;
    if (abs(Y) < 0.06) return [c[0] * 0.7, c[1] * 0.6, c[2] * 0.4];  /* the edge */
    if ((r > 0.58 && r < 0.64) || star) return [min(1, c[0] * 1.3), min(1, c[1] * 1.2), c[2] * 0.6];
    return [c[0] * 0.75, c[1] * 0.65, c[2] * 0.35];
  },
  sky: (dx, dy) => [0.06 + max(0, dy) * 0.2, 0.05 + max(0, dy) * 0.18, 0.08 + max(0, dy) * 0.3],
};

const PIPS = [];                                                     /* the die's pips: face normal, then spots */
(() => {
  const at = { 1: [[0, 0]], 2: [[-1, -1], [1, 1]], 3: [[-1, -1], [0, 0], [1, 1]], 4: [[-1, -1], [1, -1], [-1, 1], [1, 1]],
               5: [[-1, -1], [1, -1], [0, 0], [-1, 1], [1, 1]], 6: [[-1, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [1, 1]] };
  const faces = [[1, [0, 1, 0]], [6, [0, -1, 0]], [2, [1, 0, 0]], [5, [-1, 0, 0]], [3, [0, 0, 1]], [4, [0, 0, -1]]];
  for (const [n, [fx, fy, fz]] of faces)
    for (const [u, v] of at[n]) {
      const s = 0.24;
      PIPS.push(fx ? [fx * 0.55, u * s, v * s] : fy ? [u * s, fy * 0.55, v * s] : [u * s, v * s, fz * 0.55]);
    }
})();

SCENES.die = {
  name: "Rolling die", color: "#f4f4f4", cam: [0, 0.7, 2.5], look: [0, 0, 0],
  setup(p) { this.m = rotation(TAU * p, TAU * p * 2, TAU * p); },
  pips(x, y, z) { let d = 9; for (const [a, b, c] of PIPS) d = min(d, sdSphere(x - a, y - b, z - c, 0.1)); return d; },
  map(x, y, z) { T(this.m, x, y, z); return max(sdBox(X, Y, Z, 0.5, 0.5, 0.5, 0.1), -this.pips(X, Y, Z)); },
  paint(x, y, z, nx, ny, nz, c) {
    T(this.m, x, y, z);
    S.shine = 0.6;
    return this.pips(X, Y, Z) < 0.015 ? [0.08, 0.08, 0.12] : c;
  },
};

export const SCENE_LIST = Object.entries(SCENES).map(([id, s]) => [id, s.name]);
export const sceneColor = (id) => (SCENES[id] || SCENES.donut).color;

/* ---- the renderer ---- */
function norm3(v) { const l = len3(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }

function march(sc, ox, oy, oz, dx, dy, dz, far) {
  let t = 0.01;
  for (let i = 0; i < 90; i++) {
    const d = sc.map(ox + dx * t, oy + dy * t, oz + dz * t);
    if (d < 0.0015 * t) return t;
    t += d;
    if (t > far) return -1;
  }
  return -1;
}
function shadow(sc, ox, oy, oz, lx, ly, lz) {
  let res = 1, t = 0.03;
  for (let i = 0; i < 28 && t < 4; i++) {
    const h = sc.map(ox + lx * t, oy + ly * t, oz + lz * t);
    if (h < 0.001) return 0;
    res = min(res, 10 * h / t);
    t += clamp(h, 0.02, 0.3);
  }
  return clamp(res, 0, 1);
}

function renderFrame(sc, w, h, bg, color, ss) {
  const out = new Uint8Array(w * h * 3);
  const cam = sc.cam, look = sc.look;
  const f = norm3([look[0] - cam[0], look[1] - cam[1], look[2] - cam[2]]);
  const r = norm3([-f[2], 0, f[0]]), u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];   /* right = f x up, up = right x f */
  const L = norm3(sc.light || [-0.6, 0.65, 0.55]), fl = 1.7, aspect = w / h, sky = sc.sky || BACKGROUNDS[bg] || BACKGROUNDS.stars;
  for (let py = 0; py < h; py++)
    for (let px = 0; px < w; px++) {
      let cr = 0, cg = 0, cb = 0;
      for (let sy = 0; sy < ss; sy++)
        for (let sx = 0; sx < ss; sx++) {
          const uu = ((px + (sx + 0.5) / ss) / w * 2 - 1) * aspect, vv = 1 - (py + (sy + 0.5) / ss) / h * 2;
          let dx = f[0] * fl + r[0] * uu + u[0] * vv, dy = f[1] * fl + r[1] * uu + u[1] * vv, dz = f[2] * fl + r[2] * uu + u[2] * vv;
          const dl = len3(dx, dy, dz); dx /= dl; dy /= dl; dz /= dl;
          const t = march(sc, cam[0], cam[1], cam[2], dx, dy, dz, 12);
          let col;
          if (t < 0) col = sky(dx, dy, dz);
          else {
            const x = cam[0] + dx * t, y = cam[1] + dy * t, z = cam[2] + dz * t, e = 0.0015;
            const d0 = sc.map(x, y, z);
            let nx = sc.map(x + e, y, z) - d0, ny = sc.map(x, y + e, z) - d0, nz = sc.map(x, y, z + e) - d0;
            const nl = len3(nx, ny, nz) || 1; nx /= nl; ny /= nl; nz /= nl;
            S.shine = 0.5; S.glow = 0;
            const alb = sc.paint(x, y, z, nx, ny, nz, color);
            const lx = sc.headlight ? -dx : L[0], ly = sc.headlight ? -dy : L[1], lz = sc.headlight ? -dz : L[2];
            const diff = max(0, nx * lx + ny * ly + nz * lz);
            const sh = sc.shadows && diff > 0 ? shadow(sc, x + nx * 0.01, y + ny * 0.01, z + nz * 0.01, lx, ly, lz) : 1;
            const rd = dx * nx + dy * ny + dz * nz, rx = dx - 2 * rd * nx, ry = dy - 2 * rd * ny, rz = dz - 2 * rd * nz;
            const spec = pow(max(0, rx * lx + ry * ly + rz * lz), 8 + S.shine * 56) * S.shine * 1.2 * sh;
            const ao = clamp(0.5 + 0.5 * sc.map(x + nx * 0.12, y + ny * 0.12, z + nz * 0.12) / 0.12, 0.35, 1);
            const amb = (0.16 + 0.1 * ny) * ao, k = amb + diff * sh * 0.95 + S.glow;
            const fres = pow(clamp(1 + rd, 0, 1), 3) * 0.35, rim = sc.rim || [0.5, 0.55, 0.7];
            col = [alb[0] * k + spec + rim[0] * fres, alb[1] * k + spec + rim[1] * fres, alb[2] * k + spec + rim[2] * fres];
            if (sc.fog) { const fo = exp(-t * sc.fog), s2 = sky(dx, dy, dz); col = [mix(s2[0], col[0], fo), mix(s2[1], col[1], fo), mix(s2[2], col[2], fo)]; }
          }
          cr += col[0]; cg += col[1]; cb += col[2];
        }
      const n = ss * ss, o = (py * w + px) * 3;
      out[o] = clamp(round(sqrt(cr / n) * 255), 0, 255);                /* gamma: brighter mid tones, as LEDs need */
      out[o + 1] = clamp(round(sqrt(cg / n) * 255), 0, 255);
      out[o + 2] = clamp(round(sqrt(cb / n) * 255), 0, 255);
    }
  return out;
}

const hexRgb = (hex) => { const v = parseInt(String(hex).replace("#", ""), 16) || 0; return [((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255]; };
/* linear light for the maths, so colours come out as picked after the gamma above */
const lin = (c) => c.map((v) => v * v);

/* the frames of a scene: [{ rgb, delay }]. onFrame(done, total) after each one; yields to the page between
   frames (await) so it stays responsive. */
export async function sceneFrames({ scene, color, bg = "stars", secs = 3, fps = 20, w = 32, h = 32, ss = 2, onFrame }) {
  const sc = SCENES[scene] || SCENES.donut, n = Math.max(1, Math.round(secs * fps)), out = [], c = lin(hexRgb(color || sc.color));
  const cam0 = sc.cam, look0 = sc.look;
  for (let i = 0; i < n; i++) {
    sc.cam = cam0; sc.look = look0;
    sc.setup(i / n, c);
    out.push({ rgb: renderFrame(sc, w, h, bg, c, ss), delay: 1000 / fps });
    if (onFrame) onFrame(i + 1, n);
    if (i % 2 === 1) await new Promise((r) => setTimeout(r, 0));
  }
  sc.cam = cam0; sc.look = look0;
  return out;
}

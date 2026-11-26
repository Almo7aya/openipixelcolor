// Checks the web UI without a browser:
//  - every module under webui/js compiles (node --check), and every import names a file that exists and
//    something that file exports;
//  - every element the scripts look up by id ($("#name")) exists in index.html, and no id is used twice;
//  - every stylesheet, script and preview the page links to exists.
//   node tools/ci/check_webui.js
"use strict";
const fs = require("fs"), path = require("path"), { execFileSync } = require("child_process");

const dir = path.join(__dirname, "..", "..", "webui");
const html = fs.readFileSync(path.join(dir, "index.html"), "utf8");
const walk = (d) => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? walk(path.join(d, e.name)) : [path.join(d, e.name)]));
const modules = walk(path.join(dir, "js")).filter((f) => f.endsWith(".js"));
const rel = (f) => path.relative(dir, f).replace(/\\/g, "/");
let bad = 0;
const fail = (msg) => { console.error(msg); bad++; };

/* compile */
for (const f of modules) {
  try { execFileSync(process.execPath, ["--check", f], { stdio: "pipe" }); }
  catch (e) { fail(`${rel(f)}: ${String(e.stderr || e.message).trim().split("\n").slice(0, 5).join("\n")}`); }
}

/* imports -> exports */
const exportsOf = new Map();
for (const f of modules) {
  const src = fs.readFileSync(f, "utf8"), names = new Set();
  for (const m of src.matchAll(/^export\s+(?:async\s+)?(?:const|let|function\*?|class)\s+([A-Za-z_$][\w$]*)/gm)) names.add(m[1]);
  for (const m of src.matchAll(/^export\s+default\b/gm)) names.add("default");
  for (const m of src.matchAll(/^export\s*\{([^}]*)\}/gm)) m[1].split(",").forEach((s) => { const n = s.trim().split(/\s+as\s+/).pop(); if (n) names.add(n); });
  exportsOf.set(path.resolve(f), names);
}
for (const f of modules) {
  const src = fs.readFileSync(f, "utf8");
  for (const m of src.matchAll(/^import\s+([\s\S]*?)\s+from\s+"([^"]+)";/gm)) {
    const target = path.resolve(path.dirname(f), m[2]);
    const names = exportsOf.get(target);
    if (!names) { fail(`${rel(f)}: imports ${m[2]}, which doesn't exist`); continue; }
    let spec = m[1].trim();
    const def = spec.match(/^([A-Za-z_$][\w$]*)\s*(,|$)/);
    if (def) { if (!names.has("default")) fail(`${rel(f)}: ${m[2]} has no default export`); spec = spec.slice(def[0].length).trim(); }
    const braces = spec.match(/^\{([^}]*)\}$/);
    if (braces) braces[1].split(",").map((s) => s.trim().split(/\s+as\s+/)[0]).filter(Boolean)
      .forEach((n) => { if (!names.has(n)) fail(`${rel(f)}: ${m[2]} doesn't export ${n}`); });
  }
}

/* ids */
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]), idSet = new Set(ids);
for (const f of modules)
  for (const m of fs.readFileSync(f, "utf8").matchAll(/\$\("#([A-Za-z][\w-]*)"\)/g))
    if (!idSet.has(m[1])) fail(`${rel(f)}: no element with id "${m[1]}"`);
for (const id of new Set(ids.filter((id, i) => ids.indexOf(id) !== i))) fail(`index.html: id "${id}" is used more than once`);

/* linked files */
for (const m of html.matchAll(/\b(?:href|src)="([^"#:]+)"/g)) if (!fs.existsSync(path.join(dir, m[1]))) fail(`index.html: ${m[1]} is missing`);
const catalog = fs.readFileSync(path.join(dir, "js", "catalog.js"), "utf8");
const preview = (name) => { for (const ext of ["gif", "png"])
  if (!fs.existsSync(path.join(dir, `previews/${name}.${ext}`))) fail(`previews/${name}.${ext} is missing (python tools/sim/make_previews.py)`); };
const count = (list) => (catalog.match(new RegExp(`export const ${list} = \\[([\\s\\S]*?)\\];`))[1].match(/"[^"]*"/g) || []).length;
for (let i = 0; i < count("EFFECTS"); i++) preview(`effect-${String(i).padStart(2, "0")}`);
preview("welcome-00");
/* clock faces: one file per way of showing them (24-hour, date), listed in clocks.json */
let clocks = [];
try { clocks = JSON.parse(fs.readFileSync(path.join(dir, "previews", "clocks.json"), "utf8")); } catch (e) { fail(`previews/clocks.json: ${e.message}`); }
if (clocks.length !== count("CLOCKS") || clocks.some((r) => !Array.isArray(r) || r.length !== 4)) fail("previews/clocks.json doesn't match CLOCKS (4 versions a face)");
for (const name of new Set(clocks.flat())) preview(name);

if (bad) process.exit(1);
console.log(`web UI: ${modules.length} modules compile and link, ${idSet.size} element ids, all lookups and files found`);

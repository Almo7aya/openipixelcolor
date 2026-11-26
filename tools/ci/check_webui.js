// Checks the web UI without a browser: every script compiles, and every element the scripts look up by id
// ($("#name")) exists in index.html.   node tools/ci/check_webui.js
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");

const dir = path.join(__dirname, "..", "..", "webui");
const html = fs.readFileSync(path.join(dir, "index.html"), "utf8");
const sources = fs.readdirSync(dir).filter((f) => f.endsWith(".js")).map((f) => [f, fs.readFileSync(path.join(dir, f), "utf8")]);
[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].forEach((m, i) => sources.push([`index.html script ${i + 1}`, m[1]]));

let bad = 0;
for (const [name, code] of sources) {
  try { new vm.Script(code, { filename: name }); }
  catch (e) { console.error(`${name}: ${e.message}`); bad++; }
}

const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
for (const [name, code] of sources)
  for (const m of code.matchAll(/\$\("#([A-Za-z][\w-]*)"\)/g))
    if (!ids.has(m[1])) { console.error(`${name}: no element with id "${m[1]}"`); bad++; }

const dup = [...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]).filter((id, i, all) => all.indexOf(id) !== i);
for (const id of new Set(dup)) { console.error(`index.html: id "${id}" is used more than once`); bad++; }

if (bad) process.exit(1);
console.log(`web UI: ${sources.length} scripts compile, ${ids.size} element ids, all lookups found`);

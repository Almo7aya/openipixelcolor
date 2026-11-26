"""Stamps the assembled web app (the CI's site folder) with its version, so a deploy never shares a URL with
the one before: GitHub Pages lets browsers keep files for 10 minutes, and a new page with an old cached
script breaks. Also fills in the service worker's file list (webui/sw.js).

    python3 tools/ci/stamp_site.py <site-dir> <version>

- index.html: the stylesheets and the main script get ?v=<version>; <meta name="oipc-version"> = <version>
  (the page registers the service worker only when this isn't "dev")
- every module: its imports of other modules (from "./x.js", "../x.js") get ?v=<version>, so the whole
  module graph is new; fetch("panel.wasm") too
- sw.js: VERSION and FILES (everything the app needs offline; not firmware/, which is always fetched fresh)
"""
import json
import os
import re
import sys

STAMPED = ("css/", "js/")                     # files whose URLs get ?v= (they change with the code)


def main():
    site, version = sys.argv[1], sys.argv[2]
    if not re.fullmatch(r"[0-9A-Za-z._-]+", version):
        sys.exit("bad version")
    q = f"?v={version}"

    # index.html
    p = os.path.join(site, "index.html")
    html = open(p, encoding="utf-8").read()
    html, n_links = re.subn(r'((?:href|src)="(?:css|js)/[^"?]+\.(?:css|js))"', rf'\1{q}"', html)
    html, n_meta = re.subn(r'(<meta name="oipc-version" content=")dev(">)', rf"\g<1>{version}\2", html)
    if not n_links or n_meta != 1:
        sys.exit("index.html: nothing to stamp (links %d, meta %d)" % (n_links, n_meta))
    open(p, "w", encoding="utf-8").write(html)

    # modules
    n_imports = 0
    for root, _, files in os.walk(os.path.join(site, "js")):
        for f in files:
            if not f.endswith(".js"):
                continue
            p = os.path.join(root, f)
            src = open(p, encoding="utf-8").read()
            src, n = re.subn(r'(\bfrom\s+"\.{1,2}/[^"?]+\.js)"', rf'\1{q}"', src)
            src, m = re.subn(r'(\bimport\(\s*"\.{1,2}/[^"?]+\.js)"', rf'\1{q}"', src)
            src = src.replace('fetch("panel.wasm")', f'fetch("panel.wasm{q}")')
            n_imports += n + m
            open(p, "w", encoding="utf-8").write(src)

    # the service worker's list: every file of the app, by the URL the page asks for
    files = ["./"]
    for root, dirs, names in os.walk(site):
        dirs[:] = [d for d in dirs if d not in ("firmware", ".git")]
        for name in names:
            rel = os.path.relpath(os.path.join(root, name), site).replace(os.sep, "/")
            if rel in ("sw.js", ".nojekyll") or rel.startswith("firmware/"):
                continue
            files.append(rel + (q if rel.startswith(STAMPED) or rel == "panel.wasm" else ""))
    p = os.path.join(site, "sw.js")
    sw = open(p, encoding="utf-8").read()
    sw, a = re.subn(r'const VERSION = "dev";', f"const VERSION = {json.dumps(version)};", sw)
    sw, b = re.subn(r"const FILES = \[\];", "const FILES = " + json.dumps(sorted(files)) + ";", sw)
    if a != 1 or b != 1:
        sys.exit("sw.js: VERSION or FILES not found")
    open(p, "w", encoding="utf-8").write(sw)
    print(f"stamped {version}: {n_links} links, {n_imports} imports, {len(files)} files for offline use")


if __name__ == "__main__":
    main()

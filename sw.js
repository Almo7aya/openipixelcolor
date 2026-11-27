// The web app's service worker: it can be installed as an app, works offline, and updates as one piece.
// Every file of a version sits in that version's cache, so an old script is never mixed with a new page.
// The CI writes VERSION and FILES when it publishes the site (tools/ci/stamp_site.py). With "dev" (served
// from this computer) the page doesn't register it.
const VERSION = "ed0e7b5";
const FILES = ["./", "css/base.css?v=ed0e7b5", "css/components.css?v=ed0e7b5", "css/layout.css?v=ed0e7b5", "css/views.css?v=ed0e7b5", "favicon.ico", "icons/apple-touch-icon.png", "icons/favicon.svg", "icons/icon-192.png", "icons/icon-512.png", "icons/logo.svg", "index.html", "js/animpreview.js?v=ed0e7b5", "js/ble.js?v=ed0e7b5", "js/catalog.js?v=ed0e7b5", "js/core.js?v=ed0e7b5", "js/create/anim-codec.js?v=ed0e7b5", "js/create/animate.js?v=ed0e7b5", "js/create/draw.js?v=ed0e7b5", "js/create/generators.js?v=ed0e7b5", "js/create/illusions.js?v=ed0e7b5", "js/create/import.js?v=ed0e7b5", "js/create/picfx.js?v=ed0e7b5", "js/create/picture.js?v=ed0e7b5", "js/create/pixelcanvas.js?v=ed0e7b5", "js/create/scenes3d.js?v=ed0e7b5", "js/debug.js?v=ed0e7b5", "js/device.js?v=ed0e7b5", "js/dialog.js?v=ed0e7b5", "js/firmware.js?v=ed0e7b5", "js/font5x7.js?v=ed0e7b5", "js/idb.js?v=ed0e7b5", "js/led.js?v=ed0e7b5", "js/library.js?v=ed0e7b5", "js/log.js?v=ed0e7b5", "js/main.js?v=ed0e7b5", "js/mirror.js?v=ed0e7b5", "js/nav.js?v=ed0e7b5", "js/play.js?v=ed0e7b5", "js/protocol.js?v=ed0e7b5", "js/pwa.js?v=ed0e7b5", "js/readback.js?v=ed0e7b5", "js/size.js?v=ed0e7b5", "js/toast.js?v=ed0e7b5", "js/views/device.js?v=ed0e7b5", "js/views/storage.js?v=ed0e7b5", "js/views/text.js?v=ed0e7b5", "js/views/textpreview.js?v=ed0e7b5", "js/virtual.js?v=ed0e7b5", "manifest.webmanifest", "package.json", "panel.wasm?v=ed0e7b5", "previews/clocks.json"];
const CACHE = `oipc-${VERSION}`;
// the effect and clock previews (2 MB of GIFs) aren't in FILES: they're kept across versions in their own
// cache, each one stored when the page first shows it and refreshed in the background after that
const PREVIEWS = "oipc-previews";

self.addEventListener("install", (e) => {
  // straight from the server, not from the browser's HTTP cache (which may hold the last version)
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES.map((f) => new Request(f, { cache: "reload" })))));
});

self.addEventListener("activate", (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith("oipc-") && k !== CACHE && k !== PREVIEWS).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

// the page's "Reload" on the update notice: this version takes over
self.addEventListener("message", (e) => { if (e.data === "skip-waiting") self.skipWaiting(); });

self.addEventListener("fetch", (e) => {
  const req = e.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;                       // fonts: the network
  if (url.pathname.includes("/firmware/")) return;                  // releases: always fresh
  if (req.mode === "navigate") {                                    // the page itself, any #view
    e.respondWith(caches.match("./", { cacheName: CACHE }).then((r) => r || fetch(req)));
    return;
  }
  if (/\/previews\/[^/]+\.(gif|png)$/.test(url.pathname)) {         // stored copy now, a fresh one for next time
    const net = caches.open(PREVIEWS).then((c) => fetch(req).then((r) => { if (r.ok) c.put(req, r.clone()); return r; }));
    e.respondWith(caches.match(req, { cacheName: PREVIEWS }).then((hit) => hit || net));
    e.waitUntil(net.catch(() => {}));
    return;
  }
  e.respondWith(caches.match(req, { cacheName: CACHE }).then((r) => r || fetch(req)));
});

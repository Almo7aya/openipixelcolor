// The web app's service worker: it can be installed as an app, works offline, and updates as one piece.
// Every file of a version sits in that version's cache, so an old script is never mixed with a new page.
// The CI writes VERSION and FILES when it publishes the site (tools/ci/stamp_site.py). With "dev" (served
// from this computer) the page doesn't register it.
const VERSION = "c02e0ab";
const FILES = ["./", "css/base.css?v=c02e0ab", "css/components.css?v=c02e0ab", "css/layout.css?v=c02e0ab", "css/views.css?v=c02e0ab", "favicon.ico", "icons/apple-touch-icon.png", "icons/favicon.svg", "icons/icon-192.png", "icons/icon-512.png", "icons/logo.svg", "index.html", "js/animpreview.js?v=c02e0ab", "js/ble.js?v=c02e0ab", "js/catalog.js?v=c02e0ab", "js/core.js?v=c02e0ab", "js/create/anim-codec.js?v=c02e0ab", "js/create/animate.js?v=c02e0ab", "js/create/draw.js?v=c02e0ab", "js/create/generators.js?v=c02e0ab", "js/create/illusions.js?v=c02e0ab", "js/create/import.js?v=c02e0ab", "js/create/picfx.js?v=c02e0ab", "js/create/picture.js?v=c02e0ab", "js/create/pixelcanvas.js?v=c02e0ab", "js/create/scenes3d.js?v=c02e0ab", "js/device.js?v=c02e0ab", "js/firmware.js?v=c02e0ab", "js/font5x7.js?v=c02e0ab", "js/idb.js?v=c02e0ab", "js/led.js?v=c02e0ab", "js/library.js?v=c02e0ab", "js/log.js?v=c02e0ab", "js/main.js?v=c02e0ab", "js/mirror.js?v=c02e0ab", "js/nav.js?v=c02e0ab", "js/play.js?v=c02e0ab", "js/protocol.js?v=c02e0ab", "js/pwa.js?v=c02e0ab", "js/readback.js?v=c02e0ab", "js/size.js?v=c02e0ab", "js/toast.js?v=c02e0ab", "js/views/device.js?v=c02e0ab", "js/views/storage.js?v=c02e0ab", "js/views/text.js?v=c02e0ab", "js/views/textpreview.js?v=c02e0ab", "js/virtual.js?v=c02e0ab", "manifest.webmanifest", "package.json", "panel.wasm?v=c02e0ab", "previews/clocks.json"];
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

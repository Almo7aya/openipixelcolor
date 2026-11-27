// The web app's service worker: it can be installed as an app, works offline, and updates as one piece.
// Every file of a version sits in that version's cache, so an old script is never mixed with a new page.
// The CI writes VERSION and FILES when it publishes the site (tools/ci/stamp_site.py). With "dev" (served
// from this computer) the page doesn't register it.
const VERSION = "d0fb26c";
const FILES = ["./", "css/base.css?v=d0fb26c", "css/components.css?v=d0fb26c", "css/layout.css?v=d0fb26c", "css/views.css?v=d0fb26c", "favicon.ico", "icons/apple-touch-icon.png", "icons/favicon.svg", "icons/icon-192.png", "icons/icon-512.png", "icons/logo.svg", "index.html", "js/animpreview.js?v=d0fb26c", "js/ble.js?v=d0fb26c", "js/catalog.js?v=d0fb26c", "js/core.js?v=d0fb26c", "js/create/anim-codec.js?v=d0fb26c", "js/create/animate.js?v=d0fb26c", "js/create/draw.js?v=d0fb26c", "js/create/generators.js?v=d0fb26c", "js/create/illusions.js?v=d0fb26c", "js/create/import.js?v=d0fb26c", "js/create/picfx.js?v=d0fb26c", "js/create/picture.js?v=d0fb26c", "js/create/pixelcanvas.js?v=d0fb26c", "js/create/scenes3d.js?v=d0fb26c", "js/device.js?v=d0fb26c", "js/firmware.js?v=d0fb26c", "js/font5x7.js?v=d0fb26c", "js/idb.js?v=d0fb26c", "js/led.js?v=d0fb26c", "js/library.js?v=d0fb26c", "js/log.js?v=d0fb26c", "js/main.js?v=d0fb26c", "js/mirror.js?v=d0fb26c", "js/nav.js?v=d0fb26c", "js/play.js?v=d0fb26c", "js/protocol.js?v=d0fb26c", "js/pwa.js?v=d0fb26c", "js/readback.js?v=d0fb26c", "js/size.js?v=d0fb26c", "js/toast.js?v=d0fb26c", "js/views/device.js?v=d0fb26c", "js/views/storage.js?v=d0fb26c", "js/views/text.js?v=d0fb26c", "js/views/textpreview.js?v=d0fb26c", "js/virtual.js?v=d0fb26c", "manifest.webmanifest", "package.json", "panel.wasm?v=d0fb26c", "previews/clocks.json"];
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

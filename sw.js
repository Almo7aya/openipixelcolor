// The web app's service worker: it can be installed as an app, works offline, and updates as one piece.
// Every file of a version sits in that version's cache, so an old script is never mixed with a new page.
// The CI writes VERSION and FILES when it publishes the site (tools/ci/stamp_site.py). With "dev" (served
// from this computer) the page doesn't register it.
const VERSION = "7cd4bac";
const FILES = ["./", "css/base.css?v=7cd4bac", "css/components.css?v=7cd4bac", "css/layout.css?v=7cd4bac", "css/views.css?v=7cd4bac", "favicon.ico", "icons/apple-touch-icon.png", "icons/favicon.svg", "icons/icon-192.png", "icons/icon-512.png", "icons/logo.svg", "index.html", "js/animpreview.js?v=7cd4bac", "js/ble.js?v=7cd4bac", "js/catalog.js?v=7cd4bac", "js/core.js?v=7cd4bac", "js/create/anim-codec.js?v=7cd4bac", "js/create/animate.js?v=7cd4bac", "js/create/draw.js?v=7cd4bac", "js/create/generators.js?v=7cd4bac", "js/create/import.js?v=7cd4bac", "js/create/picture.js?v=7cd4bac", "js/create/pixelcanvas.js?v=7cd4bac", "js/device.js?v=7cd4bac", "js/firmware.js?v=7cd4bac", "js/font5x7.js?v=7cd4bac", "js/idb.js?v=7cd4bac", "js/led.js?v=7cd4bac", "js/library.js?v=7cd4bac", "js/log.js?v=7cd4bac", "js/main.js?v=7cd4bac", "js/mirror.js?v=7cd4bac", "js/nav.js?v=7cd4bac", "js/play.js?v=7cd4bac", "js/protocol.js?v=7cd4bac", "js/pwa.js?v=7cd4bac", "js/readback.js?v=7cd4bac", "js/size.js?v=7cd4bac", "js/toast.js?v=7cd4bac", "js/views/device.js?v=7cd4bac", "js/views/storage.js?v=7cd4bac", "js/views/text.js?v=7cd4bac", "js/views/textpreview.js?v=7cd4bac", "js/virtual.js?v=7cd4bac", "manifest.webmanifest", "package.json", "panel.wasm?v=7cd4bac", "previews/clock-00-12h-nodate.gif", "previews/clock-00-12h-nodate.png", "previews/clock-00-12h.gif", "previews/clock-00-12h.png", "previews/clock-00-nodate.gif", "previews/clock-00-nodate.png", "previews/clock-00.gif", "previews/clock-00.png", "previews/clock-01.gif", "previews/clock-01.png", "previews/clock-02-12h.gif", "previews/clock-02-12h.png", "previews/clock-02.gif", "previews/clock-02.png", "previews/clock-03-12h.gif", "previews/clock-03-12h.png", "previews/clock-03.gif", "previews/clock-03.png", "previews/clock-04-12h.gif", "previews/clock-04-12h.png", "previews/clock-04.gif", "previews/clock-04.png", "previews/clock-05-12h-nodate.gif", "previews/clock-05-12h-nodate.png", "previews/clock-05-12h.gif", "previews/clock-05-12h.png", "previews/clock-05-nodate.gif", "previews/clock-05-nodate.png", "previews/clock-05.gif", "previews/clock-05.png", "previews/clock-06-12h.gif", "previews/clock-06-12h.png", "previews/clock-06.gif", "previews/clock-06.png", "previews/clock-07-12h.gif", "previews/clock-07-12h.png", "previews/clock-07.gif", "previews/clock-07.png", "previews/clock-08-12h.gif", "previews/clock-08-12h.png", "previews/clock-08.gif", "previews/clock-08.png", "previews/clock-09-12h-nodate.gif", "previews/clock-09-12h-nodate.png", "previews/clock-09-12h.gif", "previews/clock-09-12h.png", "previews/clock-09-nodate.gif", "previews/clock-09-nodate.png", "previews/clock-09.gif", "previews/clock-09.png", "previews/clocks.json", "previews/effect-00.gif", "previews/effect-00.png", "previews/effect-01.gif", "previews/effect-01.png", "previews/effect-02.gif", "previews/effect-02.png", "previews/effect-03.gif", "previews/effect-03.png", "previews/effect-04.gif", "previews/effect-04.png", "previews/effect-05.gif", "previews/effect-05.png", "previews/effect-06.gif", "previews/effect-06.png", "previews/effect-07.gif", "previews/effect-07.png", "previews/effect-08.gif", "previews/effect-08.png", "previews/effect-09.gif", "previews/effect-09.png", "previews/effect-10.gif", "previews/effect-10.png", "previews/effect-11.gif", "previews/effect-11.png", "previews/effect-12.gif", "previews/effect-12.png", "previews/effect-13.gif", "previews/effect-13.png", "previews/effect-14.gif", "previews/effect-14.png", "previews/effect-15.gif", "previews/effect-15.png", "previews/effect-16.gif", "previews/effect-16.png", "previews/effect-17.gif", "previews/effect-17.png", "previews/effect-18.gif", "previews/effect-18.png", "previews/effect-19.gif", "previews/effect-19.png", "previews/welcome-00.gif", "previews/welcome-00.png"];
const CACHE = `oipc-${VERSION}`;

self.addEventListener("install", (e) => {
  // straight from the server, not from the browser's HTTP cache (which may hold the last version)
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES.map((f) => new Request(f, { cache: "reload" })))));
});

self.addEventListener("activate", (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith("oipc-") && k !== CACHE).map((k) => caches.delete(k))))
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
  e.respondWith(caches.match(req, { cacheName: CACHE }).then((r) => r || fetch(req)));
});

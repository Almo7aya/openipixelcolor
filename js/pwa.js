// The installed app: registers the service worker (sw.js) on the published site, offers "Reload" when a new
// version of the web app is ready (never in the middle of a firmware install), and "Install as an app"
// where the browser offers it. On this computer (version "dev") there is no service worker: what you edit
// is what you get.
import { $ } from "./core.js?v=7d6fc0c";
import { panel } from "./ble.js?v=7d6fc0c";
import { toast, toastError } from "./toast.js?v=7d6fc0c";

const VERSION = document.querySelector('meta[name="oipc-version"]')?.content || "dev";

export function initPwa() {
  installButton();
  $("#appVersion").textContent = VERSION === "dev" ? "this computer's copy" : `web app ${VERSION}`;
  if (!("serviceWorker" in navigator)) return;
  if (VERSION === "dev") {                             /* a service worker from the published site would get in the way */
    navigator.serviceWorker.getRegistrations().then((rs) => rs.forEach((r) => r.unregister())).catch(() => {});
    return;
  }
  let wantReload = false, offered = false;
  navigator.serviceWorker.addEventListener("controllerchange", () => { if (wantReload) location.reload(); });
  const offer = (worker) => {
    if (offered) return;
    offered = true;
    toast("A new version of the web app is ready.", {
      kind: "update", timeout: 0,
      action: { label: "Reload", fn: () => {
        if (panel.busy) { offered = false; toastError("Wait until the firmware install finishes, then reload."); return; }
        wantReload = true;
        worker.postMessage("skip-waiting");
      } },
    });
  };
  navigator.serviceWorker.register("sw.js", { updateViaCache: "none" }).then((reg) => {
    if (reg.waiting && navigator.serviceWorker.controller) offer(reg.waiting);
    reg.addEventListener("updatefound", () => {
      const w = reg.installing;
      if (w) w.addEventListener("statechange", () => { if (w.state === "installed" && navigator.serviceWorker.controller) offer(w); });
    });
    /* browsers look for a new version only when the page is opened: look while it stays open too, every
       5 minutes and whenever the tab comes back into view or the computer back online */
    const check = () => reg.update().catch(() => {});
    setInterval(check, 5 * 60 * 1000);
    document.addEventListener("visibilitychange", () => { if (document.visibilityState === "visible") check(); });
    window.addEventListener("online", check);
  }).catch(() => { /* no offline use; the page still works */ });
}

function installButton() {
  const btn = $("#installApp");
  let prompt = null;
  window.addEventListener("beforeinstallprompt", (e) => { e.preventDefault(); prompt = e; btn.hidden = false; });
  window.addEventListener("appinstalled", () => { btn.hidden = true; prompt = null; });
  btn.addEventListener("click", async () => {
    if (!prompt) return;
    prompt.prompt();
    await prompt.userChoice.catch(() => null);
    prompt = null;
    btn.hidden = true;
  });
}

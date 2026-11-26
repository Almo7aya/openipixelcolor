// Animation files sent from this browser, kept in IndexedDB by checksum, so the panel preview can play
// what the panel plays after the page is opened again. Failures (private windows) just mean "not kept".
const MAX = 40;
let dbp = null;
const db = () => (dbp ||= new Promise((ok, bad) => {
  const r = indexedDB.open("openipixelcolor", 1);
  r.onupgradeneeded = () => r.result.createObjectStore("anims");
  r.onsuccess = () => ok(r.result);
  r.onerror = () => bad(r.error);
}));
function req(mode, fn) {
  return db().then((d) => new Promise((ok, bad) => {
    const r = fn(d.transaction("anims", mode).objectStore("anims"));
    r.onsuccess = () => ok(r.result);
    r.onerror = () => bad(r.error);
  }));
}

export async function getAnim(crc) {
  try { const v = await req("readonly", (s) => s.get(crc)); return v ? v.bytes : null; } catch (e) { return null; }
}
export async function putAnim(crc, bytes) {
  try {
    await req("readwrite", (s) => s.put({ bytes, t: Date.now() }, crc));
    const keys = await req("readonly", (s) => s.getAllKeys());
    if (keys.length <= MAX) return;
    const all = await Promise.all(keys.map(async (k) => [k, (await req("readonly", (s) => s.get(k))).t]));
    all.sort((a, b) => a[1] - b[1]).slice(0, keys.length - MAX).forEach(([k]) => req("readwrite", (s) => s.delete(k)));
  } catch (e) { /* not kept */ }
}

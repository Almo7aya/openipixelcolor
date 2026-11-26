// Start-up: every part of the page sets itself up, then the address (#play/clocks...) picks the view.
//
//   core.js, protocol.js       helpers; the Bluetooth protocol
//   ble.js                     the connection and the request queue (panel state lives in `panel`)
//   device.js, mirror.js       the device column: connection, preview, power, brightness
//   virtual.js (+panel.wasm)   the preview of what the panel shows: the firmware's own code, in the browser
//   nav.js                     destinations and sub-views
//   play.js, library.js        Play: effects, clocks, animations on the panel, tools
//   views/text.js (+preview)   Text
//   create/*.js                Create: animation maker, picture, drawing
//   firmware.js, views/*.js    Device: firmware install and update notice, storage, rotation, details, reset
//   pwa.js (+sw.js)            the installed app: offline use, "new version ready", install button
import { $, on } from "./core.js?v=b9006af";
import { initNav, registerSub, startNav } from "./nav.js?v=b9006af";
import { mirrorOwner } from "./mirror.js?v=b9006af";
import { initVirtual, showLive } from "./virtual.js?v=b9006af";
import { initDevice } from "./device.js?v=b9006af";
import { initPlay } from "./play.js?v=b9006af";
import { initLibrary } from "./library.js?v=b9006af";
import { initText } from "./views/text.js?v=b9006af";
import { initAnimate } from "./create/animate.js?v=b9006af";
import { initPicture } from "./create/picture.js?v=b9006af";
import { initDraw } from "./create/draw.js?v=b9006af";
import { initFirmware } from "./firmware.js?v=b9006af";
import { initDeviceView } from "./views/device.js?v=b9006af";
import { initStorage } from "./views/storage.js?v=b9006af";
import { initPwa } from "./pwa.js?v=b9006af";

initNav();
initDevice();
initVirtual();
initLibrary();
initPlay();
initText();
initAnimate();
initPicture();
initDraw();
registerSub("create", $("#createChips"), "animation");
initFirmware();
initDeviceView();
initStorage();
initPwa();

/* the preview shows the panel, except while a view shows something not sent yet; leaving that view
   brings the panel back (the views' own "view" handlers run first and take the preview when it's theirs) */
const PREVIEW_HOME = { text: "text", anim: "create/animation", picture: "create/picture", draw: "create/draw" };
on("view", ({ dest, sub }) => {
  const o = mirrorOwner();
  if (o !== "panel" && PREVIEW_HOME[o] !== (sub ? `${dest}/${sub}` : dest)) showLive();
});
showLive();
startNav();

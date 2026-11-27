// Start-up: every part of the page sets itself up, then the address (#play/clocks...) picks the view.
//
//   core.js, protocol.js       helpers; the Bluetooth protocol
//   ble.js                     the connection and the request queue (panel state lives in `panel`)
//   device.js, mirror.js       the device column: connection, preview, power, brightness
//   log.js                     the activity log under it
//   virtual.js (+panel.wasm)   the preview of what the panel shows: the firmware's own code, in the browser
//   nav.js                     destinations and sub-views
//   play.js, library.js        Play: effects, clocks, animations on the panel, tools
//   views/text.js (+preview)   Text
//   create/*.js                Create: animation maker, picture, drawing
//   firmware.js, views/*.js    Device: firmware install and update notice, storage, rotation, details, reset
//   toast.js, dialog.js        notices; the dialog over the page (a panel that isn't supported, firmware updates)
//   debug.js                   the hidden debug menu (Ctrl + Shift + five clicks on the logo)
//   pwa.js (+sw.js)            the installed app: offline use, "new version ready", install button
import { $, on } from "./core.js?v=ed0e7b5";
import { initNav, registerSub, startNav } from "./nav.js?v=ed0e7b5";
import { mirrorOwner } from "./mirror.js?v=ed0e7b5";
import { initVirtual, showLive } from "./virtual.js?v=ed0e7b5";
import { initDevice } from "./device.js?v=ed0e7b5";
import { initPlay } from "./play.js?v=ed0e7b5";
import { initLibrary } from "./library.js?v=ed0e7b5";
import { initText } from "./views/text.js?v=ed0e7b5";
import { initAnimate } from "./create/animate.js?v=ed0e7b5";
import { initPicture } from "./create/picture.js?v=ed0e7b5";
import { initDraw } from "./create/draw.js?v=ed0e7b5";
import { initFirmware } from "./firmware.js?v=ed0e7b5";
import { initDeviceView } from "./views/device.js?v=ed0e7b5";
import { initStorage } from "./views/storage.js?v=ed0e7b5";
import { initPwa } from "./pwa.js?v=ed0e7b5";
import { initLog } from "./log.js?v=ed0e7b5";
import { initDebug } from "./debug.js?v=ed0e7b5";

initLog();                                             /* first: it records what the others do */
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
initDebug();

/* the preview shows the panel, except while a view shows something not sent yet; leaving that view
   brings the panel back (the views' own "view" handlers run first and take the preview when it's theirs) */
const PREVIEW_HOME = { text: "text", anim: "create/animation", picture: "create/picture", draw: "create/draw" };
on("view", ({ dest, sub }) => {
  const o = mirrorOwner();
  if (o !== "panel" && PREVIEW_HOME[o] !== (sub ? `${dest}/${sub}` : dest)) showLive();
});
showLive();
startNav();

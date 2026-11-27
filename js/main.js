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
import { $, on } from "./core.js?v=8f6fef3";
import { initNav, registerSub, startNav } from "./nav.js?v=8f6fef3";
import { mirrorOwner } from "./mirror.js?v=8f6fef3";
import { initVirtual, showLive } from "./virtual.js?v=8f6fef3";
import { initDevice } from "./device.js?v=8f6fef3";
import { initPlay } from "./play.js?v=8f6fef3";
import { initLibrary } from "./library.js?v=8f6fef3";
import { initText } from "./views/text.js?v=8f6fef3";
import { initAnimate } from "./create/animate.js?v=8f6fef3";
import { initPicture } from "./create/picture.js?v=8f6fef3";
import { initDraw } from "./create/draw.js?v=8f6fef3";
import { initFirmware } from "./firmware.js?v=8f6fef3";
import { initDeviceView } from "./views/device.js?v=8f6fef3";
import { initStorage } from "./views/storage.js?v=8f6fef3";
import { initPwa } from "./pwa.js?v=8f6fef3";
import { initLog } from "./log.js?v=8f6fef3";
import { initDebug } from "./debug.js?v=8f6fef3";

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

// The connected panel's matrix size. iPixel Color panels come in several sizes. Every panel names its model
// in the reply to the time command (0x8001, byte 4: the device type); openipixelcolor 101 and later also
// reports its exact width and height (0xF113). Everything that makes pixels (drawing, pictures, animations,
// the panel preview) works at this size. Until a panel connects, and for a type not in the table: 32 x 32.
import { emit } from "./core.js?v=62301d0";

/* device type byte -> [width, height], from the iPixel Color app's own table as open-source clients read it
   (pypixelcolor lib/device_info.py, and others agree up to 0x93; 0x94-0xA0 from apps 3.7-3.8). Seen on
   real panels: 0x81 32 x 32, 0x83 64 x 16, 0x85 64 x 20. */
export const TYPES = {
  0x00: [64, 64], 0x80: [64, 64], 0x81: [32, 32], 0x82: [32, 16], 0x83: [64, 16], 0x84: [96, 16], 0x85: [64, 20],
  0x86: [128, 32], 0x87: [144, 16], 0x88: [192, 16], 0x89: [48, 24], 0x8A: [64, 32], 0x8B: [96, 32], 0x8C: [128, 32],
  0x8D: [96, 32], 0x8E: [160, 32], 0x8F: [192, 32], 0x90: [256, 32], 0x91: [320, 32], 0x92: [384, 32], 0x93: [448, 32],
  0x94: [16, 16], 0x95: [64, 64], 0x96: [96, 64], 0x97: [128, 64], 0x98: [160, 64], 0x99: [192, 64], 0x9A: [256, 64],
  0x9B: [320, 64], 0x9C: [384, 64], 0x9D: [448, 64], 0x9E: [512, 64], 0x9F: [576, 64], 0xA0: [640, 64],
};

export const size = { w: 32, h: 32, type: null, known: false };
export const npx = () => size.w * size.h;
export const is32 = () => size.w === 32 && size.h === 32;
export const sizeText = () => `${size.w} × ${size.h}`;

export function setSize(w, h, type, known) {
  const changed = w !== size.w || h !== size.h;
  Object.assign(size, { w, h, type, known });
  emit("size", { changed });
}
export const typeSize = (type) => TYPES[type] || null;

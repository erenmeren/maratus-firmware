import { writeFileSync, mkdirSync } from "node:fs";

const WIDTH_PX = 576;                 // must match RENDER_ROLL_WIDTH
const WIDTH_BYTES = WIDTH_PX / 8;     // 72
const HEIGHT = 240;

// 1bpp bitmap, MSB-first, 1 = black.
const rows = Buffer.alloc(WIDTH_BYTES * HEIGHT, 0x00);
const setPx = (x, y) => { rows[y * WIDTH_BYTES + (x >> 3)] |= 0x80 >> (x & 7); };
for (let y = 0; y < HEIGHT; y++) for (let x = 0; x < WIDTH_PX; x++) {
  const border = x < 3 || x >= WIDTH_PX - 3 || y < 3 || y >= HEIGHT - 3;
  const bar = (y >= 40 && y < 60) || (y >= 100 && y < 108) || (y >= 160 && y < 168);
  if (border || bar) setPx(x, y);
}

const esc = Buffer.from([0x1b, 0x40]);                 // ESC @ reset
const center = Buffer.from([0x1b, 0x61, 0x01]);        // ESC a 1 (center)
const header = Buffer.from([
  0x1d, 0x76, 0x30, 0x00,                              // GS v 0 m=0
  WIDTH_BYTES & 0xff, (WIDTH_BYTES >> 8) & 0xff,        // xL xH
  HEIGHT & 0xff, (HEIGHT >> 8) & 0xff,                  // yL yH
]);
const feed = Buffer.from([0x1b, 0x4a, 0x30]);          // ESC J 48 dots
const cut = Buffer.from([0x1d, 0x56, 0x00]);           // GS V 0

const job = Buffer.concat([esc, center, header, rows, feed, cut]);
mkdirSync(new URL("./fixtures", import.meta.url), { recursive: true });
writeFileSync(new URL("./fixtures/raster-basic.escpos", import.meta.url), job);
console.log(`wrote fixtures/raster-basic.escpos (${job.length} bytes)`);

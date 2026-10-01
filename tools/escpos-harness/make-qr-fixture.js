import { writeFileSync, mkdirSync } from "node:fs";

const url = process.argv[2] || "https://maratus.co/r/qr-fixture-demo";
const bytes = Buffer.from(url, "latin1");

// GS ( k helpers (cn = 49 for QR).
const gsk = (fn, params) => {
  const payload = Buffer.concat([Buffer.from([49, fn]), Buffer.from(params)]);
  const pL = payload.length & 0xff, pH = (payload.length >> 8) & 0xff;
  return Buffer.concat([Buffer.from([0x1d, 0x28, 0x6b, pL, pH]), payload]);
};

const setSize = gsk(67, [6]);                 // module size = 6 dots
const setEcc  = gsk(69, [49]);                // ECC = M
// store data: fn=80, m=48, then the data
const storeLen = bytes.length + 3;            // cn fn m
const store = Buffer.concat([
  Buffer.from([0x1d, 0x28, 0x6b, storeLen & 0xff, (storeLen >> 8) & 0xff, 49, 80, 48]),
  bytes,
]);
const print = gsk(81, [48]);                  // print

const job = Buffer.concat([
  Buffer.from([0x1b, 0x40]),                  // ESC @
  Buffer.from([0x1b, 0x61, 0x01]),            // center
  setSize, setEcc, store, print,
  Buffer.from([0x1b, 0x4a, 0x30]),            // feed
  Buffer.from([0x1d, 0x56, 0x00]),            // cut
]);

mkdirSync(new URL("./fixtures", import.meta.url), { recursive: true });
writeFileSync(new URL("./fixtures/qr-basic.escpos", import.meta.url), job);
console.log(`wrote fixtures/qr-basic.escpos (${job.length} bytes) encoding: ${url}`);

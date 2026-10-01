import { writeFileSync, mkdirSync } from "node:fs";

const code = process.argv[2] || "MARATUS-100423";
const data = Buffer.from(code, "latin1");

const b = [];
const push = (...xs) => xs.forEach((x) => b.push(typeof x === "string" ? Buffer.from(x, "latin1") : Buffer.from(x)));

push([0x1b, 0x40]);                 // ESC @
push([0x1b, 0x61, 0x01]);           // center
push("Order\n");
push([0x1d, 0x68, 0x50]);           // GS h 80  (height 80 dots)
push([0x1d, 0x77, 0x03]);           // GS w 3   (module width 3)
push([0x1d, 0x48, 0x02]);           // GS H 2   (HRI below)
// GS k 73 n d1..dn  (CODE128, length form)
push([0x1d, 0x6b, 0x49, data.length]); b.push(data);
push("\n", [0x1b, 0x4a, 0x40]);     // feed
push([0x1d, 0x56, 0x00]);           // cut

const job = Buffer.concat(b);
mkdirSync(new URL("./fixtures", import.meta.url), { recursive: true });
writeFileSync(new URL("./fixtures/barcode.escpos", import.meta.url), job);
console.log(`wrote fixtures/barcode.escpos (${job.length} bytes) encoding: ${code}`);

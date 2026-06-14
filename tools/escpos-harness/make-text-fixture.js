import { writeFileSync, mkdirSync } from "node:fs";

const b = [];
const push = (...xs) => xs.forEach((x) => b.push(typeof x === "string" ? Buffer.from(x, "latin1") : Buffer.from(x)));

push([0x1b, 0x40]);                       // ESC @ reset
push([0x1b, 0x61, 0x01]);                 // center
push([0x1d, 0x21, 0x11]);                 // GS ! double w+h
push("ROASTWELL\n");
push([0x1d, 0x21, 0x00]);                 // GS ! normal size
push([0x1b, 0x45, 0x01], "Coffee Bar\n", [0x1b, 0x45, 0x00]); // bold line
push([0x1b, 0x61, 0x00]);                 // left
push("--------------------------------\n");
push("1x Flat White            4.50\n");
push("1x Croissant             3.25\n");
push("--------------------------------\n");
push([0x1b, 0x61, 0x02]);                 // right
push([0x1b, 0x45, 0x01], "TOTAL  7.75\n", [0x1b, 0x45, 0x00]);
push([0x1b, 0x61, 0x01]);                 // center
push("\n", "Scan for your e-receipt\n");
// a QR too:
const url = Buffer.from("https://ditto.app/r/text-demo", "latin1");
const sl = url.length + 3;
push([0x1d, 0x28, 0x6b, 0x03, 0x00, 49, 67, 6]);                 // size 6
push([0x1d, 0x28, 0x6b, 0x03, 0x00, 49, 69, 49]);                // ecc M
push([0x1d, 0x28, 0x6b, sl & 0xff, (sl >> 8) & 0xff, 49, 80, 48]); b.push(url); // store
push([0x1d, 0x28, 0x6b, 0x03, 0x00, 49, 81, 48]);                // print
push([0x1b, 0x4a, 0x40]);                 // feed
push([0x1d, 0x56, 0x00]);                 // cut

const job = Buffer.concat(b);
mkdirSync(new URL("./fixtures", import.meta.url), { recursive: true });
writeFileSync(new URL("./fixtures/text-receipt.escpos", import.meta.url), job);
console.log(`wrote fixtures/text-receipt.escpos (${job.length} bytes)`);

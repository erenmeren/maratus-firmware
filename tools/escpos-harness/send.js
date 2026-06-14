#!/usr/bin/env node
import { readFileSync } from "node:fs";
import { createConnection } from "node:net";

const [, , host, file = new URL("./fixtures/raster-basic.escpos", import.meta.url).pathname] = process.argv;
if (!host) { console.error("usage: node send.js <device-ip> [fixture.escpos]"); process.exit(1); }

const data = readFileSync(file);
const sock = createConnection({ host, port: 9100 }, () => {
  console.log(`connected ${host}:9100, sending ${data.length} bytes`);
  sock.write(data, () => sock.end());            // end() closes -> device treats as job complete
});
sock.on("close", () => console.log("done"));
sock.on("error", (e) => { console.error("error:", e.message); process.exit(1); });

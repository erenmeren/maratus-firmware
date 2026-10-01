# Maratus ESC/POS test harness

Streams ESC/POS fixtures to the device's TCP :9100 (acts as a fake POS).

```bash
node make-fixture.js          # writes fixtures/raster-basic.escpos
node send.js <device-ip>      # streams it to the printer
```

`<device-ip>` is the IP from the device's `net: got IP` serial log.

The fixture is a centered 576px-wide raster (`GS v 0`) with a border + bars — no
fonts/QR needed, matching the M4a raster slice. Width must equal the firmware's
`RENDER_ROLL_WIDTH` (576).

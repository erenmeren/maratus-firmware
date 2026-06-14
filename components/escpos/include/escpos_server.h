#pragma once
#include <stddef.h>
#include <stdint.h>

// Called once per completed print job with the full received byte buffer.
// The buffer is valid only for the duration of the callback.
typedef void (*escpos_job_cb)(const uint8_t *data, size_t len);

// Start the TCP:9100 listener task. Each accepted connection's bytes are
// accumulated until the peer closes or an idle gap elapses, then `cb` is invoked.
void escpos_server_start(escpos_job_cb cb);

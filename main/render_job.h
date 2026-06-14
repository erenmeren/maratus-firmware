#pragma once
#include <stddef.h>
#include <stdint.h>

// escpos_job_cb-compatible: parse the job, render to PNG, upload, show the QR.
void render_job_handle(const uint8_t *data, size_t len);

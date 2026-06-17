#pragma once

// Derive a stable 40-char lowercase-hex cache key from an asset URL.
//
// The query string is stripped first (everything from the first '?' onward).
// Presigned R2 URLs for the same object differ ONLY in their query (X-Amz-*),
// so two presigned URLs for the same object produce an identical key — the key
// names the object, not the signed request. out must hold at least 41 bytes
// (40 hex chars + NUL). A NULL or empty url yields out[0]='\0'.
void asset_key(const char *url, char out[41]);

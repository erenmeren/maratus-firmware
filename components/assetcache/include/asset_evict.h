#pragma once

// Compute the set difference on_flash \ referenced — the cache keys present on
// flash that are no longer referenced by the current config and may be deleted.
//
// Pure: no filesystem, no allocation. out_delete is filled with pointers taken
// directly from the on_flash array (in on_flash order); the returned count is
// the number written, clamped to max_out. A key counts as referenced if its
// string value appears anywhere in referenced[]. NULL/zero-length inputs are
// treated as empty.
int asset_evict_list(const char *const *on_flash, int n_flash,
                     const char *const *referenced, int n_ref,
                     const char **out_delete, int max_out);

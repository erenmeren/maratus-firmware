# On-flash asset cache. asset_key.c / asset_evict.c are the pure core (also built
# by the host harness in tools/cfg-harness); sha1_mbedtls.c supplies the device
# sha1_hash seam; asset_cache.c is the LittleFS I/O + network-miss layer.
idf_component_register(SRCS "asset_key.c"
                            "asset_evict.c"
                            "sha1_mbedtls.c"
                            "asset_cache.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES cloud mbedtls)

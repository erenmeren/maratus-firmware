idf_component_register(SRCS "cfg_parse.c" "render_geom.c" "qr_style.c" "clock_format.c" "provisioning.c" "wifi_util.c" "ota_manifest.c" "sleep_policy.c" "sha256.c" "settings_pin.c" "gesture.c" "overrides.c" "brightness.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json
                       PRIV_REQUIRES esp_hw_support)

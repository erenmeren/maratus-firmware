idf_component_register(SRCS "ui.c" "ui_sheet.c" "font_cache.c" "ui_wifi.c" "ui_settings.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl esp_lvgl_port devcfg assets assetcache time_sync net)

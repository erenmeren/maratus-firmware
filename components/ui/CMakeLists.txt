idf_component_register(SRCS "ui.c" "font_cache.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl esp_lvgl_port devcfg assets assetcache)

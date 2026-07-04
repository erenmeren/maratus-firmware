idf_component_register(SRCS "app_main.c" "app_state.c" "wifi_setup.c"
                       INCLUDE_DIRS "."
                       REQUIRES ui net cloud ota appcfg assets devcfg assetcache nvs_flash time_sync esp_wifi)

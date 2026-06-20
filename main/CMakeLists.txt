idf_component_register(SRCS "app_main.c" "app_state.c" "render_job.c" "wifi_setup.c"
                       INCLUDE_DIRS "."
                       REQUIRES ui net cloud appcfg assets escpos render devcfg assetcache nvs_flash time_sync esp_wifi)

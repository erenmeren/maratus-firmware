idf_component_register(SRCS "cloud.c" "commands.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client esp-tls appcfg json ui esp_system)

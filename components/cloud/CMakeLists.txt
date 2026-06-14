idf_component_register(SRCS "cloud.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client esp-tls appcfg)

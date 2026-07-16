idf_component_register(
    SRCS "mqtt_client.c" "mqtt_topics.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp-tls esp_event appcfg devcfg json cloud
)

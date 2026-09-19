idf_component_register(
    SRCS main.c x_post.c
    INCLUDE_DIRS .
    REQUIRES panchangam swisseph nvs_flash esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls json
)

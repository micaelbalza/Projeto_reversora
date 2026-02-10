idf_component_register(
    SRCS 
        "main.c"
        "wifi_manager.c"
        "calibration.c"
        "web_server.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        esp_wifi
        esp_http_server
        esp_netif
        nvs_flash
        esp_event
        esp_common
        esp_rom
        esp_system
        freertos
        driver
        nvs
    PRIV_REQUIRES
        spi_flash
)

# Incorpora arquivo HTML como asset binário
target_add_binary_data(${COMPONENT_LIB} "index.html" BINARY)


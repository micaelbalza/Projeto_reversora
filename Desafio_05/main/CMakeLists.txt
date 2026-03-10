idf_component_register(
    SRCS 
        "main.c"
        "wifi_manager.c"
        "calibration.c"
        "web_server.c"
        "Include/joystick_hall.c"
        "Include/rpm_pulse_counter.c"
        "Include/mqtt_send_module.c"
    INCLUDE_DIRS 
        "."
        "Include"
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
        mqtt
        esp_timer
    PRIV_REQUIRES
        spi_flash
        json
)

# Incorpora arquivo HTML como asset binário
target_add_binary_data(${COMPONENT_LIB} "index.html" BINARY)


idf_component_register(
    SRCS
        "main.c"
        "Include/mqtt_send_module.c"
        "Include/joystick_hall.c"
        "Include/rpm_pulse_counter.c"
        "Include/micro_sd.c"
        "Include/ds3231.c"
    INCLUDE_DIRS
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
        fatfs 
        sdmmc 
        vfs 
        esp_driver_spi
        lwip
    PRIV_REQUIRES
        spi_flash
        json
)

# Incorpora arquivo HTML como asset binário
target_add_binary_data(${COMPONENT_LIB} "index.html" BINARY)


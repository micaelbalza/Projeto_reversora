idf_component_register(
    SRCS "DS1302Manager.c" "joystick_hall.c" "hello_world_main.c" "rpm_pulse_counter.c"
    PRIV_REQUIRES driver nvs_flash spi_flash
    REQUIRES
        esp_driver_gpio
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        spiffs
    INCLUDE_DIRS "."
)

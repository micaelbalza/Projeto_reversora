idf_component_register(
    SRCS "joystick_hall.c" "hello_world_main.c" "rpm_pulse_counter.c"
    PRIV_REQUIRES driver nvs_flash spi_flash
    REQUIRES
        esp_driver_gpio
        esp_timer
        spiffs
    INCLUDE_DIRS "."
)

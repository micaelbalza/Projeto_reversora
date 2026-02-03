idf_component_register(
    SRCS "joystick_hall.c" "hello_world_main.c"
    PRIV_REQUIRES driver nvs_flash spi_flash
    INCLUDE_DIRS "."
)

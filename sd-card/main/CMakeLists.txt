set(srcs)

idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES fatfs sdmmc vfs esp_driver_spi
                       WHOLE_ARCHIVE)

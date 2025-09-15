# PROJETO REVERSORA

## Guia para configurar a biblioteca fatfs no projeto

- [REPOSITÓRIO FATFS](https://github.com/elehobica/pico_fatfs/tree/main)

### Passo a passo

- Clonar **pico_sdk**, **pico-examples**, **pico-extras** e **pico-fatfs** **(MESMO NÍVEL DE DIRETÓRIO)**

- Criar as variáveis de ambiente **PICO_SDK**, **PICO_EXAMPLES** e **PICO_EXTRAS**

- Configurar o cmakelists:

```
add_subdirectory(${CMAKE_SOURCE_DIR}/../pico_fatfs pico_fatfs_build)

target_link_libraries(pot_mqtt
    pico_stdlib
    hardware_i2c
    hardware_adc
    hardware_spi
    hardware_clocks
    pico_cyw43_arch_lwip_threadsafe_background
    pico_fatfs
)
```



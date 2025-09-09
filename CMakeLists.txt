# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

cmake_minimum_required(VERSION 3.13)

# --- Board deve vir ANTES do SDK ---
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Importa o Pico SDK (usa PICO_SDK_PATH do ambiente)
include(pico_sdk_import.cmake)

project(pot_mqtt C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Inicializa o SDK
pico_sdk_init()

# ---------- Descoberta de fontes ----------
# (usa raiz ou src/ dependendo do que existir)
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/mqtt.c)
    set(MQTT_SRC src/mqtt.c)
else()
    set(MQTT_SRC mqtt.c)
endif()

if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/wifi_conn.c)
    set(WIFI_SRC src/wifi_conn.c)
else()
    set(WIFI_SRC wifi_conn.c)
endif()

# AHT10 (novo)
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/aht10.c)
    set(AHT10_SRC src/aht10.c)
elseif (EXISTS ${CMAKE_CURRENT_LIST_DIR}/aht10.c)
    set(AHT10_SRC aht10.c)
else()
    message(FATAL_ERROR "Arquivo 'aht10.c' não encontrado (procurei em ./src e ./).")
endif()

add_executable(pot_mqtt
    pot_mqtt.c
    inc/ssd1306_i2c.c
    ${MQTT_SRC}
    ${WIFI_SRC}
    ${AHT10_SRC}          # << incluímos o driver do AHT10
)

pico_set_program_name(pot_mqtt "pot_mqtt")
pico_set_program_version(pot_mqtt "0.1")

# STDIO
pico_enable_stdio_usb(pot_mqtt 1)
pico_enable_stdio_uart(pot_mqtt 0)

# Includes locais (raiz p/ lwipopts.h, + headers)
target_include_directories(pot_mqtt PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include   # aht10.h
    ${CMAKE_CURRENT_LIST_DIR}/inc       # ssd1306 headers
)

# Definições importantes
target_compile_definitions(pot_mqtt PRIVATE
    PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1
    LWIP_MQTT=1
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=2000
)

# ---------- Checagens de alvos do SDK ----------
if (NOT TARGET pico_cyw43_arch_lwip_threadsafe_background)
    message(FATAL_ERROR "Target 'pico_cyw43_arch_lwip_threadsafe_background' não encontrado.
    Verifique seu Pico SDK (PICO_SDK_PATH) e submódulos do cyw43/lwIP.")
endif()

# Linkagem base
target_link_libraries(pot_mqtt
    pico_stdlib
    hardware_i2c
    hardware_clocks
    pico_cyw43_arch_lwip_threadsafe_background
)

# MQTT do lwIP (se existir nesse SDK)
if (TARGET pico_lwip_mqtt)
    target_link_libraries(pot_mqtt pico_lwip_mqtt)
else()
    message(WARNING "Target 'pico_lwip_mqtt' não existe no seu SDK. 
    Em SDKs 2.2.0 oficiais ele deve existir. Se faltar, atualize submódulos:
      git submodule update --init --recursive
    e/ou atualize o Pico SDK.")
endif()

# Artefatos (UF2, bin, etc.)
pico_add_extra_outputs(pot_mqtt)

# Mensagens úteis
message(STATUS ">> Usando fontes: MQTT=${MQTT_SRC}  |  WIFI=${WIFI_SRC}  |  AHT10=${AHT10_SRC}")
message(STATUS ">> PICO_SDK_PATH = ${PICO_SDK_PATH}")

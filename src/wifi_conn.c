#include <stdio.h>
#include "pico/cyw43_arch.h"   // Driver Wi-Fi CYW43 (Pico W)
#include "wifi_conn.h"

int connect_to_wifi(const char *ssid, const char *password) {
    // Inicializa o driver do CYW43 (pilha lwIP é habilitada pelo arch selecionado no CMake).
    if (cyw43_arch_init()) {
        printf("[WiFi] Erro ao iniciar driver CYW43\n");
        return -1;
    }

    // Modo estação (cliente)
    cyw43_arch_enable_sta_mode();

    // Tenta conectar (timeout 30s). Usa WPA2 AES-PSK.
    int rc = cyw43_arch_wifi_connect_timeout_ms(
        ssid,
        password,
        CYW43_AUTH_WPA2_AES_PSK,
        30000
    );

    if (rc) {
        printf("[WiFi] Falha ao conectar em '%s' (rc=%d)\n", ssid, rc);
        return -1;
    }

    printf("[WiFi] Conectado em '%s'\n", ssid);
    return 0;
}

void wifi_shutdown(void) {
    // Melhor esforço: desconecta se estiver conectado e desliga o driver
    // (as APIs de disconnect dependem do estado interno; cyw43_arch_deinit()
    // já cuida de limpar recursos).
    cyw43_arch_deinit();
    printf("[WiFi] Driver encerrado\n");
}

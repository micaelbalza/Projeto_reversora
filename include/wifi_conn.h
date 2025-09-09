#ifndef WIFI_CONN_H
#define WIFI_CONN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa o Wi-Fi da Pico W e conecta em modo estação (STA).
 *
 * @param ssid     Nome da rede Wi-Fi (AP).
 * @param password Senha da rede (WPA2).
 * @return 0 em sucesso; -1 em erro.
 */
int connect_to_wifi(const char *ssid, const char *password);

/**
 * Desconecta do AP e desliga o driver Wi-Fi.
 * Use se quiser encerrar a pilha Wi-Fi antes de dormir/desligar.
 */
void wifi_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_CONN_H

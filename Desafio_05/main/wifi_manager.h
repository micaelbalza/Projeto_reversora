#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

/**
 * @brief Estados da conexão Wi-Fi
 */
typedef enum {
    WIFI_DISCONNECTED = 0,
    WIFI_CONNECTING = 1,
    WIFI_AP_MODE = 2,
    WIFI_STA_CONNECTED = 3,
    WIFI_ERROR = 4
} wifi_state_t;

/**
 * @brief Callback para mudanças de estado Wi-Fi
 */
typedef void (*wifi_state_callback_t)(wifi_state_t state, const char *ip);

/**
 * @brief Inicializa o gerenciador de Wi-Fi
 * @return ESP_OK se bem-sucedido
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Registra callback para mudanças de estado
 * @param callback Função callback
 * @return ESP_OK se bem-sucedido
 */
esp_err_t wifi_manager_register_callback(wifi_state_callback_t callback);

/**
 * @brief Configura credenciais Wi-Fi e conecta como STA
 * @param ssid SSID da rede
 * @param password Senha da rede
 * @return ESP_OK se bem-sucedido
 */
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);

/**
 * @brief Obtém o estado atual do Wi-Fi
 * @return Estado atual
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Obtém o IP atual (STA ou AP)
 * @return Ponteiro para string com IP (válido até próxima chamada)
 */
const char *wifi_manager_get_ip(void);

/**
 * @brief Limpa credenciais salvas e volta para modo AP
 * @return ESP_OK se bem-sucedido
 */
esp_err_t wifi_manager_reset_credentials(void);

/**
 * @brief Inicia modo Access Point
 * @return ESP_OK se bem-sucedido
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief Inicia conexão STA com credenciais salvas
 * @return ESP_OK se bem-sucedido
 */
esp_err_t wifi_manager_start_sta(void);

#endif // WIFI_MANAGER_H

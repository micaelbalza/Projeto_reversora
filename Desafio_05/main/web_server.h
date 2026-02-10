#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Inicializa servidor HTTP
 * @return Handle do servidor ou NULL se falhar
 */
httpd_handle_t web_server_init(void);

/**
 * @brief Desliga servidor HTTP
 * @param server Handle do servidor
 * @return ESP_OK se bem-sucedido
 */
esp_err_t web_server_deinit(httpd_handle_t server);

/**
 * @brief Serve arquivo web estático
 * @param server Handle do servidor
 * @return ESP_OK se bem-sucedido
 */
esp_err_t web_server_register_routes(httpd_handle_t server);

#endif // WEB_SERVER_H

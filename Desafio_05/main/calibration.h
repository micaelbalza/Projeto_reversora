#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "esp_err.h"

/**
 * @brief Estados da calibração
 */
typedef enum {
    CALIB_IDLE = 0,           // Não iniciada
    CALIB_WAIT_NEUTRAL = 1,   // Aguardando posição neutra
    CALIB_NEUTRAL_DONE = 2,   // Neutra confirmada
    CALIB_WAIT_FORWARD = 3,   // Aguardando posição avante
    CALIB_FORWARD_DONE = 4,   // Avante confirmada
    CALIB_WAIT_REVERSE = 5,   // Aguardando posição ré
    CALIB_REVERSE_DONE = 6,   // Ré confirmada
    CALIB_FINALIZING = 7,     // Finalizando
    CALIB_COMPLETED = 8,      // Concluída com sucesso
    CALIB_ERROR = 9           // Erro
} calibration_state_t;

/**
 * @brief Callback para mudanças de estado da calibração
 */
typedef void (*calib_state_callback_t)(calibration_state_t state);

/**
 * @brief Inicializa o módulo de calibração
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_init(void);

/**
 * @brief Registra callback para mudanças de estado
 * @param callback Função callback
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_register_callback(calib_state_callback_t callback);

/**
 * @brief Inicia processo de calibração
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_start(void);

/**
 * @brief Confirma posição neutra
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_confirm_neutral(void);

/**
 * @brief Confirma posição avante
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_confirm_forward(void);

/**
 * @brief Confirma posição ré
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_confirm_reverse(void);

/**
 * @brief Finaliza calibração
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_finish(void);

/**
 * @brief Reseta calibração para estado inicial
 * @return ESP_OK se bem-sucedido
 */
esp_err_t calibration_reset(void);

/**
 * @brief Obtém estado atual da calibração
 * @return Estado atual
 */
calibration_state_t calibration_get_state(void);

/**
 * @brief Obtém descrição textual do estado
 * @param state Estado
 * @return String com descrição
 */
const char *calibration_get_state_string(calibration_state_t state);

/**
 * @brief Obtém percentual de progresso
 * @return Percentual (0-100)
 */
int calibration_get_progress(void);

#endif // CALIBRATION_H

#include "calibration.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CALIBRATION";

static calibration_state_t current_state = CALIB_IDLE;
static calib_state_callback_t state_callback = NULL;

/**
 * @brief Atualiza estado e chama callback
 */
static void calibration_update_state(calibration_state_t state) {
    current_state = state;
    ESP_LOGI(TAG, "Estado: %s", calibration_get_state_string(state));
    
    if (state_callback) {
        state_callback(state);
    }
}

/**
 * @brief Valida transição de estado
 */
static esp_err_t calibration_validate_transition(calibration_state_t target) {
    switch (current_state) {
        case CALIB_IDLE:
            // De IDLE só pode ir para WAIT_NEUTRAL
            if (target != CALIB_WAIT_NEUTRAL) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_WAIT_NEUTRAL:
            // De WAIT_NEUTRAL só pode confirmar
            if (target != CALIB_NEUTRAL_DONE) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_NEUTRAL_DONE:
            // De NEUTRAL_DONE vai para WAIT_FORWARD
            if (target != CALIB_WAIT_FORWARD) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_WAIT_FORWARD:
            // De WAIT_FORWARD só pode confirmar
            if (target != CALIB_FORWARD_DONE) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_FORWARD_DONE:
            // De FORWARD_DONE vai para WAIT_REVERSE
            if (target != CALIB_WAIT_REVERSE) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_WAIT_REVERSE:
            // De WAIT_REVERSE só pode confirmar
            if (target != CALIB_REVERSE_DONE) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_REVERSE_DONE:
            // De REVERSE_DONE vai para FINALIZING
            if (target != CALIB_FINALIZING) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_FINALIZING:
            // De FINALIZING vai para COMPLETED
            if (target != CALIB_COMPLETED && target != CALIB_ERROR) return ESP_ERR_INVALID_STATE;
            break;
            
        case CALIB_COMPLETED:
        case CALIB_ERROR:
            // Desses estados só pode resetar
            return ESP_ERR_INVALID_STATE;
            
        default:
            return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

/**
 * @brief Implementação das funções de calibração reais
 * Estas funções devem ser substituídas pela lógica do hardware real
 */
static void calibrate_neutral(void) {
    ESP_LOGI(TAG, "Executando calibração neutra...");
    // Aqui seria chamada a função real da reversora
    // calibrate_neutral_hw();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Simula execução
    ESP_LOGI(TAG, "Calibração neutra concluída");
}

static void calibrate_forward(void) {
    ESP_LOGI(TAG, "Executando calibração avante...");
    // calibrate_forward_hw();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Calibração avante concluída");
}

static void calibrate_reverse(void) {
    ESP_LOGI(TAG, "Executando calibração ré...");
    // calibrate_reverse_hw();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Calibração ré concluída");
}

static void calibrate_finish(void) {
    ESP_LOGI(TAG, "Finalizando calibração...");
    // calibrate_finish_hw();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Calibração finalizada com sucesso");
}

/**
 * @brief Inicializa o módulo
 */
esp_err_t calibration_init(void) {
    ESP_LOGI(TAG, "Inicializando módulo de calibração");
    current_state = CALIB_IDLE;
    return ESP_OK;
}

/**
 * @brief Registra callback
 */
esp_err_t calibration_register_callback(calib_state_callback_t callback) {
    state_callback = callback;
    return ESP_OK;
}

/**
 * @brief Inicia calibração
 */
esp_err_t calibration_start(void) {
    if (current_state != CALIB_IDLE) {
        ESP_LOGE(TAG, "Calibração já iniciada ou em progresso");
        return ESP_ERR_INVALID_STATE;
    }
    
    calibration_update_state(CALIB_WAIT_NEUTRAL);
    return ESP_OK;
}

/**
 * @brief Confirma neutra
 */
esp_err_t calibration_confirm_neutral(void) {
    if (calibration_validate_transition(CALIB_NEUTRAL_DONE) != ESP_OK) {
        ESP_LOGE(TAG, "Não é permitido confirmar neutra neste momento");
        return ESP_ERR_INVALID_STATE;
    }
    
    calibrate_neutral();
    calibration_update_state(CALIB_NEUTRAL_DONE);
    calibration_update_state(CALIB_WAIT_FORWARD);
    
    return ESP_OK;
}

/**
 * @brief Confirma avante
 */
esp_err_t calibration_confirm_forward(void) {
    if (calibration_validate_transition(CALIB_FORWARD_DONE) != ESP_OK) {
        ESP_LOGE(TAG, "Não é permitido confirmar avante neste momento");
        return ESP_ERR_INVALID_STATE;
    }
    
    calibrate_forward();
    calibration_update_state(CALIB_FORWARD_DONE);
    calibration_update_state(CALIB_WAIT_REVERSE);
    
    return ESP_OK;
}

/**
 * @brief Confirma ré
 */
esp_err_t calibration_confirm_reverse(void) {
    if (calibration_validate_transition(CALIB_REVERSE_DONE) != ESP_OK) {
        ESP_LOGE(TAG, "Não é permitido confirmar ré neste momento");
        return ESP_ERR_INVALID_STATE;
    }
    
    calibrate_reverse();
    calibration_update_state(CALIB_REVERSE_DONE);
    calibration_update_state(CALIB_FINALIZING);
    
    return ESP_OK;
}

/**
 * @brief Finaliza calibração
 */
esp_err_t calibration_finish(void) {
    if (current_state != CALIB_FINALIZING) {
        ESP_LOGE(TAG, "Não é permitido finalizar neste momento");
        return ESP_ERR_INVALID_STATE;
    }
    
    calibrate_finish();
    calibration_update_state(CALIB_COMPLETED);
    
    return ESP_OK;
}

/**
 * @brief Reseta calibração
 */
esp_err_t calibration_reset(void) {
    if (current_state == CALIB_COMPLETED || current_state == CALIB_ERROR) {
        calibration_update_state(CALIB_IDLE);
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Tentativa de reset durante calibração ativa");
    return ESP_ERR_INVALID_STATE;
}

/**
 * @brief Obtém estado
 */
calibration_state_t calibration_get_state(void) {
    return current_state;
}

/**
 * @brief Obtém string do estado
 */
const char *calibration_get_state_string(calibration_state_t state) {
    switch (state) {
        case CALIB_IDLE:
            return "IDLE";
        case CALIB_WAIT_NEUTRAL:
            return "AGUARDANDO_NEUTRA";
        case CALIB_NEUTRAL_DONE:
            return "NEUTRA_CONCLUIDA";
        case CALIB_WAIT_FORWARD:
            return "AGUARDANDO_AVANTE";
        case CALIB_FORWARD_DONE:
            return "AVANTE_CONCLUIDA";
        case CALIB_WAIT_REVERSE:
            return "AGUARDANDO_RE";
        case CALIB_REVERSE_DONE:
            return "RE_CONCLUIDA";
        case CALIB_FINALIZING:
            return "FINALIZANDO";
        case CALIB_COMPLETED:
            return "CONCLUIDA";
        case CALIB_ERROR:
            return "ERRO";
        default:
            return "DESCONHECIDO";
    }
}

/**
 * @brief Obtém progresso
 */
int calibration_get_progress(void) {
    switch (current_state) {
        case CALIB_IDLE:
            return 0;
        case CALIB_WAIT_NEUTRAL:
            return 5;
        case CALIB_NEUTRAL_DONE:
            return 25;
        case CALIB_WAIT_FORWARD:
            return 30;
        case CALIB_FORWARD_DONE:
            return 50;
        case CALIB_WAIT_REVERSE:
            return 55;
        case CALIB_REVERSE_DONE:
            return 75;
        case CALIB_FINALIZING:
            return 90;
        case CALIB_COMPLETED:
            return 100;
        case CALIB_ERROR:
            return 0;
        default:
            return 0;
    }
}

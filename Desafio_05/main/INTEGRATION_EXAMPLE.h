/*
 * EXEMPLO DE INTEGRAÇÃO COM HARDWARE REAL
 * 
 * Este arquivo demonstra como integrar suas funções de calibração reais
 * no módulo de calibração do sistema.
 * 
 * Instruções:
 * 1. Copie suas implementações das funções de calibração abaixo
 * 2. Substitua as simulações em calibration.c
 * 3. Recompile o projeto
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HARDWARE_CALIB";

/* ============================================================
   EXEMPLO 1: Usando GPIO para acionar motores/solenoides
   ============================================================ */

// Defina os pinos conforme sua placa
#define MOTOR_NEUTRAL_PIN GPIO_NUM_4
#define MOTOR_FORWARD_PIN GPIO_NUM_5
#define MOTOR_REVERSE_PIN GPIO_NUM_18

void example_init_gpio_calibration(void) {
    // Configuração de GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_NEUTRAL_PIN) | 
                        (1ULL << MOTOR_FORWARD_PIN) | 
                        (1ULL << MOTOR_REVERSE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Inicializa todos os pinos em LOW
    gpio_set_level(MOTOR_NEUTRAL_PIN, 0);
    gpio_set_level(MOTOR_FORWARD_PIN, 0);
    gpio_set_level(MOTOR_REVERSE_PIN, 0);
    
    ESP_LOGI(TAG, "GPIO calibration inicializado");
}

void example_calibrate_neutral_gpio(void) {
    ESP_LOGI(TAG, "Acionando motor para posição NEUTRA");
    
    // Desativa todas as posições
    gpio_set_level(MOTOR_FORWARD_PIN, 0);
    gpio_set_level(MOTOR_REVERSE_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Ativa neutra
    gpio_set_level(MOTOR_NEUTRAL_PIN, 1);
    ESP_LOGI(TAG, "Motor em NEUTRA - GPIO %d ativado", MOTOR_NEUTRAL_PIN);
    
    // Mantém por tempo de estabilização
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Desativa
    gpio_set_level(MOTOR_NEUTRAL_PIN, 0);
    ESP_LOGI(TAG, "Calibração NEUTRA concluída");
}

void example_calibrate_forward_gpio(void) {
    ESP_LOGI(TAG, "Acionando motor para posição AVANTE");
    
    gpio_set_level(MOTOR_NEUTRAL_PIN, 0);
    gpio_set_level(MOTOR_REVERSE_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    gpio_set_level(MOTOR_FORWARD_PIN, 1);
    ESP_LOGI(TAG, "Motor em AVANTE - GPIO %d ativado", MOTOR_FORWARD_PIN);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    gpio_set_level(MOTOR_FORWARD_PIN, 0);
    ESP_LOGI(TAG, "Calibração AVANTE concluída");
}

void example_calibrate_reverse_gpio(void) {
    ESP_LOGI(TAG, "Acionando motor para posição RÉ");
    
    gpio_set_level(MOTOR_NEUTRAL_PIN, 0);
    gpio_set_level(MOTOR_FORWARD_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    gpio_set_level(MOTOR_REVERSE_PIN, 1);
    ESP_LOGI(TAG, "Motor em RÉ - GPIO %d ativado", MOTOR_REVERSE_PIN);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    gpio_set_level(MOTOR_REVERSE_PIN, 0);
    ESP_LOGI(TAG, "Calibração RÉ concluída");
}

/* ============================================================
   EXEMPLO 2: Usando PWM para controle de velocidade
   ============================================================ */

#include "driver/ledc.h"

void example_init_pwm_calibration(void) {
    // Configuração de PWM
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };
    ledc_timer_config(&ledc_timer);
    
    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = GPIO_NUM_12,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);
    
    ESP_LOGI(TAG, "PWM calibration inicializado");
}

void example_calibrate_neutral_pwm(void) {
    ESP_LOGI(TAG, "Calibração NEUTRA com PWM");
    
    // Aumenta duty gradualmente
    for (int i = 0; i <= 100; i += 10) {
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, i);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Mantém na posição
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Reduz gradualmente
    for (int i = 100; i >= 0; i -= 10) {
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, i);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Calibração NEUTRA concluída");
}

/* ============================================================
   EXEMPLO 3: Usando ADC com feedback de posição
   ============================================================ */

void example_init_adc_calibration(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_0);
}

int example_read_position_adc(void) {
    return adc1_get_raw(ADC1_CHANNEL_0);
}

void example_calibrate_with_feedback(void) {
    ESP_LOGI(TAG, "Calibração com feedback de posição");
    
    int neutral_value = 0;
    int samples = 10;
    
    // Coleta média de amostras
    for (int i = 0; i < samples; i++) {
        neutral_value += example_read_position_adc();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    neutral_value /= samples;
    
    ESP_LOGI(TAG, "Valor de posição neutra registrado: %d", neutral_value);
}

/* ============================================================
   EXEMPLO 4: Usando I2C para sensores de posição
   ============================================================ */

#include "driver/i2c.h"

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_FREQ_HZ 100000

void example_init_i2c_calibration(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    
    ESP_LOGI(TAG, "I2C calibration inicializado");
}

int example_read_i2c_sensor(uint8_t addr, uint8_t reg) {
    uint8_t data = 0;
    i2c_master_write_read_device(I2C_MASTER_NUM, addr,
                                  &reg, 1, &data, 1, 1000 / portTICK_RATE_MS);
    return data;
}

/* ============================================================
   INTEGRAÇÃO NO MÓDULO DE CALIBRAÇÃO
   ============================================================ */

/*
 * PASSO 1: Em calibration.c, nas funções estáticas:
 * 
 * Substitua:
 * 
 * static void calibrate_neutral(void) {
 *     ESP_LOGI(TAG, "Executando calibração neutra...");
 *     vTaskDelay(pdMS_TO_TICKS(1000));
 *     ESP_LOGI(TAG, "Calibração neutra concluída");
 * }
 * 
 * Por:
 * 
 * static void calibrate_neutral(void) {
 *     example_calibrate_neutral_gpio();
 *     // ou
 *     example_calibrate_neutral_pwm();
 * }
 * 
 * PASSO 2: Em calibration_init(), adicione inicialização:
 * 
 * example_init_gpio_calibration();
 * // ou
 * example_init_pwm_calibration();
 * // ou
 * example_init_adc_calibration();
 * // ou
 * example_init_i2c_calibration();
 * 
 * PASSO 3: Recompile com: idf.py build
 */

/* ============================================================
   CHECKLIST DE INTEGRAÇÃO
   ============================================================ */

/*
 * [ ] Definer pinos GPIO corretos para seu hardware
 * [ ] Implementar funções de inicialização apropriadas
 * [ ] Integrar com calibrate_neutral(), forward(), reverse()
 * [ ] Testar cada função individualmente
 * [ ] Verificar logs UART durante execução
 * [ ] Adicionar tratamento de erros conforme necessário
 * [ ] Documentar valores e limites usados
 * [ ] Validar sequência completa de calibração
 */

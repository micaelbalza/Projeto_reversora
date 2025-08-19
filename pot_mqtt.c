#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "inc/ssd1306.h"

//configuração do pino adc para o potenciometro
const int gpio_pin_pot = 28;
const int adc_channel_pot = 2;

// Configuração do display OLED 
#define I2C_SDA_PIN 14 
#define I2C_SCL_PIN 15
#define OLED_WIDTH 128 
#define OLED_HEIGHT 32 

void oled_setup() { 
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    ssd1306_init();     
}

void show_message_oled(char* message[], int lines) {
    struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };

    calculate_render_area_buffer_length(&frame_area);
    // limpa o display
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    int y = 0;

    for (uint i = 0; i < lines; i++) {
        printf("imprimindo na tela: %s\n", message[i]);
        ssd1306_draw_string(ssd,5,y,message[i]);
        // movendo para a proxima "linha" no display
        y += 8;
    }
    render_on_display(ssd, &frame_area);
}

int main()
{
    stdio_init_all();
    oled_setup();
    adc_init();
    adc_select_input(adc_channel_pot);
    uint16_t adc_raw = adc_read();
        
    // Converte para voltagem (assumindo 3.3V como referência)
    float voltage = adc_raw * (3.3f / 4095.0f);
        
    // Converte para porcentagem (0-100%)
    float percentage = (adc_raw / 4095.0f) * 100.0f;

    // valores padrao
    char s_volt[15] = "volt: 3.25V";
    char s_perc[15] = "perc: 20";
    char s_raw[15] = "raw: 300";    
    
    while (true) {
        adc_raw = adc_read();
        voltage = adc_raw * (3.3f / 4095.0f);
        percentage = (adc_raw / 4095.0f) * 100.0f;
        // formatando para melhor visualizar no oled.
        // volt com 2 casas decimais
        sprintf(s_volt, "volt: %.2f", voltage);
        // porcentagem de 0 a 100
        sprintf(s_perc, "perc: %d", (int)percentage);
        // raw valor minimo de de 12 e maximo de 3300
        sprintf(s_raw, "raw: %d", adc_raw);
        char texto_input[20] = "volt : 3.25V";

        char *showing_text[] = {
            s_raw,
            s_volt,
            s_perc
        };
        show_message_oled(showing_text,3);
        printf("ADC Raw: %d | Voltagem: %.2fV | Porcentagem: %.1f%%\n", adc_raw, voltage, percentage);
        sleep_ms(500);
    }
}

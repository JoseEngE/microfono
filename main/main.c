#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

static const char *TAG = "INMP441";

/* Configuración de los pines I2S */
#define I2S_WS_IO 11
#define I2S_BCK_IO 12
#define I2S_DI_IO 10

#define SAMPLE_RATE 16000

i2s_chan_handle_t rx_chan;

void i2s_init(void) {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

void app_main(void) {
    ESP_LOGI(TAG, "Inicializando micrófono INMP441 (Solo lectura a Serial)...");
    
    i2s_init();
    
    ESP_LOGI(TAG, "I2S inicializado. Comenzando la lectura...");

    // Buffer para leer 512 muestras de 32 bits
    size_t bytes_read = 0;
    const int num_samples = 512;
    int32_t *i2s_read_buff = (int32_t *) calloc(num_samples, sizeof(int32_t));

    if (i2s_read_buff == NULL) {
        ESP_LOGE(TAG, "Error asignando memoria para el buffer I2S");
        return;
    }

    while (1) {
        esp_err_t err = i2s_channel_read(rx_chan, i2s_read_buff, num_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        
        if (err == ESP_OK && bytes_read > 0) {
            int samples_read = bytes_read / sizeof(int32_t);
            long long sum = 0;
            
            // Procesar las muestras
            for (int i = 0; i < samples_read; i++) {
                // El INMP441 envía datos de 24 bits rellenados a 32 bits.
                // Movemos los bits a la derecha para quedarnos con un valor manejable
                int32_t sample = i2s_read_buff[i] >> 14; 
                
                // Sumar el valor absoluto (amplitud)
                sum += abs((int)sample);
            }
            
            // Calcular el promedio de la amplitud en este bloque de audio
            long average_amplitude = sum / samples_read;
            
            // Imprimir la amplitud promedio para poder visualizarla en el Serial Plotter
            printf("%ld\n", average_amplitude);
            
        } else {
            ESP_LOGE(TAG, "Error al leer del I2S");
        }
        
        // Pequeño delay para permitir que otras tareas del sistema se ejecuten
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    free(i2s_read_buff);
}

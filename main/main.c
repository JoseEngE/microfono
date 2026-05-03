#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

static const char *TAG = "REC_PLAY";

/* Configuración de los pines I2S Micrófono (INMP441) */
#define I2S_MIC_WS_IO 11
#define I2S_MIC_BCK_IO 12
#define I2S_MIC_DI_IO 10

/* Configuración de los pines I2S Altavoz (MAX98357A) */
#define I2S_SPK_BCLK_IO 4
#define I2S_SPK_WS_IO 5
#define I2S_SPK_DOUT_IO 18

/* Frecuencia de muestreo estándar (32000 Hz) para que el altavoz sincronice perfecto */
#define SAMPLE_RATE 32000

/* Tiempo de grabación en segundos. (1 seg = 128 KB de RAM aprox) */
#define RECORD_TIME_SEC 1

i2s_chan_handle_t rx_chan; // Handle del Micrófono
i2s_chan_handle_t tx_chan; // Handle del Altavoz

void mic_init(void) {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_MIC_BCK_IO,
            .ws = I2S_MIC_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_MIC_DI_IO,
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

void spk_init(void) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SPK_BCLK_IO,
            .ws = I2S_SPK_WS_IO,
            .dout = I2S_SPK_DOUT_IO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void app_main(void) {
    ESP_LOGI(TAG, "Inicializando Micrófono INMP441...");
    mic_init();
    
    ESP_LOGI(TAG, "Inicializando Altavoz MAX98357A...");
    spk_init();
    
    // Calculamos el tamaño del buffer necesario para almacenar el audio
    const int total_samples = SAMPLE_RATE * RECORD_TIME_SEC;
    
    // El I2S del micrófono es Mono pero nos dará muestras de 32 bits (INMP441).
    // Nosotros las procesaremos a 16 bits y las duplicaremos para hacer un falso Estéreo
    // porque el altavoz MAX98357A suele reproducir Left+Right (o uno de los dos).
    // Buffer = (Total muestras) * (2 canales L y R) * (2 bytes por muestra (16 bit))
    int16_t *audio_buffer = (int16_t *) calloc(total_samples * 2, sizeof(int16_t));
    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "No hay memoria RAM suficiente para el buffer de grabacion.");
        return;
    }

    // Buffer temporal para leer fragmentos pequeños desde el micrófono (32 bits)
    const int chunk_samples = 512;
    int32_t *i2s_read_buff = (int32_t *) calloc(chunk_samples, sizeof(int32_t));
    if (i2s_read_buff == NULL) {
        ESP_LOGE(TAG, "Error asignando memoria para fragmentos de lectura.");
        free(audio_buffer);
        return;
    }

    while (1) {
        // -------------------------
        // FASE 1: GRABAR
        // -------------------------
        ESP_LOGI(TAG, ">>> GRABANDO (%d SEGUNDOS) <<<", RECORD_TIME_SEC);
        size_t samples_recorded = 0;
        
        while (samples_recorded < total_samples) {
            size_t bytes_read = 0;
            // Leemos del I2S del micrófono
            esp_err_t err = i2s_channel_read(rx_chan, i2s_read_buff, chunk_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
            
            if (err == ESP_OK && bytes_read > 0) {
                int samples_read = bytes_read / sizeof(int32_t);
                
                // Procesamos y guardamos las muestras obtenidas
                for (int i = 0; i < samples_read; i++) {
                    if (samples_recorded >= total_samples) break; // Buffer principal lleno
                    
                    // El ESP32 lee 32 bits, pero el INMP441 envía 24 bits pegados a la izquierda.
                    int32_t raw_sample = i2s_read_buff[i] >> 16; 
                    
                    // 1. Filtro para remover el Offset DC (High-Pass Filter simple)
                    // Muchos micrófonos I2S tienen un ligero voltaje "base" (DC offset).
                    // Si lo amplificamos sin quitarlo, la señal se distorsiona horriblemente.
                    static int32_t dc_offset = 0;
                    dc_offset = (dc_offset * 127 + raw_sample) / 128;
                    int32_t sample32 = raw_sample - dc_offset;
                    
                    // 2. Aumentar el volumen de forma más segura (x4 o x6)
                    sample32 *= 4; 
                    
                    // 3. Limitamos para evitar saturación (clipping)
                    if (sample32 > 32767) sample32 = 32767;
                    if (sample32 < -32768) sample32 = -32768;
                    
                    int16_t sample16 = (int16_t)sample32;
                    
                    // Guardamos la muestra en el buffer, duplicándola (Estéreo: [L, R, L, R...])
                    audio_buffer[samples_recorded * 2] = sample16;     // Left
                    audio_buffer[samples_recorded * 2 + 1] = sample16; // Right
                    
                    samples_recorded++;
                }
            } else {
                ESP_LOGE(TAG, "Error al leer datos del I2S.");
            }
        }
        
        // -------------------------
        // FASE 2: REPRODUCIR
        // -------------------------
        ESP_LOGI(TAG, ">>> REPRODUCIENDO <<<");
        size_t bytes_written = 0;
        
        // Escribimos el buffer completo hacia el I2S del altavoz usando i2s_channel_write
        // Tamaño en bytes a enviar = total_samples * 2 (estéreo) * 2 bytes (int16_t)
        i2s_channel_write(tx_chan, audio_buffer, total_samples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        
        ESP_LOGI(TAG, "Finalizado. Esperando 2 segundos...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

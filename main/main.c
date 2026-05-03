#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>


static const char *TAG = "REC_PLAY";

/* Configuración de los pines I2S Micrófono (INMP441) */
#define I2S_MIC_WS_IO 11
#define I2S_MIC_BCK_IO 12
#define I2S_MIC_DI_IO 10

/* Configuración de los pines I2S Altavoz (MAX98357A) */
#define I2S_SPK_BCLK_IO 4
#define I2S_SPK_WS_IO 5
#define I2S_SPK_DOUT_IO 18

/* Frecuencia de muestreo bajada a 16000 Hz para probar mejoras de audio */
#define SAMPLE_RATE 16000

i2s_chan_handle_t rx_chan; // Handle del Micrófono
i2s_chan_handle_t tx_chan; // Handle del Altavoz

void mic_init(void) {
  i2s_chan_config_t rx_chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

  i2s_std_config_t rx_std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_MIC_BCK_IO,
              .ws = I2S_MIC_WS_IO,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_MIC_DI_IO,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

void spk_init(void) {
  i2s_chan_config_t tx_chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_SPK_BCLK_IO,
              .ws = I2S_SPK_WS_IO,
              .dout = I2S_SPK_DOUT_IO,
              .din = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
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

  // Buffer temporal para leer fragmentos desde el micrófono (Stereo 32-bits)
  const int chunk_samples = 512; // Número de muestras por lectura
  int32_t *i2s_read_buff = (int32_t *)calloc(chunk_samples, sizeof(int32_t));
  
  // Buffer temporal para enviar al altavoz (Mono 16-bits)
  // Como leemos estéreo pero guardamos mono, el tamaño de salida es la mitad
  int16_t *i2s_write_buff = (int16_t *)calloc(chunk_samples / 2, sizeof(int16_t));

  if (i2s_read_buff == NULL || i2s_write_buff == NULL) {
    ESP_LOGE(TAG, "Error asignando memoria para buffers.");
    return;
  }

  ESP_LOGI(TAG, ">>> MODO INTERCOMUNICADOR INICIADO <<<");
  ESP_LOGI(TAG, "Habla al micrófono. El audio se reproducirá en tiempo real.");

  while (1) {
    size_t bytes_read = 0;
    
    // 1. Leer del Micrófono (Estéreo, 32 bits)
    esp_err_t err = i2s_channel_read(rx_chan, i2s_read_buff,
                                     chunk_samples * sizeof(int32_t),
                                     &bytes_read, portMAX_DELAY);

    if (err == ESP_OK && bytes_read > 0) {
      int samples_read = bytes_read / sizeof(int32_t);
      int write_idx = 0;

      // 2. Procesar datos: Extraer canal izquierdo (i += 2)
      for (int i = 0; i < samples_read; i += 2) {
        // Desplazar 14 bits (preserva volumen y calidad)
        int32_t raw_sample = i2s_read_buff[i] >> 14;

        // Filtro para remover el Offset DC
        static int32_t dc_offset = 0;
        dc_offset = (dc_offset * 127 + raw_sample) / 128;
        int32_t sample32 = raw_sample - dc_offset;

        // Limitador para evitar saturación
        if (sample32 > 32767) sample32 = 32767;
        if (sample32 < -32768) sample32 = -32768;

        // Guardar la muestra Mono de 16-bits en el buffer de escritura
        i2s_write_buff[write_idx++] = (int16_t)sample32;
      }

      // 3. Escribir al Altavoz (Mono, 16 bits)
      size_t bytes_written = 0;
      i2s_channel_write(tx_chan, i2s_write_buff,
                        write_idx * sizeof(int16_t),
                        &bytes_written, portMAX_DELAY);
    }
  }
}

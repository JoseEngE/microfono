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

/* Tiempo de grabación vuelto a 5 segundos. */
#define RECORD_TIME_SEC 5

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
                                                      I2S_SLOT_MODE_STEREO),
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

  // Calculamos el tamaño del buffer necesario para almacenar el audio
  const int total_samples = SAMPLE_RATE * RECORD_TIME_SEC;

  // Almacenamos en Mono para ahorrar RAM
  // Buffer = (Total muestras) * (2 bytes por muestra (16 bit))
  int16_t *audio_buffer = (int16_t *)calloc(total_samples, sizeof(int16_t));
  if (audio_buffer == NULL) {
    ESP_LOGE(TAG, "No hay memoria RAM suficiente para el buffer de grabacion.");
    return;
  }

  // Buffer temporal para leer fragmentos pequeños desde el micrófono (32 bits)
  const int chunk_samples = 512;
  int32_t *i2s_read_buff = (int32_t *)calloc(chunk_samples, sizeof(int32_t));
  if (i2s_read_buff == NULL) {
    ESP_LOGE(TAG, "Error asignando memoria para fragmentos de lectura.");
    free(audio_buffer);
    return;
  }

  while (1) {
    // -------------------------
    // FASE 1: GRABAR
    // -------------------------
    ESP_LOGI(TAG, "Preparando para grabar...");
    ESP_LOGI(TAG, "3...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "2...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "1...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Reiniciar el canal I2S para limpiar cualquier audio viejo atascado en el buffer DMA
    i2s_channel_disable(rx_chan);
    i2s_channel_enable(rx_chan);

    ESP_LOGI(TAG, ">>> GRABANDO (%d SEGUNDOS) <<<", RECORD_TIME_SEC);
    size_t samples_recorded = 0;

    while (samples_recorded < total_samples) {
      size_t bytes_read = 0;
      // Leemos del I2S del micrófono
      esp_err_t err = i2s_channel_read(rx_chan, i2s_read_buff,
                                       chunk_samples * sizeof(int32_t),
                                       &bytes_read, portMAX_DELAY);

      if (err == ESP_OK && bytes_read > 0) {
        int samples_read = bytes_read / sizeof(int32_t);

        // Al estar en modo STEREO, el buffer recibe Izquierdo, Derecho, Izquierdo, Derecho...
        // Iteramos de 2 en 2 (i += 2) para extraer ÚNICAMENTE el canal Izquierdo (donde está el micro)
        // e ignorar la basura del canal Derecho.
        for (int i = 0; i < samples_read; i += 2) {
          if (samples_recorded >= total_samples)
            break; // Buffer principal lleno

          // Leemos a 32 bits y desplazamos 14 bits para tener buen volumen
          int32_t raw_sample = i2s_read_buff[i] >> 14;

          // 1. Filtro para remover el Offset DC
          static int32_t dc_offset = 0;
          dc_offset = (dc_offset * 127 + raw_sample) / 128;
          int32_t sample32 = raw_sample - dc_offset;

          // 2. Limitador para evitar saturación absoluta
          if (sample32 > 32767) sample32 = 32767;
          if (sample32 < -32768) sample32 = -32768;

          // Guardamos la muestra en el buffer
          audio_buffer[samples_recorded] = (int16_t)sample32;

          samples_recorded++;
        }
      } else {
        ESP_LOGE(TAG, "Error al leer datos del I2S.");
      }
    }

    // -------------------------
    // FASE 2: ENVIAR AL PC POR PUERTO SERIE
    // -------------------------
    ESP_LOGI(TAG, ">>> ENVIANDO AUDIO POR SERIAL <<<");

    // Imprimir marcadores para que el script de Python los detecte
    printf("\n---BEGIN_AUDIO---\n");

    // Imprimir las muestras en formato hexadecimal.
    // Hacemos que cada línea tenga 32 valores para no saturar el buffer del
    // print.
    for (int i = 0; i < total_samples; i++) {
      // Imprimimos el valor como entero sin signo de 16 bits en Hex.
      printf("%04X", (uint16_t)audio_buffer[i]);
      if ((i + 1) % 32 == 0) {
        printf("\n");
        // Pausa breve para evitar que salte el Watchdog de FreeRTOS por estar imprimiendo tanto tiempo seguido
        if ((i + 1) % 3200 == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
    }

    printf("\n---END_AUDIO---\n");
    ESP_LOGI(TAG, ">>> ENVÍO COMPLETADO <<<");
    ESP_LOGI(TAG, "Esperando 5 segundos para volver a grabar...");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_manager.h"

#define TAG "aud_mgr"

#define MINIMP3_NO_STDIO
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#define I2S_PORT I2S_NUM_0
#define AUDIO_MUTE_PIN GPIO_NUM_12
#define I2S_BIT_CLOCK_PIN GPIO_NUM_26
#define I2S_WORD_SELECT_PIN GPIO_NUM_25
#define I2S_DATA_OUT_PIN GPIO_NUM_33

static audio_data_t AUDIO_DATA = {
    .data = NULL,
    .size = 0,
    .reset_flag = false
};

static audio_task_callback_t AUDIO_FINISHED_CALLBACK = NULL;

// LOCAL FUNCTOINS

static void i2s_install() {
    const i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // ONLY LEFT OR RIGHT NOT WORKING
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false
    };

    int err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "%s", "I2S DRIVER INSTALLED");

    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BIT_CLOCK_PIN,
        .ws_io_num = I2S_WORD_SELECT_PIN,
        .data_out_num = I2S_DATA_OUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_start(I2S_PORT);
    ESP_LOGI(TAG, "%s", "I2S STARTED");
}

static void audio_task(void *pvParameter) {
    while (1) {
        while (!AUDIO_DATA.size) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        mp3dec_t mp3d = {};
        mp3dec_init(&mp3d);
        ESP_LOGI(TAG, "%s", "MP3 INIT");
        mp3dec_frame_info_t info = {};
        short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

        uint8_t *mp3_data_ptr = (uint8_t*) AUDIO_DATA.data;
        int remained_size = AUDIO_DATA.size;
        int samples = mp3dec_decode_frame(&mp3d, mp3_data_ptr, remained_size, pcm, &info);
        i2s_set_sample_rates(I2S_PORT, info.hz);
        ESP_LOGI(TAG, "%s", "MP3 DECONDING STARTED");
        int current_ptr = info.frame_bytes;

        while (samples > 0) {
            if (AUDIO_DATA.reset_flag) {
                AUDIO_DATA.reset_flag = false;
                break;
            }
            size_t written;

            for (int i = samples - 1; i >= 0; i--) {
                pcm[i * 2 + 1] = pcm[i];
                pcm[i * 2] = pcm[i];
            }
            i2s_write(I2S_PORT, pcm, samples * sizeof(short) * 2, &written, portMAX_DELAY);

            remained_size -= info.frame_bytes;
            samples = mp3dec_decode_frame(&mp3d, mp3_data_ptr + current_ptr, remained_size, pcm, &info);
            current_ptr += info.frame_bytes;
        }

        if (samples <= 0) {
            if (AUDIO_FINISHED_CALLBACK) {
                AUDIO_FINISHED_CALLBACK();
            }

            // audio finished playing
            AUDIO_DATA.data = NULL;
            AUDIO_DATA.size = 0;
        }
    }
}

// GLOBAL FUNCTIONS

void audio_init() {
    i2s_install();

    xTaskCreate(&audio_task, "audio_task", 1024 * 36, NULL, tskIDLE_PRIORITY, NULL);
}

void play_audio(void *mp3, int size, audio_task_callback_t cb) {
    AUDIO_FINISHED_CALLBACK = cb;

    AUDIO_DATA.data = mp3;    
    AUDIO_DATA.size = size;
    AUDIO_DATA.reset_flag = true;    
}



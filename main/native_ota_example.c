#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "errno.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "driver/uart.h"

#include "audio_manager.h"
#include "game_manager.h"
    
#define BUFFSIZE 1024
#define OTA_URL_SIZE 256

#define TAG "ota_test"
#define BOT_GET_UPDATES_API_URL "https://api.telegram.org/BOT_TOKEN_HERE/getUpdates?allowed_updates=[\"message\"]&limit=1&timeout=30&offset="
#define BOT_API_URL "https://api.telegram.org/BOT_TOKEN_HERE/"
#define ADMIN_USER_ID 123456789
#define DATA_PARTITION_NAME "mydata"

#define UART 0
#define UART_TXD_PIN 1
#define UART_RXD_PIN 3
#define UART_BUF_SIZE 1024

static esp_partition_t *DATA_PARTITION = NULL;
static void *data_partition_ptr = NULL;
static SemaphoreHandle_t ota_download_mutex = NULL;
static SemaphoreHandle_t data_download_mutex = NULL;

static bool CALL_IN_PROGRESS = false;

void http_cleanup(esp_http_client_handle_t client) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

void uart_write_str(char* str) {
    uart_write_bytes(UART, str, strlen(str));
    uart_write_bytes_with_break(UART, "\r\n", 2, 16);
}

void end_call() {
    uart_write_str("ATH");
    CALL_IN_PROGRESS = false;
}

void make_bot_request(char *method, char *json) {
    char requestUrl[256];
    strcpy(requestUrl, BOT_API_URL);
    strcat(requestUrl, method);

    esp_http_client_config_t config = {
        .url = requestUrl,
        .skip_cert_common_name_check = true,
    };
    ESP_LOGI(TAG, "BOT REQUEST: %s", requestUrl);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "BOT REQUEST ERROR: %i", err);
    }
    http_cleanup(client);
}

void send_message(int chatId, char* text) {
    cJSON *jsonObject = cJSON_CreateObject();
    cJSON *textObject = cJSON_CreateString(text);
    cJSON *chatIdObject = cJSON_CreateNumber(chatId);
    if (!jsonObject || !textObject || !chatIdObject) {
        cJSON_Delete(jsonObject);
        cJSON_Delete(textObject);
        cJSON_Delete(chatIdObject);
        return;
    }

    cJSON_AddItemToObject(jsonObject, "text", textObject);
    cJSON_AddItemToObject(jsonObject, "chat_id", chatIdObject);

    char *jsonStr = cJSON_Print(jsonObject);
    cJSON_Delete(jsonObject);
    if (!jsonStr) {
        return;
    }

    make_bot_request("sendMessage", jsonStr);
    free(jsonStr);
}

void download_data_partition(char *url) {
    esp_http_client_config_t config = {
        .url = url,
        .skip_cert_common_name_check = true,
    };

    esp_partition_t *partition = DATA_PARTITION;
    if (!partition) {
        ESP_LOGI(TAG, "%s", "CAN'T FIND PARTITION");
        return;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err == ESP_OK) {
        int contentLength = esp_http_client_fetch_headers(client);
        if (contentLength > 0 && contentLength < partition->size) {
            char buffer[8192];

            int erasePadding = contentLength - (contentLength / partition->erase_size) * partition->erase_size;
            int eraseSize = contentLength + partition->erase_size - erasePadding;
            err = esp_partition_erase_range(partition, 0, eraseSize);
            if (err != ESP_OK) {
                ESP_LOGI(TAG, "%s", "CAN'T ERASE PARTITION");
                http_cleanup(client);
                return;
            }

            ESP_LOGI(TAG, "%s", "DATA PARTITION DOWNLOAD START");

            for (int i = 0; i < contentLength;) {
                int received = esp_http_client_read(client, buffer, sizeof(buffer));
                if (received < 0) {
                    http_cleanup(client);
                    return;
                }
            
                err = esp_partition_write(partition, i, buffer, received);
                if (err != ESP_OK) {
                    http_cleanup(client);
                    return;
                }

                i += received;
            }

            ESP_LOGI(TAG, "%s", "DATA DOWNLOAD FINISHED SUCCESSFULLY...");
        }
    }

    http_cleanup(client);
}

void partition_data_download_task(void *pvParameter) {
    char *dataUrl = (char*) pvParameter;

    xSemaphoreTake(data_download_mutex, portMAX_DELAY);
    download_data_partition(dataUrl);
    send_message(ADMIN_USER_ID, "DATA DOWNLOADED");
    xSemaphoreGive(data_download_mutex);

    vTaskDelete(NULL);
}

void download_and_apply_ota(char *url) {
    esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    esp_http_client_config_t config = {
        .url = url,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err == ESP_OK) {
        int contentLength = esp_http_client_fetch_headers(client);
        if (contentLength > 0 && contentLength < update_partition->size) {
            char buffer[8192];

            esp_ota_handle_t ota_update_handle = 0;
            err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_update_handle);
            if (err != ESP_OK) {
                esp_ota_abort(ota_update_handle);
                http_cleanup(client);
                return;
            }

            ESP_LOGI(TAG, "%s", "OTA DOWNLOAD START");

            for (int i = 0; i < contentLength;) {
                int received = esp_http_client_read(client, buffer, sizeof(buffer));
                if (received < 0) {
                    esp_ota_abort(ota_update_handle);
                    http_cleanup(client);
                    return;
                }
            
                err = esp_ota_write(ota_update_handle, buffer, received);
                if (err != ESP_OK) {
                    esp_ota_abort(ota_update_handle);
                    http_cleanup(client);
                    return;
                }

                i += received;
            }

            ESP_LOGI(TAG, "%s", "OTA DOWNLOAD FINISHED");

            err = esp_ota_end(ota_update_handle);
            if (err != ESP_OK) {
                esp_ota_abort(ota_update_handle);
                http_cleanup(client);
                return;
            }


            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK) {
                esp_ota_abort(ota_update_handle);
                http_cleanup(client);
                return;
            }

            ESP_LOGI(TAG, "%s", "OTA FINISHED SUCCESSFULLY...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_restart();
        }
    }

    http_cleanup(client);
}

void ota_task(void *pvParameter) {
    char *appUrl = (char*) pvParameter;

    xSemaphoreTake(ota_download_mutex, portMAX_DELAY);
    download_and_apply_ota(appUrl);
    xSemaphoreGive(ota_download_mutex);

    vTaskDelete(NULL);
}

void reboot_task(void *pvParameter) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
}

void process_bot_commands(cJSON *messageJson) {
    cJSON *fromJson = cJSON_GetObjectItem(messageJson, "from");
    cJSON *textJson = cJSON_GetObjectItem(messageJson, "text");
    if (!fromJson || !textJson) {
        return;
    }

    cJSON *fromId = cJSON_GetObjectItem(fromJson, "id");
    if (!fromId || fromId->valueint != ADMIN_USER_ID) {
        return;
    }

    char *text = textJson->valuestring;
    if (!strcmp(text, "/reboot")) {
        ESP_LOGI(TAG, "%s", "Rebooting...");
        xTaskCreate(&reboot_task, "reboot_task", 1024, NULL, 5, NULL);
    } else if (!strncmp(text, "/ota ", 5)) {
        ESP_LOGI(TAG, "%s", "OTA START TASK...");
        char *urlBuffer = (char*) malloc(strlen(text));
        strcpy(urlBuffer, text + 5);
        xTaskCreate(&ota_task, "ota_task", 16384, urlBuffer, 5, NULL);
    } else if (!strncmp(text, "/data ", 6)) {
        ESP_LOGI(TAG, "%s", "DOWNLOAD START TASK...");
        char *urlBuffer = (char*) malloc(strlen(text));
        strcpy(urlBuffer, text + 6);
        xTaskCreate(&partition_data_download_task, "partition_task", 16384, urlBuffer, 5, NULL);
    } else if (!strncmp(text, "/uart ", 6)) {
        ESP_LOGI(TAG, "%s", "SENDING UART...");
        uart_write_str(text + 6);
    } else if (!strcmp(text, "/audio_off")) {

    } else if (!strcmp(text, "/memory")) {
        char heapSizeStr[16];
        itoa(esp_get_free_heap_size(), heapSizeStr, 10);
        send_message(ADMIN_USER_ID, heapSizeStr);
    } else if (!strcmp(text, "/end_call")) {
        end_call();
    }
}

int parse_bot_update(char *content) {
    cJSON *root = cJSON_Parse(content);
    cJSON *resultArrayJson = cJSON_GetObjectItem(root, "result");
    cJSON *resultJson = cJSON_GetArrayItem(resultArrayJson, 0); 
    if (!resultJson) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON *updateIdJson = cJSON_GetObjectItem(resultJson, "update_id");
    int updateId = updateIdJson ? updateIdJson->valueint : 0;

    cJSON *messageJson = cJSON_GetObjectItem(resultJson, "message");
    if (messageJson) {
        process_bot_commands(messageJson);
    }

    char *resultStr = cJSON_Print(resultJson);
    ESP_LOGI(TAG, "%s", resultStr);
    free(resultStr);

    cJSON_Delete(root);
    return updateId;
}

void process_bot_updates_loop() {
    char requestUrl[256];
    int updateId = 0;

    while (1) {
        strcpy(requestUrl, BOT_GET_UPDATES_API_URL); // TODO: rewrite
        char updateIdStr[32];
        itoa(updateId, updateIdStr, 10);
        strcat(requestUrl, updateIdStr);
        ESP_LOGI(TAG, "URL: %s", requestUrl);

        esp_http_client_config_t config = {
            .url = requestUrl,
            .skip_cert_common_name_check = true,
            .timeout_ms = 30 * 1000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_method(client, HTTP_METHOD_GET);
        esp_err_t err = esp_http_client_open(client, 0);

        if (err == ESP_OK) {
            int contentLength;
            do {
                contentLength = esp_http_client_fetch_headers(client);
                vTaskDelay(100 / portTICK_PERIOD_MS);
            } while (contentLength == ESP_ERR_HTTP_EAGAIN);

            if (contentLength > 0) {
                char *contentBuffer = (char*) malloc(contentLength + 1);
                contentBuffer[contentLength] = 0;
                if (contentBuffer) {
                    esp_http_client_read_response(client, contentBuffer, contentLength);

                    int currentUpdateId = parse_bot_update(contentBuffer);
                    if (currentUpdateId) {
                        updateId = currentUpdateId + 1;
                    }
                    free(contentBuffer);
                }
            }
        }

        http_cleanup(client);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void main_task(void *pvParameter) {
    send_message(ADMIN_USER_ID, "Bot started! (v0.106)");
    process_bot_updates_loop();
}

void map_data_partition() {
    DATA_PARTITION = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, DATA_PARTITION_NAME);
    if (!DATA_PARTITION) {
        return;
    }

    esp_partition_mmap_handle_t handle;
    esp_partition_mmap(DATA_PARTITION, 0, DATA_PARTITION->size, ESP_PARTITION_MMAP_DATA, &data_partition_ptr, &handle);
}

void setup_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_driver_install(UART, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    uart_param_config(UART, &uart_config);
    uart_set_pin(UART, UART_TXD_PIN, UART_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void process_dtmf(char *str) {
    char *dtmf_str = strstr(str, "+DTMF: ");
    char detected_number = *(dtmf_str + 7);
    if (detected_number >= '0' && detected_number <= '9') {
        detected_number -= '0';
        game_process_key(detected_number, end_call);
    }
}

void uart_read_task(void *pvParameter) {
    uart_write_str("AT");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    uart_write_str("AT+IPR=115200");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    uart_write_str("AT+DDET=1,1000,0");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    uart_write_str("AT+CMIC=0,7");

    while (1) {
        char buffer[1024];
        int readed = uart_read_bytes(UART, buffer, sizeof(buffer) - 1, 20 / portTICK_PERIOD_MS);
        if (readed <= 0) {
            continue;
        }

        buffer[readed] = 0;
        if (strstr(buffer, "+CLIP: \"") && !CALL_IN_PROGRESS) {
            uart_write_str("ATA");
            CALL_IN_PROGRESS = true;
            game_init(data_partition_ptr);
        } else if (strstr(buffer, "NO CARRIER")) {
            CALL_IN_PROGRESS = false;
        } else if (strstr(buffer, "+DTMF: ")) {
            process_dtmf(buffer);
        }

        for (int i = 0; i < readed; i++) {
            char c = buffer[i];
            if (c == '\n' || c == '\r') continue;

            if (c < 32 || c > 126) {
                buffer[i] = '*';
            }
        }
        send_message(ADMIN_USER_ID, buffer);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "OTA example app_main start");

    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ota_download_mutex = xSemaphoreCreateMutex();
    data_download_mutex = xSemaphoreCreateMutex();

    audio_init();
    setup_uart();
    map_data_partition();
    xTaskCreate(&main_task, "main_task", 8192, NULL, 5, NULL);
    xTaskCreate(&uart_read_task, "uart_read_task", 8192, NULL, 5, NULL);
}

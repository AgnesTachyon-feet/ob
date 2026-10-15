#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "M4_SPIFFS";
static const char *LABEL = "storage";
static const char *FILENAME = "/spiffs/log.txt";
static const char expected[] = "Sensor Log Entry #104: Temp=24.5C, Humidity=60%\n";

void app_main(void)
{
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        // LAB ONLY: format first-use blank simulated flash; may erase real data!
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: SPIFFS mount: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "PASS: SPIFFS mounted at /spiffs (partition storage)");

    bool successful = false;
    FILE *writer = fopen(FILENAME, "w"); // Assignment: writes a new file (overwrites old).
    if (writer == NULL) {
        ESP_LOGE(TAG, "FAIL: fopen write");
        goto cleanup;
    }
    const int n = fprintf(writer, "%s", expected);
    if (n < 0 || (size_t)n != strlen(expected)) {
        ESP_LOGE(TAG, "FAIL: fprintf write");
        fclose(writer);
        goto cleanup;
    }
    if (fclose(writer) != 0) {
        ESP_LOGE(TAG, "FAIL: fclose write (flush to flash)");
        goto cleanup;
    }
    ESP_LOGI(TAG, "PASS: fprintf + fclose wrote %d bytes", n);

    FILE *reader = fopen(FILENAME, "r");
    if (reader == NULL) {
        ESP_LOGE(TAG, "FAIL: fopen read");
        goto cleanup;
    }
    char line[128] = {0};
    const bool read_ok = (fgets(line, sizeof(line), reader) != NULL);
    if (fclose(reader) != 0) {
        ESP_LOGE(TAG, "FAIL: fclose read");
        goto cleanup;
    }
    if (!read_ok || strcmp(line, expected) != 0) {
        ESP_LOGE(TAG, "FAIL: readback mismatch");
        goto cleanup;
    }
    successful = true;
    ESP_LOGI(TAG, "PASS: readback matches written text byte-for-byte: %s", line);

cleanup:
    err = esp_vfs_spiffs_unregister(LABEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: unmount SPIFFS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "PASS: SPIFFS unmounted");
    }
    if (successful && err == ESP_OK) {
        ESP_LOGI(TAG, "MODULE 4 COMPLETE");
    }
}

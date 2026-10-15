#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUTTON_PIN GPIO_NUM_4
#define DEBOUNCE_MS 50
#define QUEUE_LENGTH 10

static const char *TAG = "M3_ISR_QUEUE";
static QueueHandle_t s_events;
static volatile uint32_t s_dropped_events; // ISR increments; worker only reads for reporting.

// ISR must stay tiny: no ESP_LOGI(), printf(), blocking calls or vTaskDelay().
static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;
    const uint32_t pin = BUTTON_PIN;
    BaseType_t higher_priority_woken = pdFALSE;
    if (xQueueSendFromISR(s_events, &pin, &higher_priority_woken) != pdTRUE) {
        ++s_dropped_events;
    }
    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void button_worker(void *arg)
{
    (void)arg;
    uint32_t pin;
    uint32_t accepted_presses = 0;
    TickType_t last_press = 0;
    bool has_last_press = false;
    const TickType_t debounce_ticks = pdMS_TO_TICKS(DEBOUNCE_MS);

    for (;;) {
        if (xQueueReceive(s_events, &pin, portMAX_DELAY) == pdTRUE) {
            const TickType_t now = xTaskGetTickCount();
            if (has_last_press && (now - last_press) < debounce_ticks) {
                ESP_LOGD(TAG, "Ignored contact bounce on GPIO %lu", (unsigned long)pin);
                continue;
            }
            has_last_press = true;
            last_press = now;
            ++accepted_presses;
            ESP_LOGI(TAG, "PASS: Worker got queued GPIO %lu, presses=%lu, queue drops=%lu",
                     (unsigned long)pin, (unsigned long)accepted_presses,
                     (unsigned long)s_dropped_events);
        }
    }
}

void app_main(void)
{
    s_events = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));
    assert(s_events != NULL);

    BaseType_t task_created = xTaskCreate(button_worker, "button_worker", 3072,
                                           NULL, 10, NULL);
    assert(task_created == pdPASS);

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    // GPIO ISR service allocated normally; IRAM_ATTR alone does NOT imply
    // the *entire* interrupt call graph is safe during flash-cache disable.
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL));
    ESP_LOGI(TAG, "Ready: press GPIO4 button. ISR -> queue[10] -> worker");
}

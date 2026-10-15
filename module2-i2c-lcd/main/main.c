#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_ADDR 0x27
#define I2C_SDA GPIO_NUM_21
#define I2C_SCL GPIO_NUM_22
#define I2C_HZ 100000
// Wokwi PCF8574T mapping: P0=RS, P1=RW, P2=EN, P3=backlight, P4..P7=D4..D7
#define LCD_RS (1U << 0)
#define LCD_EN (1U << 2)
#define LCD_BL (1U << 3)

static const char *TAG = "M2_I2C_LCD";
static i2c_master_dev_handle_t s_lcd;

// Timeout for modern i2c_master_transmit() is milliseconds, not RTOS ticks.
static void expander_write(uint8_t value)
{
    ESP_ERROR_CHECK(i2c_master_transmit(s_lcd, &value, 1, 1000));
}

static void lcd_nibble(uint8_t nibble, bool rs)
{
    uint8_t value = ((nibble & 0x0FU) << 4) | LCD_BL | (rs ? LCD_RS : 0);
    expander_write(value | LCD_EN);
    expander_write(value);
}

static void lcd_byte(uint8_t byte, bool rs)
{
    lcd_nibble(byte >> 4, rs);
    lcd_nibble(byte & 0x0FU, rs);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_byte(cmd, false);
    if (cmd == 0x01 || cmd == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(3));
    }
}

static void lcd_text(const char *text)
{
    for (const char *p = text; *p != '\0'; ++p) {
        lcd_byte((uint8_t)*p, true);
    }
}

static void lcd_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_nibble(0x02, false);  // Switch HD44780 to 4-bit data mode.
    lcd_cmd(0x28);           // 4-bit mode, 2 lines, 5x8 font.
    lcd_cmd(0x0C);           // Display on; cursor off.
    lcd_cmd(0x06);           // Increment cursor.
    lcd_cmd(0x01);           // Clear display.
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    ESP_LOGI(TAG, "PASS: I2C bus initialized (SDA=21 SCL=22, 100kHz device)");

    esp_err_t probe = i2c_master_probe(bus, LCD_ADDR, 1000);
    if (probe != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: No ACK from 0x%02X (%s)", LCD_ADDR, esp_err_to_name(probe));
        ESP_ERROR_CHECK(i2c_del_master_bus(bus));
        return;
    }
    ESP_LOGI(TAG, "PASS: LCD backpack ACK at address 0x%02X", LCD_ADDR);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_lcd));
    lcd_init();
    lcd_cmd(0x80); // row 1
    lcd_text("ESP-IDF I2C OK");
    lcd_cmd(0xC0); // row 2
    lcd_text("PCF8574 ACK 0x27");
    ESP_LOGI(TAG, "PASS: Sent LCD initialization and text data over I2C");
    // Bus and device remain active; do not delete handles while display is in use.
}

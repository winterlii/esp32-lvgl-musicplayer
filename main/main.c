#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "button_gpio.h"
#include "sdkconfig.h"

#define PARALLEL_LINES 16
#define DISP_WIDTH     240
#define DISP_HEIGHT    284

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ    (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

#define EXAMPLE_PIN_NUM_DATA0     11
#define EXAMPLE_PIN_NUM_PCLK      12
#define EXAMPLE_PIN_NUM_CS        10
#define EXAMPLE_PIN_NUM_DC        4
#define EXAMPLE_PIN_NUM_RST       3
#define EXAMPLE_PIN_NUM_BK_LIGHT  9

#define EXAMPLE_LCD_CMD_BITS   8
#define EXAMPLE_LCD_PARAM_BITS 8

// 按钮点击回调
static void btn_click_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * btn_label = lv_event_get_user_data(e);
    static bool is_clicked = false;
    is_clicked = !is_clicked;
    if(is_clicked)
    {
        lv_label_set_text(btn_label, "Clicked!");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x22AA22), 0);
    }
    else
    {
        lv_label_set_text(btn_label, "Press Enter");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x4488FF), 0);
    }
}

void app_main(void)
{
    // 背光GPIO初始化
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 1);

    // SPI总线配置
    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_PCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_DATA0,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PARALLEL_LINES * DISP_WIDTH * 2 + 8
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // LCD SPI IO配置
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    // LVGL port初始化
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t lcd_panel_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &lcd_panel_handle));

    // ST7789面板复位与初始化
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));

    // LVGL显示配置 PSRAM全屏buffer
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = lcd_panel_handle,
        .buffer_size = DISP_WIDTH * DISP_HEIGHT,
        .double_buffer = true,
        .hres = DISP_WIDTH,
        .vres = DISP_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .swap_bytes = true,
        }
    };
    lv_disp_t * disp_handle = lvgl_port_add_disp(&disp_cfg);

    // ===================== LVGL Button UI =====================
    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 140, 60);
    lv_obj_center(btn);
    lv_obj_set_style_radius(btn, 10, 0);

    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Press Enter");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, btn_label);
    // ==========================================================

    // ========== 新增：创建LVGL group，导航按键必须依赖group ==========
    lv_group_t * group = lv_group_create();
    lv_group_add_obj(group, btn);   // 把按钮加入group
    lv_group_set_default(group);
    // =================================================================

    // 三个导航按键：prev(GPIO5), next(GPIO6), enter(GPIO7)
    const button_gpio_config_t bsp_button_config[] = {
        {.gpio_num = GPIO_NUM_5, .active_level = 0},
        {.gpio_num = GPIO_NUM_6, .active_level = 0},
        {.gpio_num = GPIO_NUM_7, .active_level = 0},
    };
    const button_config_t btn_cfg = {0};
    button_handle_t prev_btn_handle = NULL;
    button_handle_t next_btn_handle = NULL;
    button_handle_t enter_btn_handle = NULL;

    iot_button_new_gpio_device(&btn_cfg, &bsp_button_config[0], &prev_btn_handle);
    iot_button_new_gpio_device(&btn_cfg, &bsp_button_config[1], &next_btn_handle);
    iot_button_new_gpio_device(&btn_cfg, &bsp_button_config[2], &enter_btn_handle);

    const lvgl_port_nav_btns_cfg_t btns = {
        .disp = disp_handle,
        .button_prev = prev_btn_handle,
        .button_next = next_btn_handle,
        .button_enter = enter_btn_handle
    };
    lv_indev_t* buttons_handle = lvgl_port_add_navigation_buttons(&btns);
    (void)buttons_handle;

    // 将group绑定到导航输入设备
    lv_indev_set_group(buttons_handle, group);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

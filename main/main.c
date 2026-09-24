#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
//#include "esp_lcd_panel_st7789.h"
//#include "esp_lcd_types.h"
#include "button_gpio.h"

#include "sdkconfig.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"

#define PARALLEL_LINES 16

#define DISP_WIDTH 240
#define DISP_HEIGHT 284

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_DATA0          11  /*!< for 1-line SPI, this also refereed as MOSI */
#define EXAMPLE_PIN_NUM_PCLK           12
#define EXAMPLE_PIN_NUM_CS             10
#define EXAMPLE_PIN_NUM_DC             4
#define EXAMPLE_PIN_NUM_RST            3
#define EXAMPLE_PIN_NUM_BK_LIGHT       9

#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8


void app_main(void)
{
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    // Initialize the GPIO of backlight
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 1); // 打开背光

    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_PCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_DATA0,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PARALLEL_LINES * DISP_WIDTH * 2 + 8
    };
    // ========== SPI总线初始化，补上！ ==========
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

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

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t err = lvgl_port_init(&lvgl_cfg);

    static lv_disp_t * disp_handle;
    esp_lcd_panel_io_handle_t io_handle = NULL;
    // 这里不要写硬编码数字1！用SPI2_HOST
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t lcd_panel_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &lcd_panel_handle));

    // ========== LCD复位+初始化，补上 ==========
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));

    /* Add LCD screen */
    // const lvgl_port_display_cfg_t disp_cfg = {
    //     .io_handle = io_handle,
    //     .panel_handle = lcd_panel_handle,
    //     .buffer_size = DISP_WIDTH*DISP_HEIGHT,
    //     .double_buffer = true,
    //     .hres = DISP_WIDTH,
    //     .vres = DISP_HEIGHT,
    //     .monochrome = false,
    //     .color_format = LV_COLOR_FORMAT_RGB565,
    //     .rotation = {
    //         .swap_xy = false,
    //         .mirror_x = false,
    //         .mirror_y = false,
    //     },
    //     .flags = {
    //         .buff_dma = true,
    //         .swap_bytes = false,
    //     }
    // };

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
        .buff_dma = false,      // PSRAM不能DMA，必须关闭
        .buff_spiram = true,    // 强制LVGL缓冲区从PSRAM分配
        .swap_bytes = true,
    }
};

    disp_handle = lvgl_port_add_disp(&disp_cfg);

    // ========== LVGL测试UI，画文字 ==========
    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello LVGL IDF6.1");
    lv_obj_center(label);

    // BUTTON INPUTS ADD
    const button_gpio_config_t bsp_button_config[] = {
        {
            .gpio_num = GPIO_NUM_5,
            .active_level = 0,
        },
        {
            .gpio_num = GPIO_NUM_6,
            .active_level = 0,
        },
        {
            .gpio_num = GPIO_NUM_7,
            .active_level = 0,
        },
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

    // ========= LVGL Button Demo 开始 =========
lv_obj_t * btn = lv_btn_create(lv_scr_act());
lv_obj_set_size(btn, 140, 60);
lv_obj_center(btn);
lv_obj_set_style_radius(btn, 10, 0);

lv_obj_t * btn_label = lv_label_create(btn);
lv_label_set_text(btn_label, "Press Enter");
lv_obj_center(btn_label);

// lv_obj_t * btn = lv_btn_create(lv_scr_act());
// lv_obj_set_size(btn, 120, 50);
// lv_obj_center(btn);

// lv_obj_t * btn_label = lv_label_create(btn);
// lv_label_set_text(btn_label, "Press Enter");
// lv_obj_center(btn_label);

// 按键回调函数，按下Enter触发
static void btn_click_cb(lv_event_t * e)
{
    static bool flag = false;
    flag = !flag;
    if(flag)
    {
        lv_label_set_text(btn_label, "Clicked!");
    }
    else
    {
        lv_label_set_text(btn_label, "Press Enter");
    }
}
lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, NULL);

// 按钮点击回调
static void btn_click_cb(lv_event_t * e)
{
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
lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, NULL);
// ========= LVGL Button Demo 结束 =========

    // ========== 删掉下面两行！！初始化阶段不能销毁屏幕和按键 ==========
    //lvgl_port_remove_navigation_buttons(buttons_handle);
    //lvgl_port_remove_disp(disp_handle);

    for(;;){
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
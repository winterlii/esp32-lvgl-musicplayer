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

// 播放器全局状态
static lv_obj_t * slider_progress;
static lv_obj_t * label_time;
static lv_obj_t * label_title;
static uint32_t cur_time = 0;
static const uint32_t total_time = 180; // 总时长 180秒=3分钟
static bool is_playing = false;
static lv_timer_t * play_timer = NULL;
static lv_obj_t * g_btn_play;

// 定时器回调（C标准函数，不能lambda）
static void play_timer_cb(lv_timer_t *timer)
{
    if(cur_time < total_time)
    {
        cur_time ++;
        lv_slider_set_value(slider_progress, cur_time, LV_ANIM_OFF);
        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu / %02lu:%02lu",
            cur_time/60, cur_time%60, total_time/60, total_time%60);
        lv_label_set_text(label_time, time_buf);
    }
    else
    {
        lv_timer_del(play_timer);
        play_timer = NULL;
        is_playing = false;
        lv_label_set_text(lv_obj_get_child(g_btn_play, 0), "播放");
    }
}

// 进度条拖动回调
static void slider_progress_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    cur_time = lv_slider_get_value(slider);
    printf("Seek to %lu seconds\n", (unsigned long)cur_time);
}

// 播放/暂停按钮回调
static void btn_play_cb(lv_event_t * e)
{
    g_btn_play = lv_event_get_target(e);
    is_playing = !is_playing;
    lv_obj_t * txt = lv_obj_get_child(g_btn_play, 0);
    if(is_playing)
    {
        lv_label_set_text(txt, "暂停");
        play_timer = lv_timer_create(play_timer_cb, 1000, NULL);
    }
    else
    {
        lv_label_set_text(txt, "播放");
        if(play_timer != NULL)
        {
            lv_timer_del(play_timer);
            play_timer = NULL;
        }
    }
}

// 上一曲按钮回调
static void btn_prev_cb(lv_event_t * e)
{
    printf("Previous Song\n");
    cur_time = 0;
    lv_slider_set_value(slider_progress, 0, LV_ANIM_OFF);
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu / %02lu:%02lu",
        cur_time/60, cur_time%60, total_time/60, total_time%60);
    lv_label_set_text(label_time, time_buf);
}

// 下一曲按钮回调
static void btn_next_cb(lv_event_t * e)
{
    printf("Next Song\n");
    cur_time = 0;
    lv_slider_set_value(slider_progress, 0, LV_ANIM_OFF);
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu / %02lu:%02lu",
        cur_time/60, cur_time%60, total_time/60, total_time%60);
    lv_label_set_text(label_time, time_buf);
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
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));

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

    // ========== 播放器UI ==========
    label_title = lv_label_create(lv_scr_act());
    lv_label_set_text(label_title, "Music Player");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 10);
    // 删除字体设置行，避免字体未开启报错

    slider_progress = lv_slider_create(lv_scr_act());
    lv_obj_set_size(slider_progress, 200, 10);
    lv_obj_align(slider_progress, LV_ALIGN_TOP_MID, 0, 45);
    lv_slider_set_range(slider_progress, 0, total_time);
    lv_slider_set_value(slider_progress, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_progress, slider_progress_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label_time = lv_label_create(lv_scr_act());
    lv_label_set_text(label_time, "00:00 / 03:00");
    lv_obj_align(label_time, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t * btn_prev = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_prev, 60, 40);
    lv_obj_align(btn_prev, LV_ALIGN_CENTER, -70, 20);
    lv_obj_set_style_radius(btn_prev, 8, 0);
    lv_obj_t * lab_prev = lv_label_create(btn_prev);
    lv_label_set_text(lab_prev, "Prev");
    lv_obj_center(lab_prev);
    lv_obj_add_event_cb(btn_prev, btn_prev_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_play = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_play, 60, 40);
    lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(btn_play, 8, 0);
    lv_obj_t * lab_play = lv_label_create(btn_play);
    lv_label_set_text(lab_play, "Play");
    lv_obj_center(lab_play);
    lv_obj_add_event_cb(btn_play, btn_play_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_next = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_next, 60, 40);
    lv_obj_align(btn_next, LV_ALIGN_CENTER, 70, 20);
    lv_obj_set_style_radius(btn_next, 8, 0);
    lv_obj_t * lab_next = lv_label_create(btn_next);
    lv_label_set_text(lab_next, "Next");
    lv_obj_center(lab_next);
    lv_obj_add_event_cb(btn_next, btn_next_cb, LV_EVENT_CLICKED, NULL);

    // ========== LVGL group 绑定这三个按钮 ==========
    lv_group_t * group = lv_group_create();
    lv_group_add_obj(group, btn_prev);
    lv_group_add_obj(group, btn_play);
    lv_group_add_obj(group, btn_next);
    lv_group_set_default(group);

    // ===================== 按键配置 修复iot_button参数顺序 =====================
    // GPIO7: prev 上一个
    // GPIO8: next 下一个
    // GPIO6: enter 选中点击
    const button_gpio_config_t btn_prev_cfg = {.gpio_num = GPIO_NUM_7, .active_level = 0};
    const button_gpio_config_t btn_next_cfg = {.gpio_num = GPIO_NUM_8, .active_level = 0};
    const button_gpio_config_t btn_enter_cfg = {.gpio_num = GPIO_NUM_6, .active_level = 0};
    const button_config_t btn_cfg = {0};
    button_handle_t prev_btn_handle = NULL;
    button_handle_t next_btn_handle = NULL;
    button_handle_t enter_btn_handle = NULL;

    // 修复！原型：iot_button_new_gpio_device(button_cfg, gpio_cfg, &handle)
    iot_button_new_gpio_device(&btn_cfg, &btn_prev_cfg, &prev_btn_handle);
    iot_button_new_gpio_device(&btn_cfg, &btn_next_cfg, &next_btn_handle);
    iot_button_new_gpio_device(&btn_cfg, &btn_enter_cfg, &enter_btn_handle);

    // 导航输入设备绑定
    const lvgl_port_nav_btns_cfg_t btns = {
        .disp = disp_handle,
        .button_prev = prev_btn_handle,
        .button_next = next_btn_handle,
        .button_enter = enter_btn_handle
    };
    lv_indev_t* buttons_handle = lvgl_port_add_navigation_buttons(&btns);
    lv_indev_set_group(buttons_handle, group);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

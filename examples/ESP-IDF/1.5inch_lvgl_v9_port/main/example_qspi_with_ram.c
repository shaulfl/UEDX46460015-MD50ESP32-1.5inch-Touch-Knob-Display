#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "lvgl.h"
#include "lv_demos.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "ui/ui.h"
#include "ui/screens.h"
#include "ui/ui_events.h"

/* Forward declarations for UI control accessors (ensure no implicit-decls during build) */
int get_source_index(void);
void set_source_index(int idx);
int get_filter_index(void);
void set_filter_index(int idx);

/* Button event table is defined later in this translation unit; forward it for logging usage */
extern const char *button_event_table[];

//************** */
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"

//***************** */

// extern  esp_err_t lvgl_port_indev_init(void);

//*************************************************** */

#define  CONFIG_EXAMPLE_LCD_CONTROLLER_SH8601   1
#include "esp_lcd_sh8601.h"
#define   CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_CST816S  1
#include "esp_lcd_touch_cst816s.h"


static const char *TAG = "example";

/* Periodic FreeRTOS task to drive ui_tick() under lvgl_port_lock so
 * queued button events and knob processing run regularly and safely.
 * Using a task avoids any uncertainty with LVGL timer setup timing.
 */
static void ui_tick_task(void *arg) {
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(20); /* 50 Hz */
    uint32_t iter = 0;
    for (;;) {
        /* Call ui_tick() without holding lvgl_port_lock here.
         * ui_tick() and the functions it invokes use lvgl_port_lock/unlock
         * internally where needed. Avoiding a top-level lock prevents nested
         * locking and excessive stack usage that could lead to overflows.
         */
        ui_tick();

#if (configUSE_TRACE_FACILITY == 1) || (tskKERNEL_VERSION_MAJOR >= 10)
        /* Periodically log remaining stack to catch regressions before overflow */
        if ((iter++ % 100) == 0) {
            UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(NULL);
            /* Convert to bytes for readability (stack units are words) */
            uint32_t watermark_bytes = watermark_words * sizeof(StackType_t);
            if (watermark_bytes < 768) {
                ESP_LOGW(TAG, "ui_tick_task low stack watermark: %u bytes", (unsigned)watermark_bytes);
            } else {
                ESP_LOGD(TAG, "ui_tick_task stack watermark: %u bytes", (unsigned)watermark_bytes);
            }
        }
#endif

        vTaskDelay(period);
    }
}

// Flag to ensure screen refresh happens only once
static bool screen_refreshed = false;



/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_HOST               (SPI2_HOST)
#define EXAMPLE_LCD_BITS_PER_PIXEL  (16)
#define EXAMPLE_LCD_DRAW_BUFF_DOUBLE (1) 
#define EXAMPLE_LCD_DRAW_BUFF_HEIGHT (60)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10) 
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_8)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (GPIO_NUM_17)

// The pixel number in horizontal and vertical
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH8601
#define EXAMPLE_LCD_H_RES             470// 466
#define EXAMPLE_LCD_V_RES             466// 466
#endif

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your touch spec ////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define  CONFIG_EXAMPLE_LCD_TOUCH_ENABLED  1
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
#define EXAMPLE_TOUCH_HOST                (I2C_NUM_0)
#define EXAMPLE_TOUCH_I2C_NUM       (0)
#define EXAMPLE_TOUCH_I2C_CLK_HZ    (400000)

#define EXAMPLE_PIN_NUM_TOUCH_SCL         (GPIO_NUM_3)
#define EXAMPLE_PIN_NUM_TOUCH_SDA         (GPIO_NUM_1)
#define EXAMPLE_PIN_NUM_TOUCH_RST         (GPIO_NUM_2)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (GPIO_NUM_4)

esp_lcd_touch_handle_t tp = NULL;
#endif


typedef enum {
    BSP_BTN_PRESS = GPIO_NUM_0,
} bsp_button_t;

#define BSP_ENCODER_A         (GPIO_NUM_6)
#define BSP_ENCODER_B         (GPIO_NUM_5)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to LVGL ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LVGL_BUFF_SIZE         (EXAMPLE_LCD_H_RES * 10)
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 2
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

#define HF_ws2812  1

#if  HF_ws2812
// GPIO assignment
#define LED_STRIP_BLINK_GPIO  21
// Numbers of the LED in the strip
#define LED_STRIP_LED_NUMBERS 8
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
  led_strip_handle_t led_strip=NULL;
led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,   // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_NUMBERS,        // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0,
#else
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
#endif
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}
#endif

esp_err_t app_touch_init(void)
{
    /* Initilize I2C */
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = EXAMPLE_TOUCH_I2C_CLK_HZ
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(EXAMPLE_TOUCH_I2C_NUM, &i2c_conf), TAG, "I2C configuration failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(EXAMPLE_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0), TAG, "I2C initialization failed");

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES+2,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {//屏幕方向一般在这里改
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)EXAMPLE_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle), TAG, "");
    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle);
}

esp_err_t app_lvgl_init(void)
{
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         /* LVGL task priority */
        .task_stack = 7096,         /* LVGL task stack size */
        .task_affinity = -1,        /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500,   /* Maximum sleep in LVGL task */
        .timer_period_ms = 5        /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size =  EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT +2,//EXAMPLE_LCD_DRAW_BUFF_HEIGHT
        .double_buffer = EXAMPLE_LCD_DRAW_BUFF_DOUBLE,
        .hres = EXAMPLE_LCD_H_RES+2,
        .vres = EXAMPLE_LCD_V_RES+2,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .mirror_x = false,//true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH8601
#define  R151ASM  0    //1  是新的那个     0是旧的那个
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
//1.32 inch AMOLED from DWO LIMITED
// Part number: DO0132FMST01-QSPI 
// Size: 1.5 inch
// Resolution: 466x466
// Signal interface:  QSPI 
    #if  R151ASM
    {0xFE, (uint8_t []){0x00}, 0, 0},   
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x35, (uint8_t []){0x00}, 0, 10},
    {0x53, (uint8_t []){0x20}, 1, 10},    
    {0x51, (uint8_t []){0xFF}, 1, 10},
    {0x63, (uint8_t []){0xFF}, 1, 10},
    {0x2A, (uint8_t []){0x00,0x06,0x01,0xD7}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x01,0xD1}, 4, 0},
   
    {0x11, (uint8_t []){0x00}, 0, 60},
    {0x29, (uint8_t []){0x00}, 0, 0},

    #else
    //HF
    {0xFE, (uint8_t []){0x00}, 0, 0},   
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x35, (uint8_t []){0x00}, 0, 10},
    {0x53, (uint8_t []){0x20}, 1, 10},    
    {0x51, (uint8_t []){0xFF}, 1, 10},
    {0x63, (uint8_t []){0xFF}, 1, 10},
    //{0x2A, (uint8_t []){0x00,0x06,0x01,0xD7}, 4, 0},
    //{0x2B, (uint8_t []){0x00,0x00,0x01,0xD1}, 4, 0},
    {0x2A, (uint8_t []){0x00,0x06,0x01,0xDD}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x01,0xD1}, 4, 0},
    {0x11, (uint8_t []){0x00}, 0, 60},
    {0x29, (uint8_t []){0x00}, 0, 0},
    #endif
};
#endif


//***************The following code is for standardizing the event triggering of rotary encoders and buttons.**************** */
//**************旋钮********* */
// Global variables to store knob events
static volatile int32_t knob_accum = 0;
static volatile bool knob_event_pending = false;
static portMUX_TYPE knob_spinlock = portMUX_INITIALIZER_UNLOCKED;


/* Expose a lightweight predicate so ui.c can prioritize/coalesce when knob is active. */
bool knob_events_pending(void) {
    return knob_event_pending;
}

const char *knob_event_table[] = {
    "KNOB_LEFT",
    "KNOB_RIGHT",
    "KNOB_H_LIM",
    "KNOB_L_LIM",
    "KNOB_ZERO",
};

/* Synthetic event codes for knob events to avoid enum collisions with button events.
 * These values are intentionally high to not overlap with existing button_event_t values.
 */
#define EVT_KNOB_LEFT  1001
#define EVT_KNOB_RIGHT 1002

void   LVGL_knob_event(void *event)
{
    knob_event_t knob_event = (knob_event_t)event;
    int32_t delta = 0;
    if (knob_event == KNOB_LEFT) {
        delta = 1;
    } else if (knob_event == KNOB_RIGHT) {
        delta = -1;
    } else {
        return;
    }

    portENTER_CRITICAL_ISR(&knob_spinlock);
    knob_accum += delta;
    portEXIT_CRITICAL_ISR(&knob_spinlock);

    knob_event_pending = true;

    ESP_LOGI(TAG, "LVGL_knob_event: %s (delta=%d)", knob_event_table[knob_event], (int)delta);
}

// Function to process knob events in the main LVGL task
void process_knob_events(void)
{
    if (!knob_event_pending) return;

    /* Take a snapshot of accumulated delta, then process only a small burst per tick
     * to avoid long LVGL-critical sections and WDT under fast rotations.
     */
    int32_t d_total;
    portENTER_CRITICAL(&knob_spinlock);
    d_total = knob_accum;
    knob_accum = 0;
    portEXIT_CRITICAL(&knob_spinlock);

    if (d_total == 0) {
        knob_event_pending = false;
        return;
    }

    /* Limit per-tick work: apply at most BURST steps of +/-1 each tick.
     * Remaining delta is returned to the accumulator for subsequent ticks.
     */
    const int BURST = 3;
    int applied = 0;

    /* Derive a single step (-1 or +1) to apply this tick */
    int step = (d_total > 0) ? 1 : -1;

    ESP_LOGI(TAG, "Processing knob: total=%d, step=%d, ui_state=%d", (int)d_total, step, (int)g_control_state);

    if (g_control_state == CONTROL_STATE_SELECTION) {
        /* Only move the highlight by one control per tick to keep UI responsive */
        int new_index = g_selected_control_index + step;

        /* Wrap around all three controls */
        while (new_index < 0) new_index += CONTROL_ITEM_COUNT;
        new_index = new_index % CONTROL_ITEM_COUNT;

        g_selected_control_index = new_index;
        ESP_LOGI(TAG, "SELECTION -> selected_control=%d", g_selected_control_index);

        lvgl_port_lock(0);
        ui_highlight_control(g_selected_control_index);
        lvgl_port_unlock();

        applied = 1;
    } else if (g_control_state == CONTROL_STATE_EDIT) {
        /* Edit the selected control by small steps per tick */
        if (g_selected_control_index == CONTROL_ITEM_VOLUME) {
            int current_volume = get_volume_value();
            int new_volume = current_volume + step;
            if (new_volume < 0) new_volume = 0;
            if (new_volume > 100) new_volume = 100;
            if (new_volume != current_volume) {
                ESP_LOGI(TAG, "EDIT[VOLUME] -> %d -> %d", current_volume, new_volume);
                lvgl_port_lock(0);
                set_volume_value(new_volume);
                lvgl_port_unlock();
            }
        } else if (g_selected_control_index == CONTROL_ITEM_SOURCE) {
            int cur = get_source_index();
            ESP_LOGI(TAG, "EDIT[SOURCE] -> %d + %d", cur, step);
            lvgl_port_lock(0);
            set_source_index(cur + step);
            lvgl_port_unlock();
        } else if (g_selected_control_index == CONTROL_ITEM_FILTER) {
            int cur = get_filter_index();
            ESP_LOGI(TAG, "EDIT[FILTER] -> %d + %d", cur, step);
            lvgl_port_lock(0);
            set_filter_index(cur + step);
            lvgl_port_unlock();
        } else {
            ESP_LOGI(TAG, "EDIT -> unknown selected_control %d", g_selected_control_index);
        }
        applied = 1;
    } else {
        /* NORMAL state: volume nudge by one per tick for smoothness */
        int current_volume = get_volume_value();
        int new_volume = current_volume + step;
        if (new_volume < 0) new_volume = 0;
        if (new_volume > 100) new_volume = 100;
        if (new_volume != current_volume) {
            ESP_LOGI(TAG, "NORMAL -> volume %d -> %d", current_volume, new_volume);
            lvgl_port_lock(0);
            set_volume_value(new_volume);
            lvgl_port_unlock();
        } else {
            ESP_LOGD(TAG, "NORMAL -> volume unchanged (%d)", current_volume);
        }
        applied = 1;
    }

    /* Return any remaining delta to the accumulator and keep the pending flag set
     * so the next ui_tick() will process the rest in small bursts.
     */
    int32_t remaining = d_total - (applied * step);
    if (remaining != 0) {
        portENTER_CRITICAL(&knob_spinlock);
        knob_accum += remaining;
        portEXIT_CRITICAL(&knob_spinlock);
        knob_event_pending = true;
    } else {
        knob_event_pending = false;
    }
}

// Add a periodic task to ensure knob events are processed

void LVGL_button_event(void *event)
{
    /* Button callbacks may be invoked from timer/ISR contexts (iot_button). Avoid calling
     * LVGL APIs or UI functions directly here. Instead, enqueue a minimal set of events onto
     * the FreeRTOS queue declared in ui.h so ui_tick() (LVGL/main context) can consume it.
     *
     * Policy: Only BUTTON_SINGLE_CLICK is enqueued to drive the SELECTION->EDIT->APPLY flow.
     * All other button events are ignored to prevent queue flooding and nondeterminism.
     */
    button_event_t bev = (button_event_t)event;

    if (bev != BUTTON_SINGLE_CLICK) {
        /* Drop non-single-click events to keep the queue clean and predictable */
        ESP_LOGD(TAG, "LVGL_button_event: dropping %s (only SINGLE_CLICK is enqueued)", button_event_table[bev]);
        return;
    }

    ESP_LOGI(TAG, "LVGL_button_event (enqueue): %s (ui_state=%d)", button_event_table[bev], (int)g_control_state);

    /* Try to enqueue into the shared FreeRTOS queue. Choose correct API based on ISR/task context.
     * Never call blocking xQueueSend from ISR context.
     */
    if (g_button_queue) {
        QueueHandle_t q = (QueueHandle_t)g_button_queue;
        if (xPortInIsrContext()) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            BaseType_t rc = xQueueSendFromISR(q, &bev, &xHigherPriorityTaskWoken);
            if (rc == pdTRUE) {
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            } else {
                /* Queue full in ISR: don't block. Record a lightweight legacy flag for ui_tick() fallback. */
                UBaseType_t free_spaces = uxQueueSpacesAvailable(q);
                UBaseType_t messages_waiting = uxQueueMessagesWaiting(q);
                ESP_EARLY_LOGW(TAG, "LVGL_button_event[ISR]: queue full; free=%u waiting=%u; using legacy handoff",
                               (unsigned)free_spaces, (unsigned)messages_waiting);
                last_button_event = (int)bev;
                button_event_pending = true;
            }
        } else {
            /* Task context: non-blocking send first; if it fails, use a very short timeout. */
            BaseType_t rc = xQueueSend(q, &bev, 0);
            if (rc != pdTRUE) {
                TickType_t timeout = pdMS_TO_TICKS(2);
                rc = xQueueSend(q, &bev, timeout);
                if (rc == pdTRUE) {
                    ESP_LOGD(TAG, "LVGL_button_event: enqueued via xQueueSend with short timeout");
                } else {
                    UBaseType_t free_spaces = uxQueueSpacesAvailable(q);
                    UBaseType_t messages_waiting = uxQueueMessagesWaiting(q);
                    ESP_LOGW(TAG, "LVGL_button_event: enqueue failed (rc=%d) free=%u waiting=%u; using legacy handoff",
                             (int)rc, (unsigned)free_spaces, (unsigned)messages_waiting);
                    last_button_event = (int)bev;
                    button_event_pending = true;
                }
            }
        }
    } else {
        /* Queue not created yet — fallback to legacy single-event handoff */
        ESP_LOGW(TAG, "LVGL_button_event: g_button_queue not initialized, using legacy handoff");
        last_button_event = (int)bev;
        button_event_pending = true;
    }
}

static knob_handle_t knob = NULL;

static void knob_event_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "knob event %s, %d", knob_event_table[(knob_event_t)data], iot_knob_get_count_value(knob));
    LVGL_knob_event(data);
   
}

void knob_init(uint32_t encoder_a, uint32_t encoder_b)
{
    knob_config_t cfg = {
        .default_direction = 0,
        .gpio_encoder_a = encoder_a,
        .gpio_encoder_b = encoder_b,
#if CONFIG_PM_ENABLE
        .enable_power_save = true,
#endif
    };

    knob = iot_knob_create(&cfg);
    assert(knob);
    esp_err_t err = iot_knob_register_cb(knob, KNOB_LEFT, knob_event_cb, (void *)KNOB_LEFT);
    err |= iot_knob_register_cb(knob, KNOB_RIGHT, knob_event_cb, (void *)KNOB_RIGHT);
    err |= iot_knob_register_cb(knob, KNOB_H_LIM, knob_event_cb, (void *)KNOB_H_LIM);
    err |= iot_knob_register_cb(knob, KNOB_L_LIM, knob_event_cb, (void *)KNOB_L_LIM);
    err |= iot_knob_register_cb(knob, KNOB_ZERO, knob_event_cb, (void *)KNOB_ZERO);
    ESP_ERROR_CHECK(err);
}

//******************** */

//*********按键**** */
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32C6
#define BOOT_BUTTON_NUM         9
#else
#define BOOT_BUTTON_NUM         0
#endif
#define BUTTON_ACTIVE_LEVEL     0
const char *button_event_table[] = {
    "BUTTON_PRESS_DOWN",
    "BUTTON_PRESS_UP",
    "BUTTON_PRESS_REPEAT",
    "BUTTON_PRESS_REPEAT_DONE",
    "BUTTON_SINGLE_CLICK",
    "BUTTON_DOUBLE_CLICK",
    "BUTTON_MULTIPLE_CLICK",
    "BUTTON_LONG_PRESS_START",
    "BUTTON_LONG_PRESS_HOLD",
    "BUTTON_LONG_PRESS_UP",
    "BUTTON_PRESS_END",
};

static void button_event_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Button event %s", button_event_table[(button_event_t)data]);
    LVGL_button_event(data);
  
  
}
void button_init(uint32_t button_num)
{
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = button_num,
            .active_level = BUTTON_ACTIVE_LEVEL,
#if CONFIG_GPIO_BUTTON_SUPPORT_POWER_SAVE
            .enable_power_save = true,
#endif
        },
    };
    button_handle_t btn = iot_button_create(&btn_cfg);
    assert(btn);
    esp_err_t err = iot_button_register_cb(btn, BUTTON_PRESS_DOWN, button_event_cb, (void *)BUTTON_PRESS_DOWN);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_UP, button_event_cb, (void *)BUTTON_PRESS_UP);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, button_event_cb, (void *)BUTTON_PRESS_REPEAT);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, button_event_cb, (void *)BUTTON_PRESS_REPEAT_DONE);
    err |= iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, button_event_cb, (void *)BUTTON_SINGLE_CLICK);
    err |= iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, button_event_cb, (void *)BUTTON_DOUBLE_CLICK);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, button_event_cb, (void *)BUTTON_LONG_PRESS_START);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, button_event_cb, (void *)BUTTON_LONG_PRESS_HOLD);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, button_event_cb, (void *)BUTTON_LONG_PRESS_UP);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_END, button_event_cb, (void *)BUTTON_PRESS_END);

#if CONFIG_ENTER_LIGHT_SLEEP_MODE_MANUALLY
    /*!< For enter Power Save */
    button_power_save_config_t config = {
        .enter_power_save_cb = button_enter_power_save,
    };
    err |= iot_button_register_power_save_cb(&config);
#endif

    ESP_ERROR_CHECK(err);
}

//***************** */
//BOTH THOSE FUNCTIONS ARE A WORKAROUND FOR THE STARTUP GLITCH IN DISPLAY
// Function to adjust volume once
static void adjust_volume_once(void) {
    // This helper runs in a FreeRTOS task. Do minimal work and always use the LVGL port lock
    // when touching LVGL objects to avoid reentrancy and watchdog triggers.
    screen_refreshed = true;

    int current_volume = get_volume_value();
    ESP_LOGI(TAG, "adjust_volume_once: current=%d", current_volume);

    // Decrease volume by 1 under LVGL lock, then yield to allow LVGL ticks to run.
    lvgl_port_lock(0);
    set_volume_value(current_volume - 1);
    lvgl_port_unlock();

    // Give LVGL time to process (short delay)
    vTaskDelay(pdMS_TO_TICKS(50));

    // Restore original volume under LVGL lock
    lvgl_port_lock(0);
    set_volume_value(current_volume);
    lvgl_port_unlock();

    // Small delay to ensure the repaint completes and other tasks run
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "adjust_volume_once: restored to %d", current_volume);
}

// Task implementation for volume adjustment
static void adjust_volume_task_impl(void* arg) {
    // Wait 50 milliseconds for UI to fully initialize
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Call the volume adjustment function
    adjust_volume_once();
    
    // Delete this task
    vTaskDelete(NULL);
}

void app_main(void)
{
    // static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    // static lv_disp_drv_t disp_drv;      // contains callback functions

    
    if (EXAMPLE_PIN_NUM_BK_LIGHT >= 0) {
        ESP_LOGI(TAG, "Turn off LCD backlight");
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    }
    #if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    

    #endif
    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg =
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH8601
        SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK, EXAMPLE_PIN_NUM_LCD_DATA0,
                                     EXAMPLE_PIN_NUM_LCD_DATA1, EXAMPLE_PIN_NUM_LCD_DATA2,
                                     EXAMPLE_PIN_NUM_LCD_DATA3, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
#endif
    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = -1,//EXAMPLE_PIN_NUM_LCD_CS,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,//-1,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .flags = {
            .quad_mode = true,
        },
    };

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,         // Uncomment these line if use custom initialization commands
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(sh8601_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_HOST, &io_config, &lcd_io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "Install LCD driver");
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH8601
    // Temporarily suppress error logging to avoid "swap_xy is not supported" message
    esp_err_t ret = esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
    }
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    app_touch_init();

    if (EXAMPLE_PIN_NUM_BK_LIGHT >= 0) {
        ESP_LOGI(TAG, "HF --Turn on LCD backlight");
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    }

    app_lvgl_init();

    #if HF_ws2812
    led_strip = configure_led();
    #endif   
    knob_init(BSP_ENCODER_A, BSP_ENCODER_B);
    button_init(BSP_BTN_PRESS);
    
    // Knob processing now handled directly in ui_tick() with accumulation for fast turns
    // No separate task needed
   
    ESP_LOGI(TAG, "Display custom UI");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    lvgl_port_lock(0);
    ui_init();

    // lv_demo_widgets();      /* A widgets example */
    // lv_demo_music();        /* A modern, smartphone-like music player demo. */
    // lv_demo_stress();       /* A stress test for LVGL. */
    // lv_demo_benchmark();    /* A demo to measure the performance of LVGL or to compare different settings. */

    // Release the mutex
    lvgl_port_unlock();

    /* Start the periodic UI tick task (no top-level LVGL lock; functions inside do their own locking).
     * Pin to core 1 to keep it alongside other input helpers and avoid preempting display flush.
     * Bump stack to 8 KB to prevent overflow during heavy LVGL flows (edit screen updates, logging).
     */
    BaseType_t ui_tick_created = xTaskCreatePinnedToCore(
        ui_tick_task,
        "ui_tick",
        8192,                     /* stack size (bytes) */
        NULL,
        tskIDLE_PRIORITY + 2,     /* slight boost over idle */
        NULL,
        1);
    if (ui_tick_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ui_tick task (rc=%d)", (int)ui_tick_created);
    }
    
    // Adjust the volume after 1 second (only once)
    if (!screen_refreshed) {
        // Create a simple task to handle the volume adjustment
        // Pin helper tasks for input handling to core 1 as well
        xTaskCreatePinnedToCore(adjust_volume_task_impl, "adjust_volume", 4096, NULL, 5, NULL, 1);
    }
}

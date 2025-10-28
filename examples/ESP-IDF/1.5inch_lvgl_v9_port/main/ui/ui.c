#if defined(EEZ_FOR_LVGL)
#include <eez/core/vars.h>
#endif

#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"
#include "esp_log.h"
#include "iot_button.h" /* bring BUTTON_PRESS_* enums into scope for ui_tick() */
/* ui_events.h removed: knob events are processed via process_knob_events() flag/accumulator only */

/* FreeRTOS includes: include FreeRTOS.h before other freertos headers to satisfy
 * the kernel's include-order requirements.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Forward declaration for knob processing implemented in example_qspi_with_ram.c */
extern void process_knob_events(void);
/* Query if knob has pending work; used to defer button transitions while rotating */
extern bool knob_events_pending(void);







#if defined(EEZ_FOR_LVGL)

void ui_init() {
    eez_flow_init(assets, sizeof(assets), (lv_obj_t **)&objects, sizeof(objects), images, sizeof(images), actions);
}

void ui_tick() {
    eez_flow_tick();
    tick_screen(g_currentScreen);
}

#else

#include <string.h>

static int16_t currentScreen = -1;

/* Selection/edit state and helpers (shared with input layer via ui.h) */
#include "esp_timer.h"

volatile control_state_t g_control_state = CONTROL_STATE_NORMAL;
volatile int g_selected_control_index = CONTROL_ITEM_VOLUME;

/* Mutex protecting UI state (g_control_state, g_selected_control_index, selection deadline) */
static SemaphoreHandle_t s_ui_state_mutex = NULL;

/* Convenience helpers for locking/unlocking the UI state mutex.
 * Use these everywhere we read/modify UI state to avoid races between
 * the LVGL main task, the consumer task and button/knob callbacks.
 */
static inline void ui_state_lock(void) {
    if (s_ui_state_mutex) {
        xSemaphoreTake(s_ui_state_mutex, portMAX_DELAY);
    }
}
static inline void ui_state_unlock(void) {
    if (s_ui_state_mutex) {
        xSemaphoreGive(s_ui_state_mutex);
    }
}

/* Concrete FreeRTOS queue handle is kept static here. The public header exposes an
 * opaque pointer `g_button_queue` (void*) — we define that symbol below and keep the
 * real QueueHandle_t private as s_button_queue.
 */
static QueueHandle_t s_button_queue = NULL;
/* Opaque pointer visible to other translation units (declared extern void *g_button_queue in ui.h) */
void *g_button_queue = NULL;

/* Backwards-compatible single-event flags (kept for minimal changes in other files) */
volatile bool button_event_pending = false;
volatile int last_button_event = -1;

static uint32_t g_selection_timeout_ms = UI_SELECTION_TIMEOUT_MS_DEFAULT;
static int64_t g_selection_deadline_ms = 0;
static bool g_ui_debug_enabled = false;

static const char *g_control_item_names[] = { "VOLUME", "SOURCE", "FILTER" };

static void update_selection_deadline_ms(void) {
    g_selection_deadline_ms = esp_timer_get_time() / 1000 + g_selection_timeout_ms;
    if (g_ui_debug_enabled) {
        ESP_LOGI("ui", "Selection deadline set to %lld ms (timeout %u)", g_selection_deadline_ms, g_selection_timeout_ms);
    }
}

void ui_debug_enable(bool enable) {
    g_ui_debug_enabled = enable;
    ESP_LOGI("ui", "UI debug %s", enable ? "ENABLED" : "DISABLED");
}

void ui_set_selection_timeout_ms(uint32_t ms) {
    g_selection_timeout_ms = ms;
    if (g_ui_debug_enabled) ESP_LOGI("ui", "Selection timeout set to %u ms", ms);
}

/* Highlight a control container (or clear when control_index < 0).
 * We operate on the inner button (child 0) of each container to avoid touching layout.
 *
 * Visual rules:
 * - In SELECTION mode: light grey ring (subtle hover).
 * - In EDIT mode: green ring to indicate active editing.
 */
void ui_highlight_control(int control_index) {
    /* Cache to avoid redundant style churn that can trigger WDT under fast input */
    static int s_last_highlight = -999;
    static control_state_t s_last_state = (control_state_t)-1;
    if (control_index == s_last_highlight && g_control_state == s_last_state) {
        if (g_ui_debug_enabled) {
            if (control_index >= 0 && control_index < CONTROL_ITEM_COUNT) {
                ESP_LOGD("ui", "ui_highlight_control: no-op for %d (state=%d)", control_index, (int)g_control_state);
            }
        }
        return;
    }
    s_last_highlight = control_index;
    s_last_state = g_control_state;

    lv_obj_t *containers[CONTROL_ITEM_COUNT] = {
        objects.volume_meter,
        objects.source_select,
        objects.filter_select
    };

    for (int i = 0; i < CONTROL_ITEM_COUNT; ++i) {
        lv_obj_t *c = containers[i];
        if (!c) continue;
        lv_obj_t *btn = lv_obj_get_child(c, 0);
        if (!btn) continue;

        if (control_index == i) {
            /* Choose style depending on current control state */
            if (g_control_state == CONTROL_STATE_EDIT) {
                /* EDIT state: green ring, slightly stronger background tint */
                lv_obj_set_style_border_width(btn, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xff00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xff111111), LV_PART_MAIN | LV_STATE_DEFAULT);
            } else {
                /* SELECTION (or other) state: light grey ring and subtle bg */
                lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xffcccccc), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xff0f0f0f), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        } else {
            /* Clear highlight */
            lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    if (g_ui_debug_enabled) {
        if (control_index >= 0 && control_index < CONTROL_ITEM_COUNT) {
            ESP_LOGI("ui", "Highlight control: %s (%d) state=%d", g_control_item_names[control_index], control_index, (int)g_control_state);
        } else {
            ESP_LOGI("ui", "Highlight cleared");
        }
    }
}

void ui_enter_selection_mode(void) {
    if (g_control_state != CONTROL_STATE_NORMAL) {
        if (g_ui_debug_enabled) ESP_LOGI("ui", "ui_enter_selection_mode called but state=%d", g_control_state);
        return;
    }
    g_control_state = CONTROL_STATE_SELECTION;
    /* Start selection on SOURCE to cycle through all three controls */
    g_selected_control_index = CONTROL_ITEM_SOURCE;
    update_selection_deadline_ms();
#ifdef lvgl_port_lock
    lvgl_port_lock(0);
#endif
    ui_highlight_control(g_selected_control_index);
#ifdef lvgl_port_unlock
    lvgl_port_unlock();
#endif
    ESP_LOGI("ui", "Entered SELECTION mode (start on SOURCE)");
}

void ui_confirm_selection(void) {
    if (g_control_state == CONTROL_STATE_SELECTION) {
        /* Transition SELECTION -> EDIT: set state, refresh deadline and update visual highlight for EDIT */
        g_control_state = CONTROL_STATE_EDIT;
        update_selection_deadline_ms();
        ESP_LOGI("ui", "Confirmed selection -> EDIT for %s", g_control_item_names[g_selected_control_index]);
        /* Ensure LVGL updates happen under the port lock if available so the green ring appears immediately */
#ifdef lvgl_port_lock
        lvgl_port_lock(0);
#endif
        ui_highlight_control(g_selected_control_index); /* will render green ring in EDIT state */
#ifdef lvgl_port_unlock
        lvgl_port_unlock();
#endif
        return;
    }
    if (g_control_state == CONTROL_STATE_EDIT) {
        /* In EDIT state confirm means apply */
        ui_apply_selected_control();
        return;
    }
    if (g_ui_debug_enabled) ESP_LOGI("ui", "ui_confirm_selection called but state=%d", g_control_state);
}

void ui_cancel_selection_mode(void) {
    if (g_control_state == CONTROL_STATE_NORMAL) return;
    g_control_state = CONTROL_STATE_NORMAL;
#ifdef lvgl_port_lock
    lvgl_port_lock(0);
#endif
    ui_highlight_control(-1);
#ifdef lvgl_port_unlock
    lvgl_port_unlock();
#endif
    ESP_LOGI("ui", "Selection cancelled -> NORMAL");
}

void ui_apply_selected_control(void) {
    if (g_control_state != CONTROL_STATE_EDIT) {
        if (g_ui_debug_enabled) ESP_LOGI("ui", "ui_apply_selected_control called but state=%d", g_control_state);
        return;
    }

    /* Apply semantics:
     * - Volume changes are applied immediately by set_volume_value during EDIT.
     * - Source/filter changes should already have been written to their respective indices
     *   via set_source_index()/set_filter_index() during EDIT. If you need to notify an
     *   audio backend, add that hook here.
     */

    ESP_LOGI("ui", "Applying selected control %s", g_control_item_names[g_selected_control_index]);

    /* Clear highlight and return to normal */
    ui_highlight_control(-1);
    g_control_state = CONTROL_STATE_NORMAL;
}

/* Removed button_consumer_task: event consumption is centralized in ui_tick()
 * to ensure all LVGL/UI calls occur in the LVGL/main context. */

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&objects)[index];
}

void loadScreen(enum ScreensEnum screenId) {
    currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void ui_init() {
    /* Ensure the button queue exists before any button callbacks try to use it.
     * Create the real FreeRTOS queue and expose it as an opaque void* via g_button_queue.
     */
    if (s_button_queue == NULL) {
        /* Increase queue capacity to handle bursts of knob events without dropping.
         * Buttons are low-frequency, but knobs can emit many events during fast rotation.
         * 64 entries gives a better buffer while remaining memory-light.
         */
        s_button_queue = xQueueCreate(64, sizeof(int));
        if (s_button_queue == NULL) {
            ESP_LOGI("ui", "Failed to create s_button_queue");
        } else {
            ESP_LOGI("ui", "s_button_queue created (capacity=64)");
        }
    }
    g_button_queue = (void *)s_button_queue;
 
    /* Removed separate consumer task; ui_tick() will drain the queue in LVGL context */

    create_screens();
    loadScreen(SCREEN_ID_MAIN);
}

void ui_tick() {
#ifdef lvgl_port_lock
    lvgl_port_lock(0);
#endif
    tick_screen(currentScreen);
#ifdef lvgl_port_unlock
    lvgl_port_unlock();
#endif

    /* Handle selection timeout (cancels selection/edit when expired) */
    if (g_control_state != CONTROL_STATE_NORMAL) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (g_selection_deadline_ms && now_ms >= g_selection_deadline_ms) {
            ESP_LOGI("ui", "Selection timeout reached (now=%lld, deadline=%lld), cancelling", now_ms, g_selection_deadline_ms);
            ui_cancel_selection_mode();
        }
    }

    /* Drain events from the FreeRTOS queue first (preferred, robust). Each event is
     * an int representing button_event_t. This runs in LVGL/main context so it's safe
     * to call LVGL APIs.
     */
    if (g_button_queue) {
        int bev;
        BaseType_t rc;
        QueueHandle_t q = (QueueHandle_t)g_button_queue;
        /* Diagnostic: check how many messages are waiting before we attempt to drain.
         * If the queue appears full but we never dequeue, this log will help root-cause it.
         */
        UBaseType_t before_waiting = uxQueueMessagesWaiting(q);
        if (g_ui_debug_enabled) {
            ESP_LOGI("ui", "ui_tick: queue waiting before drain=%u", (unsigned)before_waiting);
        }
        /* Drain as many events as available. Use non-blocking receive (0 ticks) to avoid stalling
         * and keep per-tick work bounded.
         *
         * IMPORTANT: If knob is currently active, defer button transitions this tick to avoid
         * races like SELECTION<->EDIT flips while style updates are in flight.
         */
        /* Cap events per tick to avoid starving LVGL and tripping WDT under fast rotations */
        int processed = 0;
        while (processed < 2 && (rc = xQueueReceive(q, &bev, 0)) == pdTRUE) {
            ESP_LOGI("ui", "ui_tick: dequeued event %d", bev);
            switch (bev) {
                case BUTTON_SINGLE_CLICK:
                    /* Single click: documented 3-step flow:
                     * 1st -> enter SELECTION (hover)
                     * 2nd -> confirm selection and enter EDIT
                     * 3rd -> apply and return to NORMAL
                     */
                    if (g_control_state == CONTROL_STATE_NORMAL) {
                        ui_enter_selection_mode();
                    } else {
                        ui_confirm_selection();
                    }
                    /* After a state transition, stop draining more events this tick to avoid
                     * overlapping style updates with concurrent knob processing.
                     */
                    processed = 2; /* force exit loop */
                    break;
                default:
                    /* ignore other events for the selection flow */
                    break;
            }
            if (processed < 2) {
                processed++;
            }
            /* Optional safety: if queue still appears full and we're continually draining, emit a debug message */
            if (g_ui_debug_enabled) {
                UBaseType_t mid_waiting = uxQueueMessagesWaiting(q);
                if (mid_waiting > 0) {
                    ESP_LOGD("ui", "ui_tick: queue still has %u messages after dequeue", (unsigned)mid_waiting);
                }
            }
        }
        UBaseType_t after_waiting = uxQueueMessagesWaiting(q);
        if (g_ui_debug_enabled) {
            ESP_LOGI("ui", "ui_tick: queue waiting after drain=%u (rc=%d)", (unsigned)after_waiting, (int)rc);
        }
    } else {
        /* Fallback to the legacy single-event handoff if queue wasn't created for some reason */
        extern volatile bool button_event_pending;
        extern volatile int last_button_event;
        if (button_event_pending) {
            int bev = last_button_event;
            button_event_pending = false;
            last_button_event = -1;
            ESP_LOGI("ui", "ui_tick: consuming queued button event %d (legacy)", bev);

            switch (bev) {
                case BUTTON_PRESS_REPEAT_DONE:
                case BUTTON_SINGLE_CLICK:
                    if (g_control_state == CONTROL_STATE_NORMAL) {
                        ui_enter_selection_mode();
                    } else {
                        ui_confirm_selection();
                    }
                    break;
                default:
                    break;
            }
        }
    }

    /* Always call process_knob_events() in LVGL context.
     * It returns immediately if no knob event is pending (flag is managed inside example_qspi_with_ram.c).
     * This avoids relying on a TU-local knob_event_pending symbol from here.
     */
    process_knob_events();
}

#endif

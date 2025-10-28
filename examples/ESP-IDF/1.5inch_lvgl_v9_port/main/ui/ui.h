#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include "lvgl.h"



#if defined(EEZ_FOR_LVGL)
#include <eez/flow/lvgl_api.h>
#endif

#if !defined(EEZ_FOR_LVGL)
#include "screens.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif



/* Selection/edit state machine API */
#include <stdint.h>
#include <stdbool.h>

#define UI_SELECTION_TIMEOUT_MS_DEFAULT 5000

typedef enum {
    CONTROL_STATE_NORMAL = 0,
    CONTROL_STATE_SELECTION,
    CONTROL_STATE_EDIT
} control_state_t;

typedef enum {
    CONTROL_ITEM_VOLUME = 0,
    CONTROL_ITEM_SOURCE,
    CONTROL_ITEM_FILTER,
    CONTROL_ITEM_COUNT
} control_item_t;

/* Shared state visible to input layer */
extern volatile control_state_t g_control_state;
extern volatile int g_selected_control_index;

/* Button event handoff from timer/ISR context into LVGL context */
#include <stdbool.h>
/* We avoid including FreeRTOS headers from this public UI header to prevent
 * include-order issues (FreeRTOS.h must be included before task/queue headers).
 * The implementation defines and uses the actual FreeRTOS queue. Here we expose
 * an opaque pointer that the implementation casts to QueueHandle_t.
 */
extern void *g_button_queue; /* opaque FreeRTOS queue handle (QueueHandle_t) */
extern volatile bool button_event_pending;
extern volatile int last_button_event;

/* Selection/edit API (used by button/knob handlers) */
void ui_enter_selection_mode(void);
void ui_confirm_selection(void); /* confirm -> enter EDIT or apply */
void ui_cancel_selection_mode(void);
void ui_apply_selected_control(void);
void ui_highlight_control(int control_index);
void ui_set_selection_timeout_ms(uint32_t ms);

/* Enable/disable verbose debug logging for UI control flow */
void ui_debug_enable(bool enable);

/* Core UI APIs */
void ui_init();
void ui_tick();

/* Function to process knob events */
void process_knob_events(void);

#if !defined(EEZ_FOR_LVGL)
void loadScreen(enum ScreensEnum screenId);
#endif

#ifdef __cplusplus
}
#endif


#endif // EEZ_LVGL_UI_GUI_H
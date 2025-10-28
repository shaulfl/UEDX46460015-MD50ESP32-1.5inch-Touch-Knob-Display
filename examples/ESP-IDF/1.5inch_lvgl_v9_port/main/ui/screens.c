#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"


/* Forward declaration: helper that safely sets the last-child label text of a button-like object.
 * Declared early so other functions that call it before its definition don't trigger implicit-declarations.
 */
static void set_button_label_text_safe(lv_obj_t *btn, const char *text);

/* Forward references for value arrays (defined later) so setters can use them
 * regardless of ordering within this file.
 */
extern const char *g_source_vals[];
extern const int g_source_count;
extern const char *g_filter_vals[];
extern const int g_filter_count;

/* (Original) No forward-declared custom event callbacks here. */

objects_t objects;
lv_obj_t *tick_value_change_obj;
/* Runtime-controllable bitrate value and helper APIs.
 * Use set_bitrate_value(...) to change the displayed bitrate; get_bitrate_value() to read it.
 */
static int g_bitrate_value = 00000; /* default 44.1 kHz represented as integer Hz (44100) */
/* Runtime-controllable volume (0-100) and helper APIs.
 * Use set_volume_value(...) to change the displayed volume; get_volume_value() to read it.
 * The volume label inside the volume control will be updated if available.
 */
static int g_volume_value = 100; /* percent 0-100 */

void set_volume_value(int value) {
    /* Accept any integer value for volume (no clamping). The displayed label shows the raw value. */
    g_volume_value = value;
    if (objects.volume_meter) {
        /* The volume label is the last child of the inner button; reuse helper to set it */
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", g_volume_value);
        /* inner button is child 0 of container */
        lv_obj_t *btn = lv_obj_get_child(objects.volume_meter, 0);
        if (btn) {
            set_button_label_text_safe(btn, buf);
        }
    }
}

int get_volume_value(void) {
    return g_volume_value;
}

void set_bitrate_value(int hz) {
    g_bitrate_value = hz;
    /* Update on-screen label if created */
    if (objects.bitrate) {
        /* Display as kHz or Hz depending on value; original UI showed "44.1" so format as kHz with one decimal when divisible by 100 */
        char buf[32];
        if (hz % 100 == 0) {
            /* show like "44.1" for 44100 -> 44.1 */
            float khz = hz / 1000.0f;
            snprintf(buf, sizeof(buf), "%.1f", khz);
        } else {
            snprintf(buf, sizeof(buf), "%d", hz);
        }
        lv_label_set_text(objects.bitrate, buf);
    }
}

/* State indexes for source/input and filter controls (0..3).
 * Changing these via the setters updates the on-screen label immediately.
 */
static int g_source_index = 0;
static int g_filter_index = 0;

/* Set source/input index (0..g_source_count-1) and update UI */
void set_source_index(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= g_source_count) idx = g_source_count - 1;
    g_source_index = idx;
    if (objects.source_select) {
        lv_obj_t *btn = lv_obj_get_child(objects.source_select, 0);
        if (btn) {
            set_button_label_text_safe(btn, g_source_vals[g_source_index]);
        }
    }
}

/* Get current source index */
int get_source_index(void) {
    return g_source_index;
}

/* Set filter index (0..g_filter_count-1) and update UI */
void set_filter_index(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= g_filter_count) idx = g_filter_count - 1;
    g_filter_index = idx;
    if (objects.filter_select) {
        lv_obj_t *btn = lv_obj_get_child(objects.filter_select, 0);
        if (btn) {
            set_button_label_text_safe(btn, g_filter_vals[g_filter_index]);
        }
    }
}

/* Get current filter index */
int get_filter_index(void) {
    return g_filter_index;
}
int get_bitrate_value(void) {
    return g_bitrate_value;
}
uint32_t active_theme_index = 0;

/* Predefined value lists for controls to avoid repeated allocations */
const char *g_source_vals[] = {"AUTO", "USB", "COAX", "OPTIC"};
const int g_source_count = 4;
const char *g_filter_vals[] = {"LIN", "MIX", "MIN", "SLO"};
const int g_filter_count = 4;
/* Prototypes for click handlers added later to cycle values */
static void source_click_cb(lv_event_t * e);
static void filter_click_cb(lv_event_t * e);
/* Gesture handler for audio control swipe actions (added to inner button) */
static void audio_ctrl_gesture_cb(lv_event_t * e);
/* Big arc change handler (declared so it can be attached during creation) */
static void big_arc_event_cb(lv_event_t * e);
void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.obj0 = obj;
            lv_obj_set_pos(obj, 8, 8);
            lv_obj_set_size(obj, 450, 450);
            lv_arc_set_value(obj, 100);
            lv_arc_set_bg_start_angle(obj, 20);
            lv_arc_set_bg_end_angle(obj, 340);
            lv_arc_set_rotation(obj, 90);
 
            /* Make the knob (handle) invisible but keep it interactive.
             * We set the knob part to fully transparent and remove visible border/radius,
             * but do not clear interaction flags so the user can still drag the arc.
             */
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

            lv_obj_set_style_arc_width(obj, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_width(obj, 18, LV_PART_INDICATOR | LV_STATE_DEFAULT);

            /* Update volume when the big arc value changes */
            lv_obj_add_event_cb(objects.obj0, big_arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        }
        // Create audio control components using the unified function
        objects.volume_meter = create_audio_control(parent_obj, 163, 286, "100", &ui_font_inter_bold_58);
        objects.source_select = create_audio_control(parent_obj, 163 , 44, g_source_vals[0], &ui_font_inter_bold_32);
        objects.filter_select = create_audio_control(parent_obj, 286 , 163, g_filter_vals[0], &ui_font_inter_bold_32);

        /* Attach click handlers to the container objects so existing click feedback
         * remains (the container's inner button still changes color). */
        if (objects.source_select) {
            lv_obj_add_event_cb(objects.source_select, source_click_cb, LV_EVENT_CLICKED, NULL);
        }
        if (objects.filter_select) {
            lv_obj_add_event_cb(objects.filter_select, filter_click_cb, LV_EVENT_CLICKED, NULL);
        }

        /* Attach click handlers to the source (input) and filter controls so each
         * click cycles through the requested values and wraps around.
         * We register callbacks on the inner button (child index 0 of the container).
         */
        if (objects.source_select) {
            lv_obj_t *btn = lv_obj_get_child(objects.source_select, 0);
            if (btn) {
                lv_obj_add_event_cb(btn, source_click_cb, LV_EVENT_CLICKED, NULL);
            }
        }
        if (objects.filter_select) {
            lv_obj_t *btn = lv_obj_get_child(objects.filter_select, 0);
            if (btn) {
                lv_obj_add_event_cb(btn, filter_click_cb, LV_EVENT_CLICKED, NULL);
            }
        }


        /* Connect the large decorative arc (objects.obj0) to the volume control:
         * - Find and cache the label inside the volume control so we can update its text.
         * - Register a C callback (big_arc_event_cb) on the large arc to update
         *   the numeric label and the inner decorative arc when the big arc changes.
         *
         * Use LVGL v8/v9 compatible APIs: iterate children with lv_obj_get_child and
         * lv_obj_get_child_cnt to locate the label rather than relying on non-portable helpers.
         */
        /* (Original) No volume_label caching or extra event registration here. */
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            lv_obj_set_pos(obj, 51, 173);
            lv_obj_set_size(obj, 223, 120);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // bitrate
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.bitrate = obj;
                    lv_obj_set_pos(obj, 12, 44);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_font(obj, &ui_font_inter_extrabold_italic_64, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "44.1");
                }
                {
                    // khz
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.khz = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_font(obj, &ui_font_inter_bold_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 37, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "kHz");
                }
            }
        }
        {
            // format
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.format = obj;
            lv_obj_set_pos(obj, 51, 245);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_inter_bold_32, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "PCM");
        }
    }
    
    tick_screen_main();
}

void delete_screen_main() {
    lv_obj_delete(objects.main);
    objects.main = 0;
    objects.obj0 = 0;
    objects.volume_meter = 0;
    objects.source_select = 0;
    objects.filter_select = 0;
    objects.bitrate = 0;
    objects.khz = 0;
    objects.format = 0;
}

/* Hover behavior removed. tick_screen_main is now a no-op. */
void tick_screen_main() {
    /* Intentionally left empty to disable hover cycling. */
}



typedef void (*create_screen_func_t)();
create_screen_func_t create_screen_funcs[] = {
    create_screen_main,
};
void create_screen(int screen_index) {
    create_screen_funcs[screen_index]();
}
void create_screen_by_id(enum ScreensEnum screenId) {
    create_screen_funcs[screenId - 1]();
}

/* Forward-declare event callbacks so they can be attached from create_audio_control() and create_screen_main(). */
static void audio_ctrl_btn_event_cb(lv_event_t * e);
static void big_arc_event_cb(lv_event_t * e);

typedef void (*delete_screen_func_t)();
delete_screen_func_t delete_screen_funcs[] = {
    delete_screen_main,
};
void delete_screen(int screen_index) {
    delete_screen_funcs[screen_index]();
}
void delete_screen_by_id(enum ScreensEnum screenId) {
    delete_screen_funcs[screenId - 1]();
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

 // Audio control component creation function
 lv_obj_t *create_audio_control(lv_obj_t *parent, int32_t x, int32_t y, const char *text, const lv_font_t *font) {
    int button_size =140;
    // Create container box
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_pos(container, x, y);
    lv_obj_set_size(container, button_size, button_size);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    /* Hover state styling removed. */
    
    // Create button inside container
    lv_obj_t *obj = lv_button_create(container);
    lv_obj_set_size(obj, button_size, button_size);
    lv_obj_set_pos(obj, 0, 0);
    add_style_circular_button(obj);
    lv_obj_set_style_radius(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* (Original) no event callback attached here for long-press; keep button passive. */
    
    // Create decorative arc (smaller to fit inside the button)
    /* Decorative arc: slightly inset so it sits inside the button */
    lv_obj_t *arc = lv_arc_create(obj);
    lv_obj_set_pos(arc, 0, 0);
    /* make the arc slightly smaller than the button so it doesn't overflow */
    int arcThickness = 8;
    int arcSize = button_size-arcThickness-2;
    lv_obj_set_size(arc, arcSize, arcSize);
    /* center the arc inside the button */
    lv_obj_set_style_align(arc, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_value(arc, 0);
    lv_arc_set_bg_start_angle(arc, 20);
    lv_arc_set_bg_end_angle(arc, 340);
    lv_arc_set_rotation(arc, 90);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_WITH_ARROW | LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_set_style_bg_opa(arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    /* thinner arc stroke to look like a ring inside the button */
    lv_obj_set_style_arc_width(arc, arcThickness, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* put the arc behind the button's contents */
    lv_obj_move_background(arc);
    
    /* Create label and center it inside the button */
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    
    /* Attach gesture handler so horizontal swipes change the value:
     *  - swipe right -> next value
     *  - swipe left  -> previous value
     * The handler is attached to the inner button (obj).
     */
    lv_obj_add_event_cb(obj, audio_ctrl_gesture_cb, LV_EVENT_GESTURE, NULL);
    
    return container;
}

/* Animator helper: animate container transform width/height.
* `var` is the lv_obj_t* container, `value` is current interpolated integer (px).
*/

/* Hover animator removed. */

/* big_arc_event_cb:
 * - Triggered when the large decorative arc (objects.obj0) value changes.
 * - Maps the big arc value to the volume control and the inner decorative arc.
 *
 * Behavior:
 *  - Reads the big arc value (assumes its range is 0..100 or config-specific).
 *  - Updates g_volume_value via set_volume_value(mapped).
 *  - Updates the inner decorative arc inside the volume control button to reflect the same percentage.
 */
static void big_arc_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *big = lv_event_get_target(e);
    if (!big) return;

    /* Read big arc value and determine its range.
     * LVGL arcs typically use 0..100 by default in this project, but be defensive.
     */
    int big_val = lv_arc_get_value(big);

    /* Map big_val to desired volume value. Here we assume big_val already in 0..100.
     * If your big arc uses a different range, adjust mapping accordingly.
     */
    int new_volume = big_val;
    set_volume_value(new_volume);
}

/* Event handler for the audio control's inner button.
 * We use long-press to grow the control. On long press start we animate to
 * the "hovered" size; on release we animate back to normal.
 *
 * User data for the event is expected to be the container object returned by
 * create_audio_control (so we can animate the container, not the button).
 */
/* (Original) No audio_ctrl_btn_event_cb implementation */

/* Animate hover: when hover==true animate from current transform to target,
* otherwise animate back to 0.
*/
/* Hover animation API retained as no-op to avoid breaking callers.
 * Does nothing now; visual hover behavior was removed.
 */
void set_audio_control_hover(lv_obj_t *obj, bool hover) {
    (void)obj;
    (void)hover;
    /* no-op */
}

/* Toggle hover is now a no-op to keep API stable. */
void toggle_audio_control_hover(lv_obj_t *obj) {
    (void)obj;
}

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_main();
}

/* Utility: safely set the last child label text of a button-like object */
static void set_button_label_text_safe(lv_obj_t *btn, const char *text) {
    if (!btn) return;
    int32_t cnt = lv_obj_get_child_cnt(btn);
    if (cnt <= 0) return;
    lv_obj_t *lbl = lv_obj_get_child(btn, cnt - 1); /* last child, usually the label */
    if (!lbl) return;
    /* Only call lv_label_set_text when the object is actually a label.
       lv_obj_check_type is not universally available; try to be defensive. */
#ifdef LV_USE_OBJ_GET_TYPE
    if (lv_obj_check_type(lbl, &lv_label_class)) {
        lv_label_set_text(lbl, text);
    }
#else
    /* Best-effort: attempt to set text; if not a label this may assert on some LVGL configs.
       If your build crashes here, we'll instead require explicit label bookkeeping. */
    lv_label_set_text(lbl, text);
#endif
}

/* Click callback for the source/input audio control.
 * Cycles through: Auto -> USB -> SPDIF1 -> SPDIF2 -> Auto ...
 * The event target is the button; we update its inner label.
 */
static void audio_ctrl_cycle_values_btn(lv_obj_t *btn, const char **vals, int count, int delta) {
    if (!btn || !vals || count <= 0) return;
    int32_t cnt = lv_obj_get_child_cnt(btn);
    if (cnt <= 0) return;
    lv_obj_t *lbl = lv_obj_get_child(btn, cnt - 1);
    if (!lbl) return;
    const char *cur = lv_label_get_text(lbl);
    int idx = 0;
    /* find current index (default 0 if not found) */
    for (int i = 0; i < count; ++i) {
        if (strcmp(cur, vals[i]) == 0) { idx = i; break; }
    }
    int next = (idx + delta + count) % count;
    set_button_label_text_safe(btn, vals[next]);
}

/* Thin click wrappers reuse the same helper and pass the value arrays. */
static void source_click_cb(lv_event_t * e) {
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn) return;
    audio_ctrl_cycle_values_btn(btn, g_source_vals, g_source_count, +1);
}

/* Click callback for the filter audio control.
 * Cycles through: Linear -> Mixed -> Min -> Slow -> Linear ...
 */
static void filter_click_cb(lv_event_t * e) {
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn) return;
    audio_ctrl_cycle_values_btn(btn, g_filter_vals, g_filter_count, +1);
}

/* Programmatic helpers to change a control's value in the same way gestures do.
 * Call `audio_ctrl_next(btn)` to advance to the next value (equivalent to swipe right),
 * or `audio_ctrl_prev(btn)` to go to the previous value (equivalent to swipe left).
 *
 * These helpers reuse the same value-sets used by the gesture handler so behavior
 * stays consistent with swipe interactions.
 */

/* audio_ctrl_change_by_delta removed — unified into audio_ctrl_cycle_values_btn. */

/* Advance to next value (swipe right) */
void audio_ctrl_next(lv_obj_t *btn) {
    /* Use unified cycle function with automatic detection. */
    audio_ctrl_cycle_values_btn(btn, NULL, 0, +1);
}

/* Go to previous value (swipe left) */
void audio_ctrl_prev(lv_obj_t *btn) {
    /* Use unified cycle function with automatic detection. */
    audio_ctrl_cycle_values_btn(btn, NULL, 0, -1);
}

/* Gesture event handler: change control value on left/right swipe.
 * It detects which value-set the control currently displays (source/input or filter)
 * then advances forward on right swipe or backward on left swipe.
 */
static void audio_ctrl_gesture_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_obj_t *btn = lv_event_get_target(e);
    if (!btn) return;
    /* find the label (last child) */
    int32_t cnt = lv_obj_get_child_cnt(btn);
    if (cnt <= 0) return;
    lv_obj_t *lbl = lv_obj_get_child(btn, cnt - 1);
    if (!lbl) return;
    /* Determine gesture direction from active input device */
    lv_indev_t *act = lv_indev_get_act();
    if (!act) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(act);
    if (dir == LV_DIR_RIGHT) {
        audio_ctrl_cycle_values_btn(btn, NULL, 0, +1);
    } else if (dir == LV_DIR_LEFT) {
        audio_ctrl_cycle_values_btn(btn, NULL, 0, -1);
    } else {
        /* ignore other directions */
        return;
    }
}

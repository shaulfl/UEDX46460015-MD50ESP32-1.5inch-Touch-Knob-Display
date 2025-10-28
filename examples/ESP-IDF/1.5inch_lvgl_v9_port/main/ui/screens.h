#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *obj0;
    lv_obj_t *volume_meter;
    lv_obj_t *source_select;
    lv_obj_t *filter_select;
    lv_obj_t *bitrate;
    lv_obj_t *khz;
    lv_obj_t *format;
    /* Label inside the volume control's button so the displayed numeric value can be updated. */
    lv_obj_t *volume_label;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void delete_screen_main();
void tick_screen_main();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

// Audio control component creation function
lv_obj_t *create_audio_control(lv_obj_t *parent, int32_t x, int32_t y, const char *text, const lv_font_t *font);


/* External hooks for platform-level input events (knob/button) */
void LVGL_knob_event(void *event);
void LVGL_button_event(void *event);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
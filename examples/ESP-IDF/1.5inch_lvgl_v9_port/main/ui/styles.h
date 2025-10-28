#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: circular button
lv_style_t *get_style_circular_button_MAIN_DEFAULT();
void add_style_circular_button(lv_obj_t *obj);
void remove_style_circular_button(lv_obj_t *obj);



#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/
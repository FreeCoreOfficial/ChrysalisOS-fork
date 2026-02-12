#pragma once
#include "../flyui.h"

#ifdef __cplusplus
extern "C" {
#endif

fly_widget_t *fly_label_create(const char *text);
void fly_label_set_text(fly_widget_t *w, const char *text);
fly_widget_t *fly_button_create(const char *text);
void fly_button_set_callback(fly_widget_t *w, fly_on_click_t on_click);
fly_widget_t *fly_image_create(int w, int h, const uint32_t *pixels);
void fly_image_set_data(fly_widget_t *w, const uint32_t *pixels);
fly_widget_t *fly_icon_button_create(int icon_id, const char *text);

fly_widget_t *fly_textbox_create(int w, bool password);
const char *fly_textbox_get_text(fly_widget_t *w);
void fly_textbox_set_text(fly_widget_t *w, const char *text);

/* Panel helper */
fly_widget_t *fly_panel_create(int w, int h);

#ifdef __cplusplus
}
#endif

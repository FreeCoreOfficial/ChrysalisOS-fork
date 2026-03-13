#pragma once
#include "../ui/wm/window.h"
#include "../input/input.h"

#ifdef __cplusplus
extern "C" {
#endif
void file_manager_app_create(void);
void file_manager_app_open_path(const char* path);
bool file_manager_app_handle_event(input_event_t* ev);
window_t* file_manager_app_get_window(void);
#ifdef __cplusplus
}
#endif

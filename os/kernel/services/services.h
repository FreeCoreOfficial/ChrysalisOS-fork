#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start services defined in /system/services (files with .srv) */
void services_start(void);
void services_set_gui_ready(int enabled);

#ifdef __cplusplus
}
#endif

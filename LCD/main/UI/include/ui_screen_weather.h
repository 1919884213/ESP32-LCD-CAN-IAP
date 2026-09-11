#ifndef UI_SCREEN_WEATHER_H
#define UI_SCREEN_WEATHER_H

#include "lvgl.h"

/* Create the weather screen controls. */
void ui_weather_screen_init(lv_obj_t *parent);
/* Start one asynchronous weather request when the screen is opened. */
void ui_weather_screen_request(void);
/* Refresh weather labels from the completed request result. */
void ui_weather_screen_update(void);

#endif /* UI_SCREEN_WEATHER_H */

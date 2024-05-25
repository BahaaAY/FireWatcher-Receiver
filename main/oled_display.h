#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <lvgl.h>

void setupOled(lv_disp_t *disp);
void display_oled_qr(char *link);
void display_oled(lv_disp_t *disp, int16_t *temperature, int16_t *humidity,
                  int16_t *smoke, int16_t *calSmokeVoltage);
#endif // OLED_DISPLAY_H
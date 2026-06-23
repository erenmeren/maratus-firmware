#pragma once

#include <stdbool.h>

typedef struct {
    int brightness;       // -1 follow, else 10..100
    int brightness_base;  // cloud brightness captured when set
    int sleep;            // -1 follow, 0 off, >0 timeout secs
    int sleep_base;       // cloud sleep (encoded) captured when set
} overrides_t;

void overrides_init(overrides_t *o);
void override_set_brightness(overrides_t *o, int val, int cloud_now);
void override_clear_brightness(overrides_t *o);
void override_set_sleep(overrides_t *o, int encoded, int cloud_now);
void override_clear_sleep(overrides_t *o);
void override_reconcile(overrides_t *o, int cloud_brightness, int cloud_sleep_encoded);
int  override_eff_brightness(const overrides_t *o, int cloud_brightness);
int  override_eff_sleep(const overrides_t *o, int cloud_sleep_encoded);

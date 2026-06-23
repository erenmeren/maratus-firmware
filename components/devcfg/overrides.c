#include "overrides.h"

void overrides_init(overrides_t *o) {
    o->brightness = o->brightness_base = -1;
    o->sleep = o->sleep_base = -1;
}

void override_set_brightness(overrides_t *o, int v, int base) {
    o->brightness = v;
    o->brightness_base = base;
}

void override_clear_brightness(overrides_t *o) {
    o->brightness = -1;
    o->brightness_base = -1;
}

void override_set_sleep(overrides_t *o, int e, int base) {
    o->sleep = e;
    o->sleep_base = base;
}

void override_clear_sleep(overrides_t *o) {
    o->sleep = -1;
    o->sleep_base = -1;
}

void override_reconcile(overrides_t *o, int cb, int cs) {
    if (o->brightness >= 0 && cb != o->brightness_base) override_clear_brightness(o);
    if (o->sleep >= 0 && cs != o->sleep_base) override_clear_sleep(o);
}

int override_eff_brightness(const overrides_t *o, int cb) {
    return o->brightness >= 0 ? o->brightness : cb;
}

int override_eff_sleep(const overrides_t *o, int cs) {
    return o->sleep >= 0 ? o->sleep : cs;
}

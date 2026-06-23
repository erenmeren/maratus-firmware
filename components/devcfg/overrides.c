#include "overrides.h"

void devset_init(device_settings_t *d) {
    d->use_device = 0;
    d->brightness = -1;
    d->sleep = -1;
}

int devset_eff_brightness(const device_settings_t *d, int cb) {
    return (d->use_device && d->brightness >= 0) ? d->brightness : cb;
}

int devset_eff_sleep(const device_settings_t *d, int cs) {
    return (d->use_device && d->sleep >= 0) ? d->sleep : cs;
}

void devset_set_use_device(device_settings_t *d, int on, int cb, int cs) {
    d->use_device = on ? 1 : 0;
    if (d->use_device) {
        if (d->brightness < 0) d->brightness = cb;
        if (d->sleep < 0)      d->sleep = cs;
    }
}

void devset_set_brightness(device_settings_t *d, int level) {
    d->brightness = level;
}

void devset_set_sleep(device_settings_t *d, int e) {
    d->sleep = e;
}

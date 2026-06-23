#pragma once

#include <stdbool.h>

// Device-settings source model. A single `use_device` flag governs BOTH
// brightness and sleep: when off, both effective values follow the admin
// (server) policy; when on, the device-local values win. brightness/sleep == -1
// means "unset" and is seeded from the cloud value the moment use_device is
// enabled, so nothing jumps.
typedef struct {
    int use_device;   // 0 = follow admin (server), 1 = use device-local values
    int brightness;   // device-local level 10..100, or -1 = unset
    int sleep;        // device-local sleep encoded: 0 off / >0 timeout secs, or -1 = unset
} device_settings_t;

void devset_init(device_settings_t *d);                    // use_device=0, brightness=-1, sleep=-1
int  devset_eff_brightness(const device_settings_t *d, int cloud_brightness);   // use_device&&br>=0 ? br : cloud
int  devset_eff_sleep(const device_settings_t *d, int cloud_sleep_encoded);     // use_device&&sl>=0 ? sl : cloud
// Turn the source on/off; when enabling, seed any unset (-1) device value from
// the cloud so nothing jumps.
void devset_set_use_device(device_settings_t *d, int on, int cloud_brightness, int cloud_sleep_encoded);
void devset_set_brightness(device_settings_t *d, int level);   // sets d->brightness
void devset_set_sleep(device_settings_t *d, int encoded);      // sets d->sleep

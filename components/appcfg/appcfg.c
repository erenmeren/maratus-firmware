#include "appcfg.h"
#include "sdkconfig.h"

const char *appcfg_base_url(void)   { return CONFIG_DITTO_API_BASE_URL; }
const char *appcfg_device_key(void) { return CONFIG_DITTO_DEVICE_KEY; }
const char *appcfg_fw_version(void) { return CONFIG_DITTO_FW_VERSION; }
int appcfg_roll_width(void)         { return CONFIG_DITTO_ROLL_WIDTH; }

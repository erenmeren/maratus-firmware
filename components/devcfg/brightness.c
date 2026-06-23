#include "brightness.h"
#include <math.h>

#define BRIGHTNESS_GAMMA 0.5f   // <1 boosts low/mid; tune 0.45..0.7 to taste

int brightness_level_to_duty(int level) {
    if (level < 10)  level = 10;
    if (level > 100) level = 100;
    int duty = (int)lroundf(100.0f * powf((float)level / 100.0f, BRIGHTNESS_GAMMA));
    if (duty < 10)  duty = 10;
    if (duty > 100) duty = 100;
    return duty;
}

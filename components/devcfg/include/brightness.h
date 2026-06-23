#pragma once

// Map a perceptual brightness level (10..100, what the slider/admin set) to a
// backlight PWM duty (10..100) using a gamma curve so the low/mid range isn't
// perceptually too dark.
// duty = 100 * (level/100)^BRIGHTNESS_GAMMA, clamped to [10,100].
int brightness_level_to_duty(int level);

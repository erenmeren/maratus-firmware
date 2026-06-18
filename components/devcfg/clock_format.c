#include "clock_format.h"
#include <stdio.h>
#include <string.h>

static const char *const WDAY[7] = {"Sunday","Monday","Tuesday","Wednesday",
                                    "Thursday","Friday","Saturday"};
static const char *const MON[12] = {"January","February","March","April","May","June",
                                    "July","August","September","October","November","December"};

void format_clock_time(const struct tm *tm, bool h24, char *out, int outlen) {
    if (outlen <= 0) return;
    if (h24) {
        snprintf(out, outlen, "%d:%02d", tm->tm_hour, tm->tm_min);
    } else {
        int h = tm->tm_hour % 12;
        if (h == 0) h = 12;
        const char *ap = tm->tm_hour < 12 ? "AM" : "PM";
        snprintf(out, outlen, "%d:%02d %s", h, tm->tm_min, ap);
    }
}

void format_clock_date(const struct tm *tm, bool weekday, char *out, int outlen) {
    if (outlen <= 0) return;
    int wd = tm->tm_wday, mo = tm->tm_mon;
    if (wd < 0 || wd > 6) wd = 0;
    if (mo < 0 || mo > 11) mo = 0;
    if (weekday) {
        snprintf(out, outlen, "%s, %s %d", WDAY[wd], MON[mo], tm->tm_mday);
    } else {
        snprintf(out, outlen, "%s %d", MON[mo], tm->tm_mday);
    }
}

void format_mmss(int seconds, char *out, int outlen) {
    if (outlen <= 0) return;
    if (seconds < 0) seconds = 0;
    snprintf(out, outlen, "%d:%02d", seconds / 60, seconds % 60);
}

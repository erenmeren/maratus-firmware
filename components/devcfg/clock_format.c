#include "clock_format.h"
#include <stdio.h>
#include <string.h>

static const char *const WDAY[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *const MON[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};

void format_clock(const struct tm *tm, bool h24, bool date, bool weekday,
                  char *out, int outlen) {
    if (outlen <= 0) return;
    int n;
    if (h24) {
        n = snprintf(out, outlen, "%d:%02d", tm->tm_hour, tm->tm_min);
    } else {
        int h = tm->tm_hour % 12;
        if (h == 0) h = 12;
        const char *ap = tm->tm_hour < 12 ? "AM" : "PM";
        n = snprintf(out, outlen, "%d:%02d %s", h, tm->tm_min, ap);
    }
    if (!date || n < 0 || n >= outlen) return;

    int wd = tm->tm_wday, mo = tm->tm_mon;
    if (wd < 0 || wd > 6) wd = 0;
    if (mo < 0 || mo > 11) mo = 0;
    if (weekday) {
        snprintf(out + n, outlen - n, "\n%s %d %s", WDAY[wd], tm->tm_mday, MON[mo]);
    } else {
        snprintf(out + n, outlen - n, "\n%d %s", tm->tm_mday, MON[mo]);
    }
}

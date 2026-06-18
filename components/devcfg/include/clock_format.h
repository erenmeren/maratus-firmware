#pragma once
#include <time.h>
#include <stdbool.h>

// Format a clock string from a broken-down local time.
//   h24:     true = 24-hour ("14:05"), false = 12-hour ("2:05 PM")
//   date:    append the day-of-month line content
//   weekday: include the weekday
// Writes into out (size outlen). Layout: "TIME" optionally followed by a newline
// and a "Weekday DD Month" date line (matching the branding preview's clock).
void format_clock(const struct tm *tm, bool h24, bool date, bool weekday,
                  char *out, int outlen);

// Format a remaining-seconds count as "M:SS" (e.g. 48 -> "0:48", 125 -> "2:05").
// Negative inputs clamp to 0. Writes into out (size outlen).
void format_mmss(int seconds, char *out, int outlen);
